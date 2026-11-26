#include "indexer.hpp"
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <memory>
#include <mutex>

namespace {

constexpr uint64_t RECORD_MASK = 0x0000FFFFFFFFFFFFull; // lower 48 bits of an FRN
constexpr uint64_t ROOT_RECORD = 5;                     // MFT record of the volume root
constexpr DWORD BUFFER_SIZE = 1024 * 1024;

constexpr uint32_t INDEX_MAGIC = 0x31584446; // "FDX1"
constexpr uint32_t INDEX_VERSION = 1;
constexpr uint32_t END_MARKER = 0xFFFFFFFF;

constexpr ULONGLONG SAVE_INTERVAL_MS = 5 * 60 * 1000;

inline uint64_t recordOf(uint64_t frn) { return frn & RECORD_MASK; }
inline uint16_t sequenceOf(uint64_t frn) { return static_cast<uint16_t>(frn >> 48); }

// Case folding table for the BMP. Built once, used for case-insensitive search
// without allocating a lowercase copy of every name.
const wchar_t* foldTable() {
    static const std::unique_ptr<wchar_t[]> table = [] {
        std::unique_ptr<wchar_t[]> t(new wchar_t[65536]);
        for (uint32_t i = 0; i < 65536; ++i) t[i] = static_cast<wchar_t>(i);
        CharLowerBuffW(t.get() + 1, 65535);
        return t;
    }();
    return table.get();
}

// Position of `folded_needle` inside `haystack` (compared case-insensitively),
// or npos.
size_t findFolded(const std::wstring& haystack, const std::wstring& folded_needle, const wchar_t* fold) {
    const size_t n = folded_needle.size();
    const size_t h = haystack.size();
    if (n == 0) return 0;
    if (n > h) return std::wstring::npos;
    const wchar_t first = folded_needle[0];
    for (size_t i = 0; i + n <= h; ++i) {
        if (fold[static_cast<uint16_t>(haystack[i])] != first) continue;
        size_t j = 1;
        while (j < n && fold[static_cast<uint16_t>(haystack[i + j])] == folded_needle[j]) ++j;
        if (j == n) return i;
    }
    return std::wstring::npos;
}

inline bool isWordChar(wchar_t c) { return iswalnum(c) != 0; }

// Minimal growable byte buffer for (de)serialising the index.
struct Writer {
    std::vector<char> data;
    template <typename T> void put(const T& v) {
        const char* p = reinterpret_cast<const char*>(&v);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void putBytes(const void* p, size_t n) {
        const char* c = static_cast<const char*>(p);
        data.insert(data.end(), c, c + n);
    }
};

struct Reader {
    const char* p;
    const char* end;
    template <typename T> bool get(T& v) {
        if (static_cast<size_t>(end - p) < sizeof(T)) return false;
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return true;
    }
    bool getBytes(void* out, size_t n) {
        if (static_cast<size_t>(end - p) < n) return false;
        std::memcpy(out, p, n);
        p += n;
        return true;
    }
};

} // namespace

USNIndexer::USNIndexer() {
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

USNIndexer::~USNIndexer() {
    closeVolume();
    if (stop_event) CloseHandle(stop_event);
    if (io_event) CloseHandle(io_event);
}

void USNIndexer::stop() {
    stopping = true;
    if (stop_event) SetEvent(stop_event);
}

bool USNIndexer::loadSnapshot(wchar_t drive_letter) {
    drive = static_cast<wchar_t>(towupper(drive_letter));
    wchar_t root[] = L"X:\\";
    root[0] = drive;
    if (!GetVolumeInformationW(root, nullptr, 0, &volume_serial, nullptr, nullptr, nullptr, 0)) return false;
    USN ignored = 0;
    if (!loadIndex(nullptr, ignored)) return false;
    current_state = IndexState::Ready;
    ++change_generation;
    return true;
}

void USNIndexer::setError(const std::wstring& msg) {
    {
        std::unique_lock lock(mutex);
        last_error = msg;
    }
    current_state = IndexState::Error;
    notify();
}

std::wstring USNIndexer::lastError() const {
    std::shared_lock lock(mutex);
    return last_error;
}

void USNIndexer::notify() {
    ++change_generation;
    if (onChanged) onChanged();
}

// ---------------------------------------------------------------------------
// Volume / IOCTL helpers
// ---------------------------------------------------------------------------

bool USNIndexer::openVolume(wchar_t drive_letter) {
    drive = static_cast<wchar_t>(towupper(drive_letter));

    wchar_t root[] = L"X:\\";
    root[0] = drive;
    wchar_t fs_name[MAX_PATH + 1] = {};
    if (!GetVolumeInformationW(root, nullptr, 0, &volume_serial, nullptr, nullptr, fs_name, MAX_PATH)) {
        setError(L"Could not query volume information (error " + std::to_wstring(GetLastError()) + L")");
        return false;
    }
    if (_wcsicmp(fs_name, L"NTFS") != 0) {
        setError(std::wstring(L"Volume is not NTFS (") + fs_name + L")");
        return false;
    }

    wchar_t device[] = L"\\\\.\\X:";
    device[4] = drive;
    // FILE_FLAG_OVERLAPPED lets us cancel the blocking journal read on stop().
    volume = CreateFileW(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (volume == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        setError(err == ERROR_ACCESS_DENIED
                     ? std::wstring(L"Access denied opening the volume. Run Findex as administrator.")
                     : L"Could not open volume (error " + std::to_wstring(err) + L")");
        return false;
    }
    return true;
}

void USNIndexer::closeVolume() {
    if (volume != INVALID_HANDLE_VALUE) {
        CloseHandle(volume);
        volume = INVALID_HANDLE_VALUE;
    }
}

bool USNIndexer::ioctl(DWORD code, void* in, DWORD in_size, void* out, DWORD out_size, DWORD* returned, bool cancellable) {
    OVERLAPPED ov = {};
    ResetEvent(io_event);
    ov.hEvent = io_event;

    *returned = 0;
    if (DeviceIoControl(volume, code, in, in_size, out, out_size, returned, &ov)) return true;

    DWORD err = GetLastError();
    if (err != ERROR_IO_PENDING) return false;

    if (cancellable) {
        HANDLE handles[2] = {io_event, stop_event};
        DWORD w = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0) {
            CancelIoEx(volume, &ov);
            GetOverlappedResult(volume, &ov, returned, TRUE); // wait for the cancel to complete
            SetLastError(ERROR_OPERATION_ABORTED);
            return false;
        }
    }
    return GetOverlappedResult(volume, &ov, returned, TRUE) != FALSE;
}

bool USNIndexer::queryJournal(USN_JOURNAL_DATA_V0& data) {
    DWORD returned = 0;
    return ioctl(FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &data, sizeof(data), &returned, false);
}

bool USNIndexer::ensureJournal(USN_JOURNAL_DATA_V0& data) {
    if (queryJournal(data)) return true;

    DWORD err = GetLastError();
    if (err != ERROR_JOURNAL_NOT_ACTIVE && err != ERROR_JOURNAL_DELETE_IN_PROGRESS) {
        setError(L"FSCTL_QUERY_USN_JOURNAL failed (error " + std::to_wstring(err) + L")");
        return false;
    }

    // Same defaults Windows uses: 32 MB maximum, 8 MB allocation delta.
    CREATE_USN_JOURNAL_DATA create = {};
    create.MaximumSize = 32ull * 1024 * 1024;
    create.AllocationDelta = 8ull * 1024 * 1024;
    DWORD returned = 0;
    if (!ioctl(FSCTL_CREATE_USN_JOURNAL, &create, sizeof(create), nullptr, 0, &returned, false)) {
        setError(L"FSCTL_CREATE_USN_JOURNAL failed (error " + std::to_wstring(GetLastError()) + L")");
        return false;
    }
    if (!queryJournal(data)) {
        setError(L"FSCTL_QUERY_USN_JOURNAL failed (error " + std::to_wstring(GetLastError()) + L")");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Index mutation (caller holds the unique lock)
// ---------------------------------------------------------------------------

void USNIndexer::upsert(uint64_t frn, uint64_t parent_frn, uint32_t attributes, const wchar_t* name, size_t name_len) {
    if (name_len == 0) return;
    const uint64_t rec = recordOf(frn);
    if (rec >= entries.size()) {
        if (rec > (1ull << 32)) return; // garbage, refuse to allocate
        entries.resize(std::max<size_t>(static_cast<size_t>(rec) + 1, entries.size() + entries.size() / 4));
    }
    IndexEntry& e = entries[rec];
    if (!e.used()) ++file_count;
    e.name.assign(name, name_len);
    e.parent_frn = parent_frn;
    e.attributes = attributes;
    e.sequence = sequenceOf(frn);
}

void USNIndexer::remove(uint64_t frn) {
    const uint64_t rec = recordOf(frn);
    if (rec >= entries.size()) return;
    IndexEntry& e = entries[rec];
    if (!e.used() || e.sequence != sequenceOf(frn)) return; // slot already reused by another file
    e = IndexEntry{};
    --file_count;
}

void USNIndexer::applyRecord(uint64_t frn, uint64_t parent_frn, uint32_t attributes, uint32_t reason,
                             const wchar_t* name, size_t name_len) {
    if (reason & USN_REASON_FILE_DELETE) {
        remove(frn);
        return;
    }
    // The RENAME_OLD_NAME record carries the *previous* name; the matching
    // RENAME_NEW_NAME record that follows has the new one.
    if ((reason & USN_REASON_RENAME_OLD_NAME) && !(reason & USN_REASON_RENAME_NEW_NAME)) return;
    // Hard-link changes carry the name of the link that was added/removed,
    // not necessarily the file's primary name.
    if ((reason & USN_REASON_HARD_LINK_CHANGE) && !(reason & USN_REASON_FILE_CREATE)) return;

    // Every other record carries the file's current name and parent, so an
    // upsert covers creates, renames and moves.
    upsert(frn, parent_frn, attributes, name, name_len);
}

// Parses one record of a buffer returned by FSCTL_ENUM_USN_DATA or
// FSCTL_READ_USN_JOURNAL. Handles V2 (64-bit ids) and V3 (128-bit ids).
bool USNIndexer::parseRecord(const BYTE* p, ParsedRecord& out) {
    const auto* header = reinterpret_cast<const USN_RECORD_COMMON_HEADER*>(p);
    if (header->MajorVersion == 2) {
        const auto* r = reinterpret_cast<const USN_RECORD_V2*>(p);
        out.frn = r->FileReferenceNumber;
        out.parent_frn = r->ParentFileReferenceNumber;
        out.attributes = r->FileAttributes;
        out.reason = r->Reason;
        out.name = reinterpret_cast<const wchar_t*>(p + r->FileNameOffset);
        out.name_len = r->FileNameLength / sizeof(wchar_t);
        return true;
    }
    if (header->MajorVersion == 3) {
        // On NTFS the 128-bit id is the 64-bit FRN zero-extended.
        const auto* r = reinterpret_cast<const USN_RECORD_V3*>(p);
        std::memcpy(&out.frn, r->FileReferenceNumber.Identifier, sizeof(uint64_t));
        std::memcpy(&out.parent_frn, r->ParentFileReferenceNumber.Identifier, sizeof(uint64_t));
        out.attributes = r->FileAttributes;
        out.reason = r->Reason;
        out.name = reinterpret_cast<const wchar_t*>(p + r->FileNameOffset);
        out.name_len = r->FileNameLength / sizeof(wchar_t);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// MFT enumeration and journal reading
// ---------------------------------------------------------------------------

bool USNIndexer::enumerateMft() {
    // Size the table up front from the MFT size.
    NTFS_VOLUME_DATA_BUFFER ntfs = {};
    DWORD returned = 0;
    size_t expected = 0;
    if (ioctl(FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &ntfs, sizeof(ntfs), &returned, false) &&
        ntfs.BytesPerFileRecordSegment > 0) {
        expected = static_cast<size_t>(ntfs.MftValidDataLength.QuadPart / ntfs.BytesPerFileRecordSegment);
    }

    {
        std::unique_lock lock(mutex);
        entries.clear();
        entries.shrink_to_fit();
        entries.resize(expected);
        file_count = 0;
    }

    MFT_ENUM_DATA_V0 enum_data = {};
    enum_data.StartFileReferenceNumber = 0;
    enum_data.LowUsn = 0;
    enum_data.HighUsn = MAXLONGLONG; // everything; changes made meanwhile are replayed from the journal

    auto buffer = std::make_unique<BYTE[]>(BUFFER_SIZE);
    ULONGLONG last_notify = GetTickCount64();

    while (!stopping) {
        if (!ioctl(FSCTL_ENUM_USN_DATA, &enum_data, sizeof(enum_data), buffer.get(), BUFFER_SIZE, &returned, false)) {
            DWORD err = GetLastError();
            if (err == ERROR_HANDLE_EOF) break; // done
            setError(L"FSCTL_ENUM_USN_DATA failed (error " + std::to_wstring(err) + L")");
            return false;
        }
        if (returned <= sizeof(USN)) break;

        {
            std::unique_lock lock(mutex);
            DWORD offset = sizeof(USN);
            while (offset + sizeof(USN_RECORD_COMMON_HEADER) <= returned) {
                const BYTE* p = buffer.get() + offset;
                const DWORD len = reinterpret_cast<const USN_RECORD_COMMON_HEADER*>(p)->RecordLength;
                if (len == 0 || offset + len > returned) break;
                ParsedRecord r;
                if (parseRecord(p, r)) upsert(r.frn, r.parent_frn, r.attributes, r.name, r.name_len);
                offset += len;
            }
        }

        // First 8 bytes: the FRN to continue the enumeration from.
        enum_data.StartFileReferenceNumber = *reinterpret_cast<const DWORDLONG*>(buffer.get());

        if (GetTickCount64() - last_notify > 250) {
            last_notify = GetTickCount64();
            notify();
        }
    }
    return !stopping;
}

USNIndexer::JournalResult USNIndexer::readJournal(USN& start_usn, bool wait) {
    READ_USN_JOURNAL_DATA_V0 read = {};
    read.ReasonMask = 0xFFFFFFFF;
    read.ReturnOnlyOnClose = FALSE;
    read.Timeout = 0;
    read.BytesToWaitFor = wait ? 1 : 0; // 1 => block until at least one new record exists
    read.UsnJournalID = journal_id;

    auto buffer = std::make_unique<BYTE[]>(BUFFER_SIZE);
    ULONGLONG last_save = GetTickCount64();
    bool dirty = false;

    while (!stopping) {
        read.StartUsn = start_usn;
        DWORD returned = 0;
        if (!ioctl(FSCTL_READ_USN_JOURNAL, &read, sizeof(read), buffer.get(), BUFFER_SIZE, &returned, wait)) {
            DWORD err = GetLastError();
            if (stopping || err == ERROR_OPERATION_ABORTED) return JournalResult::Stopped;
            if (err == ERROR_HANDLE_EOF) return JournalResult::Ok;
            // Journal wrapped, was deleted or recreated: the index can't be
            // trusted any more and the caller must rescan.
            return JournalResult::NeedRescan;
        }
        if (returned < sizeof(USN)) return JournalResult::NeedRescan;

        const USN next = *reinterpret_cast<const USN*>(buffer.get());
        if (returned > sizeof(USN)) {
            std::unique_lock lock(mutex);
            DWORD offset = sizeof(USN);
            while (offset + sizeof(USN_RECORD_COMMON_HEADER) <= returned) {
                const BYTE* p = buffer.get() + offset;
                const DWORD len = reinterpret_cast<const USN_RECORD_COMMON_HEADER*>(p)->RecordLength;
                if (len == 0 || offset + len > returned) break;
                ParsedRecord r;
                if (parseRecord(p, r)) applyRecord(r.frn, r.parent_frn, r.attributes, r.reason, r.name, r.name_len);
                offset += len;
            }
            dirty = true;
        }

        const bool caught_up = (returned <= sizeof(USN)) || next == start_usn;
        start_usn = next;

        if (!wait) {
            if (caught_up) return JournalResult::Ok;
            continue;
        }

        notify();
        if (dirty && GetTickCount64() - last_save > SAVE_INTERVAL_MS) {
            saveIndex(start_usn);
            last_save = GetTickCount64();
            dirty = false;
        }
    }
    return JournalResult::Stopped;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

bool USNIndexer::run(wchar_t drive_letter, bool force_rescan) {
    stopping = false;
    ResetEvent(stop_event);
    current_state = IndexState::Loading;
    notify();

    if (!openVolume(drive_letter)) return false;

    USN_JOURNAL_DATA_V0 jd = {};
    if (!ensureJournal(jd)) return false;
    journal_id = jd.UsnJournalID;

    USN next_usn = 0;
    bool need_scan = true;

    if (!force_rescan && loadIndex(&jd, next_usn)) {
        // Catch up with everything that happened while Findex was closed.
        JournalResult r = readJournal(next_usn, false);
        if (r == JournalResult::Stopped) return true;
        need_scan = (r != JournalResult::Ok);
    }

    while (!stopping) {
        if (need_scan) {
            current_state = IndexState::Scanning;
            notify();

            if (!ensureJournal(jd)) return false;
            journal_id = jd.UsnJournalID;
            next_usn = jd.NextUsn; // snapshot taken *before* enumerating

            if (!enumerateMft()) return current_state != IndexState::Error;

            // Replay whatever changed during the enumeration.
            JournalResult r = readJournal(next_usn, false);
            if (r == JournalResult::Stopped) return true;
            if (r == JournalResult::NeedRescan) continue;

            saveIndex(next_usn);
            need_scan = false;
        }

        current_state = IndexState::Ready;
        notify();

        // Watch the journal until stop() or an unrecoverable journal error.
        JournalResult r = readJournal(next_usn, true);
        if (r == JournalResult::Stopped) break;
        need_scan = (r == JournalResult::NeedRescan);
    }

    if (!need_scan) saveIndex(next_usn);
    return true;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

std::wstring USNIndexer::buildPath(uint64_t rec) const {
    std::wstring path;
    path.reserve(260);

    // Walk up to the root collecting components (in reverse order).
    const std::wstring* parts[256];
    size_t count = 0;
    bool orphan = false;
    uint64_t cur = rec;
    while (cur != ROOT_RECORD) {
        if (cur >= entries.size() || !entries[cur].used() || count == 256) {
            orphan = true;
            break;
        }
        const IndexEntry& e = entries[cur];
        parts[count++] = &e.name;
        const uint64_t parent = recordOf(e.parent_frn);
        if (parent == cur) break;
        cur = parent;
    }

    path += drive;
    path += L':';
    if (orphan) path += L"\\<unknown>";
    for (size_t i = count; i-- > 0;) {
        path += L'\\';
        path += *parts[i];
    }
    if (count == 0 && !orphan) path += L'\\';
    return path;
}

std::wstring USNIndexer::fold(const std::wstring& in) {
    const wchar_t* table = foldTable();
    std::wstring out(in);
    for (auto& c : out) c = table[static_cast<uint16_t>(c)];
    return out;
}

std::vector<SearchResult> USNIndexer::search(const std::wstring& query, const SearchOptions& options,
                                             size_t* total_matches) const {
    const wchar_t* fold = foldTable();

    std::vector<std::wstring> terms;
    {
        std::wstring cur;
        for (wchar_t c : query) {
            if (c == L' ' || c == L'\t') {
                if (!cur.empty()) terms.push_back(std::move(cur));
                cur.clear();
            } else {
                cur += fold[static_cast<uint16_t>(c)];
            }
        }
        if (!cur.empty()) terms.push_back(std::move(cur));
    }

    const bool rank = options.rank && !terms.empty();
    const size_t limit = options.limit;

    // Pass 1: collect matching record numbers (+ a score when ranking).
    struct Candidate {
        uint32_t record;
        uint32_t score;
    };
    std::vector<Candidate> candidates;
    size_t total = 0;

    std::shared_lock lock(mutex);
    const size_t n = entries.size();
    for (size_t i = 0; i < n; ++i) {
        const IndexEntry& e = entries[i];
        if (!e.used() || i == ROOT_RECORD) continue;
        if (options.filter == SearchFilter::Files && e.isDirectory()) continue;
        if (options.filter == SearchFilter::Folders && !e.isDirectory()) continue;

        uint32_t category = 3;
        bool match = true;
        for (size_t t = 0; t < terms.size(); ++t) {
            const size_t pos = findFolded(e.name, terms[t], fold);
            if (pos == std::wstring::npos) {
                match = false;
                break;
            }
            if (t == 0 && rank) {
                if (pos == 0 && e.name.size() == terms[0].size()) category = 0;      // exact
                else if (pos == 0) category = 1;                                     // prefix
                else if (!isWordChar(e.name[pos - 1])) category = 2;                 // word start
            }
        }
        if (!match) continue;

        ++total;
        if (!rank) {
            if (candidates.size() < limit) candidates.push_back({static_cast<uint32_t>(i), 0});
            else if (!total_matches) break;
        } else {
            const uint32_t len = static_cast<uint32_t>(std::min<size_t>(e.name.size(), 0xFFFF));
            candidates.push_back({static_cast<uint32_t>(i), (category << 16) | len});
        }
    }

    if (rank) {
        const size_t keep = std::min(limit, candidates.size());
        std::partial_sort(candidates.begin(), candidates.begin() + keep, candidates.end(),
                          [](const Candidate& a, const Candidate& b) {
                              return a.score != b.score ? a.score < b.score : a.record < b.record;
                          });
        candidates.resize(keep);
    }

    // Pass 2: materialise only what will be shown.
    std::vector<SearchResult> results;
    results.reserve(candidates.size());
    for (const Candidate& c : candidates) {
        const IndexEntry& e = entries[c.record];
        SearchResult r;
        r.frn = (static_cast<uint64_t>(e.sequence) << 48) | c.record;
        r.name = e.name;
        r.path = buildPath(c.record);
        r.is_directory = e.isDirectory();
        results.push_back(std::move(r));
    }

    if (total_matches) *total_matches = total;
    return results;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

std::wstring USNIndexer::indexFilePath(wchar_t drive) {
    std::wstring dir;
    PWSTR base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        dir = std::wstring(base) + L"\\Findex\\";
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    CoTaskMemFree(base);
    return dir + L"index_" + static_cast<wchar_t>(towupper(drive)) + L".dat";
}

bool USNIndexer::saveIndex(USN next_usn) {
    Writer w;
    {
        std::shared_lock lock(mutex);
        w.data.reserve(64 + file_count.load() * 48);
        w.put(INDEX_MAGIC);
        w.put(INDEX_VERSION);
        w.put(static_cast<uint32_t>(volume_serial));
        w.put(static_cast<uint32_t>(drive));
        w.put(static_cast<uint64_t>(journal_id));
        w.put(static_cast<int64_t>(next_usn));
        w.put(static_cast<uint32_t>(entries.size()));
        for (size_t i = 0; i < entries.size(); ++i) {
            const IndexEntry& e = entries[i];
            if (!e.used() || e.name.size() > 0xFFFF) continue;
            w.put(static_cast<uint32_t>(i));
            w.put(e.sequence);
            w.put(e.attributes);
            w.put(e.parent_frn);
            w.put(static_cast<uint16_t>(e.name.size()));
            w.putBytes(e.name.data(), e.name.size() * sizeof(wchar_t));
        }
        w.put(END_MARKER);
    }

    // Write to a temp file and swap it in, so a crash never leaves a torn index.
    const std::wstring path = indexFilePath(drive);
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    size_t off = 0;
    while (ok && off < w.data.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(w.data.size() - off, 16 * 1024 * 1024));
        DWORD written = 0;
        ok = WriteFile(f, w.data.data() + off, chunk, &written, nullptr) && written == chunk;
        off += chunk;
    }
    CloseHandle(f);

    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool USNIndexer::loadIndex(const USN_JOURNAL_DATA_V0* current, USN& next_usn) {
    HANDLE f = CreateFileW(indexFilePath(drive).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(f, &size) || size.QuadPart < 64 || size.QuadPart > (4ll << 30)) {
        CloseHandle(f);
        return false;
    }
    std::vector<char> data(static_cast<size_t>(size.QuadPart));
    size_t off = 0;
    bool ok = true;
    while (ok && off < data.size()) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - off, 16 * 1024 * 1024));
        DWORD got = 0;
        ok = ReadFile(f, data.data() + off, chunk, &got, nullptr) && got == chunk;
        off += chunk;
    }
    CloseHandle(f);
    if (!ok) return false;

    Reader r{data.data(), data.data() + data.size()};
    uint32_t magic, version, serial, drv, table_size;
    uint64_t jid;
    int64_t usn;
    if (!r.get(magic) || !r.get(version) || !r.get(serial) || !r.get(drv) || !r.get(jid) || !r.get(usn) ||
        !r.get(table_size))
        return false;

    // Only usable if it belongs to this volume and journal, and the journal
    // still contains every record since the moment it was saved.
    if (magic != INDEX_MAGIC || version != INDEX_VERSION || serial != volume_serial || drv != drive) return false;
    if (current && (jid != current->UsnJournalID || usn < current->FirstUsn || usn > current->NextUsn)) return false;
    if (table_size > (1u << 31)) return false;

    std::vector<IndexEntry> loaded(table_size);
    size_t count = 0;
    while (true) {
        uint32_t rec;
        if (!r.get(rec)) return false;
        if (rec == END_MARKER) break;
        IndexEntry e;
        uint16_t name_len;
        if (!r.get(e.sequence) || !r.get(e.attributes) || !r.get(e.parent_frn) || !r.get(name_len)) return false;
        if (rec >= loaded.size() || name_len == 0) return false;
        e.name.resize(name_len);
        if (!r.getBytes(e.name.data(), name_len * sizeof(wchar_t))) return false;
        if (!loaded[rec].used()) ++count;
        loaded[rec] = std::move(e);
    }

    {
        std::unique_lock lock(mutex);
        entries = std::move(loaded);
        file_count = count;
    }
    next_usn = usn;
    notify();
    return true;
}
