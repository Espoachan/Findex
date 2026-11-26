// findex: instant file search for NTFS volumes, from the terminal.
#include "elevate.hpp"
#include "findex.hpp"
#include "service.hpp"
#include "source.hpp"
#include "term.hpp"
#include "tui.hpp"

#include <windows.h>
#include <objbase.h>

#include <chrono>
#include <cwctype>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace term;

namespace {

constexpr const wchar_t* VERSION = L"0.3.0";

struct Options {
    enum class Mode { Search, InstallService, UninstallService, ServiceStatus, RunService };

    Mode mode = Mode::Search;
    wchar_t drive = L'C';
    size_t limit = 30;
    SearchFilter filter = SearchFilter::All;
    bool paths_only = false;
    bool rank = true;
    bool rescan = false;
    bool local = false; // --local: don't use the service
    bool color = true;
    std::wstring query;
    std::wstring sid;    // --user / --for-user (internal, used by the service install)
    std::wstring drives; // --drives (internal)
};

void printHelp(bool color) {
    const std::wstring A = color ? ACCENT : L"", B = color ? BOLD : L"", M = color ? MUTED : L"",
                       R = color ? RESET : L"";
    auto opt = [&](const wchar_t* flags, const wchar_t* what) {
        std::wstring f = flags;
        f.resize(std::max<size_t>(f.size(), 29), L' ');
        return L"  " + f + M + what + R + L"\n";
    };
    out(B + A + L"✻ Findex" + R + L" " + VERSION + M + L"  instant file search for NTFS" + R + L"\n\n" +
        B + L"Usage" + R + L"\n" +
        opt(L"findex", L"interactive search (live as you type)") +
        opt(L"findex <words...>", L"print the best matches and exit") + L"\n" +
        B + L"Options" + R + L"\n" +
        opt(L"-d, --drive <letter>", L"volume to search (default C)") +
        opt(L"-n, --limit <n>", L"max results to print, 0 = all (default 30)") +
        opt(L"-f, --files", L"only files") +
        opt(L"-F, --folders", L"only folders") +
        opt(L"-p, --paths", L"plain paths, one per line (for scripts)") +
        opt(L"    --no-rank", L"keep MFT order instead of sorting by relevance") +
        opt(L"    --no-color", L"disable colours") +
        opt(L"    --local", L"index in this process instead of using the service") +
        opt(L"    --rescan", L"(with --local) ignore the saved index, read the MFT again") +
        opt(L"-h, --help", L"show this help") +
        opt(L"-v, --version", L"show the version") + L"\n" +
        B + L"Service" + R + M + L"  no administrator prompt every time you open findex" + R + L"\n" +
        opt(L"--install-service [drives]", L"install the background indexer (e.g. C D)") +
        opt(L"--uninstall-service", L"remove it") +
        opt(L"--status", L"show whether it is running") + L"\n" +
        B + L"Examples" + R + L"\n" +
        L"  findex invoice 2024 pdf\n" +
        L"  findex -F node_modules -n 0 -p > folders.txt\n" +
        L"  findex --install-service C D\n");
}

// Returns false on a usage error. Sets `exit_now` for --help / --version.
bool parseArgs(int argc, wchar_t** argv, Options& o, bool& exit_now) {
    exit_now = false;
    auto addWord = [&](const std::wstring& w) { o.query += (o.query.empty() ? L"" : L" ") + w; };

    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        auto value = [&](std::wstring& v) {
            if (i + 1 >= argc) {
                err(L"findex: " + a + L" needs a value\n");
                return false;
            }
            v = argv[++i];
            return true;
        };

        if (a == L"-h" || a == L"--help" || a == L"/?") {
            printHelp(o.color && isConsole(GetStdHandle(STD_OUTPUT_HANDLE)));
            exit_now = true;
            return true;
        }
        if (a == L"-v" || a == L"--version") {
            out(std::wstring(L"findex ") + VERSION + L"\n");
            exit_now = true;
            return true;
        }
        if (a == L"-d" || a == L"--drive") {
            std::wstring v;
            if (!value(v)) return false;
            if (v.empty() || !iswalpha(v[0])) {
                err(L"findex: invalid drive '" + v + L"'\n");
                return false;
            }
            o.drive = static_cast<wchar_t>(towupper(v[0]));
        } else if (a == L"-n" || a == L"--limit") {
            std::wstring v;
            if (!value(v)) return false;
            wchar_t* end = nullptr;
            const long long n = wcstoll(v.c_str(), &end, 10);
            if (v.empty() || *end != L'\0' || n < 0) {
                err(L"findex: invalid limit '" + v + L"'\n");
                return false;
            }
            o.limit = n == 0 ? SIZE_MAX : static_cast<size_t>(n);
        } else if (a == L"-f" || a == L"--files") {
            o.filter = SearchFilter::Files;
        } else if (a == L"-F" || a == L"--folders") {
            o.filter = SearchFilter::Folders;
        } else if (a == L"-p" || a == L"--paths") {
            o.paths_only = true;
        } else if (a == L"--no-rank") {
            o.rank = false;
        } else if (a == L"--no-color") {
            o.color = false;
        } else if (a == L"--rescan") {
            o.rescan = true;
        } else if (a == L"--local") {
            o.local = true;
        } else if (a == L"--install-service") {
            o.mode = Options::Mode::InstallService;
        } else if (a == L"--uninstall-service") {
            o.mode = Options::Mode::UninstallService;
        } else if (a == L"--status") {
            o.mode = Options::Mode::ServiceStatus;
        } else if (a == L"--service") { // used by the Service Control Manager
            o.mode = Options::Mode::RunService;
        } else if (a == L"--user" || a == L"--for-user") {
            if (!value(o.sid)) return false;
        } else if (a == L"--drives") {
            if (!value(o.drives)) return false;
        } else if (a == L"--") {
            for (++i; i < argc; ++i) addWord(argv[i]); // the rest is query, even if it starts with '-'
        } else if (a.size() > 1 && a[0] == L'-') {
            err(L"findex: unknown option '" + a + L"' (see findex --help)\n");
            return false;
        } else {
            addWord(a);
        }
    }
    return true;
}

void enableVt(HANDLE h) {
    DWORD mode;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

std::wstring fileAge(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return L"earlier";
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    auto ticks = [](const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    const uint64_t secs = (ticks(now) - ticks(data.ftLastWriteTime)) / 10000000ull;
    if (secs < 120) return L"just now";
    if (secs < 7200) return std::to_wstring(secs / 60) + L" minutes ago";
    if (secs < 172800) return std::to_wstring(secs / 3600) + L" hours ago";
    return std::to_wstring(secs / 86400) + L" days ago";
}

std::vector<std::wstring> foldedTerms(const std::wstring& query) {
    std::vector<std::wstring> terms;
    std::wstring cur;
    for (wchar_t c : USNIndexer::fold(query)) {
        if (c == L' ' || c == L'\t') {
            if (!cur.empty()) terms.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) terms.push_back(cur);
    return terms;
}

// Dim parent folder, bright name with the matched parts highlighted.
void appendColored(std::wstring& buf, const SearchResult& r, const std::vector<std::wstring>& terms) {
    const size_t name_at = r.path.size() >= r.name.size() ? r.path.size() - r.name.size() : 0;
    buf += MUTED;
    buf += r.path.substr(0, name_at);
    buf += RESET;

    std::vector<bool> mask(r.name.size(), false);
    const std::wstring folded = USNIndexer::fold(r.name);
    for (const auto& t : terms)
        for (size_t p = folded.find(t); p != std::wstring::npos; p = folded.find(t, p + 1))
            for (size_t k = 0; k < t.size(); ++k) mask[p + k] = true;

    const std::wstring base = r.is_directory ? FOLDER : L"";
    bool hl = false;
    buf += base;
    for (size_t i = 0; i < r.name.size(); ++i) {
        if (mask[i] != hl) {
            hl = mask[i];
            buf += RESET;
            buf += hl ? std::wstring(BOLD) + MATCH : base;
        }
        buf += r.name[i];
    }
    buf += RESET;
    if (r.is_directory) buf += std::wstring(FOLDER) + L"\\" + RESET;
}

// Picks where results come from: the service if it's available, otherwise the
// indexer in this process (admin) or, for one-shot searches, the saved index.
// `why_no_service` explains why the service wasn't used.
std::unique_ptr<IndexSource> openSource(const Options& o, bool interactive, std::wstring& why_no_service) {
    if (!o.local) {
        if (auto client = ServiceClient::connect(o.drive, why_no_service)) return client;
    } else {
        why_no_service = L"--local was given";
    }
    if (isAdministrator()) return LocalSource::start(o.drive, o.rescan);
    if (!interactive) return LocalSource::snapshot(o.drive);
    return nullptr; // interactive without admin: the caller elevates
}

// One-shot mode: print matches and exit.
int searchOnce(const Options& o) {
    const bool color = o.color && !o.paths_only && isConsole(GetStdHandle(STD_OUTPUT_HANDLE));
    const bool err_color = o.color && isConsole(GetStdHandle(STD_ERROR_HANDLE));
    if (color) enableVt(GetStdHandle(STD_OUTPUT_HANDLE));
    if (err_color) enableVt(GetStdHandle(STD_ERROR_HANDLE));
    const std::wstring M = err_color ? MUTED : L"", W = err_color ? WARN : L"", X = err_color ? BAD : L"",
                       R = err_color ? RESET : L"";
    const std::wstring drive = std::wstring(1, o.drive) + L":";

    std::wstring why;
    std::unique_ptr<IndexSource> src = openSource(o, false, why);
    if (!src) {
        err(X + L"findex: there is no index for " + drive + R + L" Install the service (findex --install-service " +
            std::wstring(1, o.drive) + L") or run findex once as administrator.\n");
        return 1;
    }
    if (src->label() == L"saved index, not live" && !o.paths_only) {
        err(W + L"! " + R + M + L"Using the index saved " + fileAge(USNIndexer::indexFilePath(o.drive)) +
            L" (not live): " + why + L". Run findex --install-service to fix this." + R + L"\n");
    }

    bool announced = false;
    for (;;) {
        src->poll();
        const IndexState s = src->state();
        if (!isBusy(s)) break;
        if (!announced && s == IndexState::Scanning && !o.paths_only) {
            err(M + L"Reading the MFT of " + drive + L" (only needed the first time)…" + R + L"\n");
            announced = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (src->state() == IndexState::Error) {
        err(X + L"findex: " + src->lastError() + R + L"\n");
        return 1;
    }

    SearchOptions so;
    so.limit = o.limit;
    so.filter = o.filter;
    so.rank = o.rank;
    size_t total = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const auto results = src->search(o.query, so, &total);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    const auto terms = foldedTerms(o.query);
    std::wstring buf;
    for (const auto& r : results) {
        if (color) appendColored(buf, r, terms);
        else buf += r.path;
        buf += L"\n";
        if (buf.size() > 64 * 1024) {
            out(buf);
            buf.clear();
        }
    }
    out(buf);

    if (!o.paths_only) {
        std::wstring summary = M + formatCount(total) + (total == 1 ? L" match" : L" matches");
        if (total > results.size()) summary += L" · showing " + formatCount(results.size()) + L" (-n 0 for all)";
        wchar_t t[32];
        swprintf(t, 32, L" · %.1f ms", ms);
        summary += t;
        err((results.empty() ? L"" : L"\n") + summary + R + L"\n");
    }

    src->close();
    return results.empty() ? 1 : 0;
}

int interactive(const Options& o) {
    if (!isConsole(GetStdHandle(STD_INPUT_HANDLE)) || !isConsole(GetStdHandle(STD_OUTPUT_HANDLE))) {
        err(L"findex: interactive mode needs a terminal. Pass a search instead, e.g.  findex -p report\n");
        return 2;
    }
    std::wstring why;
    std::unique_ptr<IndexSource> src = openSource(o, true, why);
    if (!src) {
        // No service and no admin rights: fall back to the old behaviour.
        enableVt(GetStdHandle(STD_ERROR_HANDLE));
        if (service::query() == service::Status::NotInstalled) {
            err(std::wstring(MUTED) + L"Tip: run " + RESET + ACCENT + L"findex --install-service" + RESET + MUTED +
                L" once and findex won't ask for administrator rights again." + RESET + L"\n");
        } else {
            err(std::wstring(WARN) + L"! " + RESET + MUTED + L"Not using the service: " + why + L"." + RESET + L"\n");
        }
        Findex findex;
        findex.elevate(); // relaunches elevated (UAC prompt) and exits
        return 0;
    }
    return tui::run(*src, o.drive);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options o;
    bool exit_now = false;
    if (!parseArgs(argc, argv, o, exit_now)) return 2;
    if (exit_now) return 0;

    enableVt(GetStdHandle(STD_ERROR_HANDLE));
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int rc = 0;

    switch (o.mode) {
    case Options::Mode::RunService: rc = service::runAsService(o.sid, o.drives); break;
    case Options::Mode::InstallService: {
        std::wstring drives = o.drives;
        for (wchar_t c : o.query) // "findex --install-service C D" or "C: D:"
            if (iswalpha(c)) drives += c;
        rc = service::install(drives, o.sid);
        break;
    }
    case Options::Mode::UninstallService: rc = service::uninstall(); break;
    case Options::Mode::ServiceStatus: rc = service::printStatus(); break;
    case Options::Mode::Search: rc = o.query.empty() ? interactive(o) : searchOnce(o); break;
    }

    CoUninitialize();
    return rc;
}
