#pragma once

#include <windows.h>
#include <winioctl.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <string>
#include <vector>

// One entry per MFT record. The vector index *is* the MFT record number
// (lower 48 bits of the File Reference Number), so lookups by FRN are O(1)
// and no hashing is needed.
struct IndexEntry {
    std::wstring name;          // empty name == unused slot
    uint64_t parent_frn = 0;    // full parent FRN (record number + sequence)
    uint32_t attributes = 0;
    uint16_t sequence = 0;      // upper 16 bits of the FRN

    bool used() const { return !name.empty(); }
    bool isDirectory() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

struct SearchResult {
    uint64_t frn;
    std::wstring name;
    std::wstring path;
    bool is_directory;
};

enum class SearchFilter { All, Files, Folders };

struct SearchOptions {
    size_t limit = 100;
    SearchFilter filter = SearchFilter::All;
    // Best matches first: exact name, then prefix, then word start, then
    // anywhere; shorter names win ties. Off = MFT order (slightly faster).
    bool rank = true;
};

enum class IndexState { Idle, Loading, Scanning, Ready, Error };

inline bool isBusy(IndexState s) {
    return s == IndexState::Idle || s == IndexState::Loading || s == IndexState::Scanning;
}

class USNIndexer {
public:
    USNIndexer();
    ~USNIndexer();

    USNIndexer(const USNIndexer&) = delete;
    USNIndexer& operator=(const USNIndexer&) = delete;

    // Opens the volume, loads the saved index (or enumerates the MFT when it is
    // missing/stale), catches up with the USN journal and then keeps watching it
    // until stop() is called. Blocking: call it from a worker thread.
    // `force_rescan` ignores the saved index and reads the MFT again.
    bool run(wchar_t drive_letter, bool force_rescan = false);

    // Loads the index saved by a previous run() without opening the volume, so
    // it works without administrator rights. The data is as fresh as the last
    // save and is not updated. Don't combine with run().
    bool loadSnapshot(wchar_t drive_letter);

    // Where the index of a drive is saved (%LOCALAPPDATA%\Findex\index_X.dat).
    static std::wstring indexFilePath(wchar_t drive_letter);

    // Thread-safe. Makes run() return as soon as possible.
    void stop();

    // Thread-safe, can be called while run() is active.
    // Case-insensitive search on file names. Whitespace separates terms and
    // all of them must appear in the name. An empty query matches everything.
    // `total_matches` (optional) receives the full number of matches.
    std::vector<SearchResult> search(const std::wstring& query, const SearchOptions& options,
                                     size_t* total_matches = nullptr) const;

    // Lowercases the same way search() compares (useful to highlight matches).
    static std::wstring fold(const std::wstring& s);

    size_t fileCount() const { return file_count.load(); }
    IndexState state() const { return current_state.load(); }
    // Increments every time the index or its state changes. Cheap to poll.
    uint64_t generation() const { return change_generation.load(); }
    std::wstring lastError() const;

    // Called from the indexing thread whenever the index changes (scan
    // finished, journal changes applied). Keep it cheap: just post a message
    // to the UI thread.
    std::function<void()> onChanged;

private:
    struct ParsedRecord {
        uint64_t frn = 0;
        uint64_t parent_frn = 0;
        uint32_t attributes = 0;
        uint32_t reason = 0;
        const wchar_t* name = nullptr;
        size_t name_len = 0;
    };
    enum class JournalResult { Ok, Stopped, NeedRescan };

    bool openVolume(wchar_t drive_letter);
    void closeVolume();
    bool ioctl(DWORD code, void* in, DWORD in_size, void* out, DWORD out_size, DWORD* returned, bool cancellable);

    bool queryJournal(USN_JOURNAL_DATA_V0& data);
    bool ensureJournal(USN_JOURNAL_DATA_V0& data);

    bool enumerateMft();
    // Reads journal records starting at `start_usn`, which is advanced past
    // the processed records. When `wait` is true it keeps blocking for new
    // records until stop() is called or the journal becomes unusable.
    JournalResult readJournal(USN& start_usn, bool wait);

    static bool parseRecord(const BYTE* p, ParsedRecord& out);
    void applyRecord(uint64_t frn, uint64_t parent_frn, uint32_t attributes, uint32_t reason,
                     const wchar_t* name, size_t name_len);
    void upsert(uint64_t frn, uint64_t parent_frn, uint32_t attributes, const wchar_t* name, size_t name_len);
    void remove(uint64_t frn);

    std::wstring buildPath(uint64_t record) const; // caller holds the lock

    bool saveIndex(USN next_usn);
    // `current` == nullptr skips the journal checks (snapshot mode).
    bool loadIndex(const USN_JOURNAL_DATA_V0* current, USN& next_usn);

    void setError(const std::wstring& msg);
    void notify();

    HANDLE volume = INVALID_HANDLE_VALUE;
    HANDLE stop_event = nullptr;
    HANDLE io_event = nullptr;
    wchar_t drive = L'C';
    DWORD volume_serial = 0;
    uint64_t journal_id = 0;

    mutable std::shared_mutex mutex;
    std::vector<IndexEntry> entries;
    std::atomic<size_t> file_count{0};
    std::atomic<IndexState> current_state{IndexState::Idle};
    std::atomic<bool> stopping{false};
    std::atomic<uint64_t> change_generation{0};
    std::wstring last_error;
};
