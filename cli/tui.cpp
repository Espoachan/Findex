#include "tui.hpp"
#include "source.hpp"
#include "shell.hpp"
#include "term.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using namespace term;
using Clock = std::chrono::steady_clock;

namespace {

constexpr size_t RESULT_LIMIT = 1000;
const wchar_t* const SPINNER[] = {L"·", L"✢", L"✳", L"✶", L"✻", L"✽",
                                  L"✻", L"✶", L"✳", L"✢"};
constexpr int SPINNER_FRAMES = 10;

// Header (3) + input box (3) + info line (1) above the list, footer (1) below.
constexpr int LIST_TOP = 8;

IndexSource* g_source = nullptr;

BOOL WINAPI onConsoleClose(DWORD type) {
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        // The window is going away: let the indexer save before we're killed.
        if (g_source) g_source->emergencyStop(4000);
        return TRUE;
    }
    return TRUE; // Ctrl+C / Ctrl+Break are handled as keys
}

bool isHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool isLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

std::vector<std::wstring> splitTerms(const std::wstring& folded) {
    std::vector<std::wstring> terms;
    std::wstring cur;
    for (wchar_t c : folded) {
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

// Which characters of `name` are part of a match (for highlighting).
std::vector<bool> matchMask(const std::wstring& name, const std::vector<std::wstring>& folded_terms) {
    std::vector<bool> mask(name.size(), false);
    if (folded_terms.empty()) return mask;
    const std::wstring folded = USNIndexer::fold(name);
    for (const auto& t : folded_terms) {
        for (size_t pos = folded.find(t); pos != std::wstring::npos; pos = folded.find(t, pos + 1)) {
            for (size_t i = 0; i < t.size(); ++i) mask[pos + i] = true;
        }
    }
    return mask;
}

void addHighlighted(Line& line, const std::wstring& shown, const std::vector<bool>& mask, const wchar_t* color) {
    size_t i = 0;
    while (i < shown.size()) {
        const bool hl = i < mask.size() && mask[i];
        size_t j = i;
        while (j < shown.size() && ((j < mask.size() && mask[j]) == hl)) ++j;
        const std::wstring part = shown.substr(i, j - i);
        if (hl) {
            line.style(BOLD);
            line.add(part, MATCH);
        } else {
            line.add(part, color);
        }
        i = j;
    }
}

std::wstring parentOf(const SearchResult& r) {
    const size_t cut = r.path.size() >= r.name.size() + 1 ? r.path.size() - r.name.size() - 1 : 0;
    std::wstring parent = r.path.substr(0, cut);
    if (parent.size() == 2 && parent[1] == L':') parent += L'\\';
    return parent;
}

std::wstring formatSeconds(double s) {
    wchar_t buf[32];
    swprintf(buf, 32, s < 10 ? L"%.1f s" : L"%.0f s", s);
    return buf;
}

std::wstring formatMs(double ms) {
    wchar_t buf[32];
    swprintf(buf, 32, ms < 10 ? L"%.1f ms" : L"%.0f ms", ms);
    return buf;
}

class App {
public:
    App(IndexSource& src, wchar_t drive) : src(src), drive(drive) {}

    int run();

private:
    // --- state ---
    IndexSource& src;
    wchar_t drive;
    HANDLE in = nullptr, out = nullptr;
    DWORD old_in_mode = 0, old_out_mode = 0;

    std::wstring query;
    size_t caret = 0;
    size_t query_scroll = 0;
    SearchFilter filter = SearchFilter::All;

    std::vector<SearchResult> results;
    std::vector<std::wstring> terms; // folded, for highlighting
    size_t total = 0;
    double search_ms = 0;
    int selected = 0;
    int scroll = 0;

    bool pending_refresh = false;
    bool need_search = false;
    bool dirty = true;
    bool quit = false;

    Clock::time_point started = Clock::now();
    Clock::time_point last_search = Clock::now();
    Clock::time_point last_spin = Clock::now();
    double ready_after = -1;
    int spinner = 0;

    std::wstring toast;
    const wchar_t* toast_style = GOOD;
    Clock::time_point toast_until;

    int width = 80, height = 24;
    DWORD last_click = 0;
    int last_click_row = -1;

    // --- helpers ---
    void out_text(const std::wstring& s) {
        DWORD n;
        WriteConsoleW(out, s.data(), static_cast<DWORD>(s.size()), &n, nullptr);
    }
    int listRows() const { return std::max(0, height - LIST_TOP); }
    void setupConsole();
    void restoreConsole();
    bool readSize();
    void handleInput();
    void onKey(const KEY_EVENT_RECORD& k);
    void onMouse(const MOUSE_EVENT_RECORD& m);
    void queryChanged();
    void insert(wchar_t c);
    size_t prevPos(size_t p) const;
    size_t nextPos(size_t p) const;
    size_t wordLeft(size_t p) const;
    size_t wordRight(size_t p) const;
    void moveSelection(int delta);
    void ensureVisible();
    void doSearch(bool keep_selection);
    void openSelected(bool reveal);
    void copySelected();
    void showToast(const std::wstring& msg, const wchar_t* style);
    void render();

    Line headerLine(int inner) const;
    Line inputLine(int inner, int& caret_col);
    Line infoLine(int w) const;
    Line resultLine(int i, int w, int name_w) const;
    Line welcomeLine(int row, int w) const;
    Line footerLine(int w) const;
};

// ---------------------------------------------------------------------------

void App::setupConsole() {
    in = GetStdHandle(STD_INPUT_HANDLE);
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(in, &old_in_mode);
    GetConsoleMode(out, &old_out_mode);
    // No line input / echo / Ctrl+C processing: we get every key ourselves.
    // Disabling quick-edit lets the mouse reach us (wheel, clicks).
    SetConsoleMode(in, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    SetConsoleMode(out, old_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleTitleW(L"Findex");
    out_text(L"\x1b[?1049h\x1b[H\x1b[2J\x1b[6 q");
}

void App::restoreConsole() {
    // Back to the normal screen. Console modes are restored at the very end,
    // after the "saving" message (which still needs VT processing).
    out_text(L"\x1b[0m\x1b[0 q\x1b[?25h\x1b[?1049l");
}

bool App::readSize() {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out, &info)) return false;
    const int w = info.srWindow.Right - info.srWindow.Left + 1;
    const int h = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (w == width && h == height) return false;
    width = w;
    height = h;
    ensureVisible();
    return true;
}

// ---------------------------------------------------------------------------
// Input

void App::handleInput() {
    INPUT_RECORD records[128];
    DWORD available = 0;
    while (GetNumberOfConsoleInputEvents(in, &available) && available > 0) {
        DWORD n = 0;
        if (!ReadConsoleInputW(in, records, 128, &n)) return;
        for (DWORD i = 0; i < n; ++i) {
            switch (records[i].EventType) {
            case KEY_EVENT:
                if (records[i].Event.KeyEvent.bKeyDown) {
                    for (WORD r = 0; r < std::max<WORD>(1, records[i].Event.KeyEvent.wRepeatCount); ++r)
                        onKey(records[i].Event.KeyEvent);
                }
                break;
            case MOUSE_EVENT: onMouse(records[i].Event.MouseEvent); break;
            case WINDOW_BUFFER_SIZE_EVENT: dirty = true; break;
            default: break;
            }
        }
    }
}

void App::onKey(const KEY_EVENT_RECORD& k) {
    const DWORD st = k.dwControlKeyState;
    const bool ctrl = (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool alt = (st & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
    const bool shift = (st & SHIFT_PRESSED) != 0;
    const wchar_t ch = k.uChar.UnicodeChar;
    dirty = true;

    switch (k.wVirtualKeyCode) {
    case VK_UP: moveSelection(-1); return;
    case VK_DOWN: moveSelection(1); return;
    case VK_PRIOR: moveSelection(-std::max(1, listRows() - 1)); return;
    case VK_NEXT: moveSelection(std::max(1, listRows() - 1)); return;
    case VK_LEFT: caret = ctrl ? wordLeft(caret) : prevPos(caret); return;
    case VK_RIGHT: caret = ctrl ? wordRight(caret) : nextPos(caret); return;
    case VK_HOME:
        if (ctrl) moveSelection(-static_cast<int>(results.size()));
        else caret = 0;
        return;
    case VK_END:
        if (ctrl) moveSelection(static_cast<int>(results.size()));
        else caret = query.size();
        return;
    case VK_BACK:
        if (caret > 0) {
            const size_t from = ctrl ? wordLeft(caret) : prevPos(caret);
            query.erase(from, caret - from);
            caret = from;
            queryChanged();
        }
        return;
    case VK_DELETE:
        if (caret < query.size()) {
            query.erase(caret, nextPos(caret) - caret);
            queryChanged();
        }
        return;
    case VK_RETURN: openSelected(ctrl); return;
    case VK_TAB: {
        int f = static_cast<int>(filter) + (shift ? 2 : 1);
        filter = static_cast<SearchFilter>(f % 3);
        need_search = true;
        selected = scroll = 0;
        return;
    }
    case VK_ESCAPE:
        if (query.empty()) quit = true;
        else {
            query.clear();
            caret = 0;
            queryChanged();
        }
        return;
    default: break;
    }

    if (ctrl && !alt) { // AltGr (ctrl+alt) is used to type characters like \ or @
        switch (k.wVirtualKeyCode) {
        case 'C':
        case 'D': quit = true; return;
        case 'O': openSelected(true); return;
        case 'Y': copySelected(); return;
        case 'U':
            query.erase(0, caret);
            caret = 0;
            queryChanged();
            return;
        case 'W':
            if (caret > 0) {
                size_t from = wordLeft(caret);
                query.erase(from, caret - from);
                caret = from;
                queryChanged();
            }
            return;
        case 'A': caret = 0; return;
        case 'E': caret = query.size(); return;
        case 'P': moveSelection(-1); return;
        case 'N': moveSelection(1); return;
        case 'L': out_text(L"\x1b[2J"); return;
        default: return;
        }
    }

    if (ch >= 0x20 && ch != 0x7F) insert(ch);
}

void App::onMouse(const MOUSE_EVENT_RECORD& m) {
    if (m.dwEventFlags == MOUSE_WHEELED) {
        const short delta = static_cast<short>(HIWORD(m.dwButtonState));
        moveSelection(delta > 0 ? -3 : 3);
        dirty = true;
        return;
    }
    const bool left = (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
    if (!left || (m.dwEventFlags != 0 && m.dwEventFlags != DOUBLE_CLICK)) return;

    const int row = m.dwMousePosition.Y + 1; // 1-based screen row
    if (row < LIST_TOP || row >= LIST_TOP + listRows()) return;
    const int index = scroll + (row - LIST_TOP);
    if (index >= static_cast<int>(results.size())) return;

    selected = index;
    dirty = true;
    const DWORD now = GetTickCount();
    if (m.dwEventFlags == DOUBLE_CLICK || (row == last_click_row && now - last_click < GetDoubleClickTime())) {
        openSelected(false);
        last_click_row = -1;
    } else {
        last_click_row = row;
        last_click = now;
    }
}

void App::queryChanged() {
    need_search = true;
    selected = 0;
    scroll = 0;
}

void App::insert(wchar_t c) {
    query.insert(caret, 1, c);
    ++caret;
    queryChanged();
}

size_t App::prevPos(size_t p) const {
    if (p == 0) return 0;
    --p;
    if (p > 0 && isLowSurrogate(query[p]) && isHighSurrogate(query[p - 1])) --p;
    return p;
}

size_t App::nextPos(size_t p) const {
    if (p >= query.size()) return query.size();
    ++p;
    if (p < query.size() && isLowSurrogate(query[p])) ++p;
    return p;
}

size_t App::wordLeft(size_t p) const {
    while (p > 0 && query[p - 1] == L' ') --p;
    while (p > 0 && query[p - 1] != L' ') --p;
    return p;
}

size_t App::wordRight(size_t p) const {
    while (p < query.size() && query[p] == L' ') ++p;
    while (p < query.size() && query[p] != L' ') ++p;
    return p;
}

void App::moveSelection(int delta) {
    if (results.empty()) return;
    selected = std::clamp(selected + delta, 0, static_cast<int>(results.size()) - 1);
    ensureVisible();
}

void App::ensureVisible() {
    const int rows = listRows();
    if (rows <= 0) return;
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + rows) scroll = selected - rows + 1;
    scroll = std::max(0, std::min(scroll, static_cast<int>(results.size()) - rows));
}

// ---------------------------------------------------------------------------
// Actions

void App::doSearch(bool keep_selection) {
    last_search = Clock::now();
    if (query.find_first_not_of(L" \t") == std::wstring::npos) {
        results.clear();
        terms.clear();
        total = 0;
        selected = scroll = 0;
        return;
    }

    uint64_t keep_frn = 0;
    if (keep_selection && selected < static_cast<int>(results.size())) keep_frn = results[selected].frn;

    SearchOptions opts;
    opts.limit = RESULT_LIMIT;
    opts.filter = filter;
    const auto t0 = Clock::now();
    results = src.search(query, opts, &total);
    search_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    terms = splitTerms(USNIndexer::fold(query));

    if (keep_frn) {
        for (size_t i = 0; i < results.size(); ++i) {
            if (results[i].frn == keep_frn) {
                selected = static_cast<int>(i);
                break;
            }
        }
    }
    selected = std::min(selected, std::max(0, static_cast<int>(results.size()) - 1));
    ensureVisible();
}

void App::openSelected(bool reveal) {
    if (selected >= static_cast<int>(results.size())) return;
    const SearchResult& r = results[selected];
    const bool ok = reveal ? shell::reveal(r.path) : shell::open(r.path);
    if (ok) showToast((reveal ? L"Shown in Explorer  " : L"Opened  ") + r.name, GOOD);
    else showToast(L"Could not open " + r.path, BAD);
}

void App::copySelected() {
    if (selected >= static_cast<int>(results.size())) return;
    const SearchResult& r = results[selected];
    if (shell::copyToClipboard(r.path)) showToast(L"Copied  " + r.path, GOOD);
    else showToast(L"Could not access the clipboard", BAD);
}

void App::showToast(const std::wstring& msg, const wchar_t* style) {
    toast = msg;
    toast_style = style;
    toast_until = Clock::now() + std::chrono::milliseconds(2500);
    dirty = true;
}

// ---------------------------------------------------------------------------
// Drawing

Line App::headerLine(int inner) const {
    Line left;
    left.style(BOLD).add(L"✻ ", ACCENT);
    left.style(BOLD).add(L"Findex");
    left.add(L"   ");

    const IndexState s = src.state();
    const std::wstring count = formatCount(src.fileCount());
    switch (s) {
    case IndexState::Idle:
    case IndexState::Loading:
        left.add(SPINNER[spinner % SPINNER_FRAMES], WARN).add(L" loading index…", MUTED);
        break;
    case IndexState::Scanning:
        left.add(SPINNER[spinner % SPINNER_FRAMES], WARN).add(L" reading MFT… ", MUTED).add(count, WARN);
        break;
    case IndexState::Ready:
        left.add(L"●", GOOD).add(L" live", MUTED);
        if (!src.label().empty()) left.add(L" · " + src.label(), SUBTLE);
        if (ready_after >= 0) left.add(L" · ready in " + formatSeconds(ready_after), SUBTLE);
        break;
    case IndexState::Error: {
        const std::wstring msg = src.lastError();
        left.add(L"✖ ", BAD).add(truncateEnd(msg, std::max(0, inner - left.width - 4)), BAD);
        break;
    }
    }

    Line right;
    if (s != IndexState::Error) {
        right.add(std::wstring(1, drive) + L":", BOLD).add(L"  ").add(count).add(L" objects", MUTED);
    }

    Line line;
    line.text = left.text;
    line.width = left.width;
    if (left.width + right.width + 2 <= inner) {
        line.pad(inner - right.width);
        line.text += right.text;
        line.width += right.width;
    }
    return line.pad(inner);
}

Line App::inputLine(int inner, int& caret_col) {
    Line tag;
    switch (filter) {
    case SearchFilter::All: tag.add(L"all", SUBTLE); break;
    case SearchFilter::Files: tag.add(L"files only", ACCENT); break;
    case SearchFilter::Folders: tag.add(L"folders only", ACCENT); break;
    }

    Line line;
    line.style(BOLD).add(L"❯ ", ACCENT);
    const int avail = std::max(1, inner - line.width - tag.width - 2);

    // Horizontal scrolling keeps the caret visible in long queries.
    if (caret < query_scroll) query_scroll = caret;
    while (query_scroll < caret && displayWidth(query.substr(query_scroll, caret - query_scroll)) > avail - 1)
        query_scroll = nextPos(query_scroll);

    int text_w = 0;
    if (query.empty()) {
        line.add(truncateEnd(L"Search files and folders…", avail), SUBTLE);
    } else {
        std::wstring visible;
        for (size_t i = query_scroll; i < query.size(); ++i) {
            const int cw = charWidth(query[i]);
            if (text_w + cw > avail) break;
            visible += query[i];
            text_w += cw;
        }
        line.add(visible);
    }
    // Box border (col 1) + padding (col 2) + "❯ " (cols 3-4).
    caret_col = 5 + displayWidth(query.substr(query_scroll, caret - query_scroll));

    line.pad(inner - tag.width);
    line.text += tag.text;
    line.width += tag.width;
    return line;
}

Line App::infoLine(int w) const {
    Line line;
    line.add(L"  ");
    if (query.find_first_not_of(L" \t") == std::wstring::npos) return line;

    line.add(formatCount(total), BOLD).add(total == 1 ? L" match" : L" matches", MUTED);
    line.add(L" · " + formatMs(search_ms), SUBTLE);
    if (total > results.size()) line.add(L" · showing the best " + formatCount(results.size()), SUBTLE);

    if (!results.empty()) {
        const std::wstring pos = formatCount(selected + 1) + L"/" + formatCount(results.size());
        const int pos_w = displayWidth(pos) + 2;
        if (line.width + pos_w + 2 <= w) {
            line.pad(w - pos_w);
            line.add(pos, SUBTLE);
        }
    }
    return line;
}

Line App::resultLine(int i, int w, int name_w) const {
    const SearchResult& r = results[i];
    const bool sel = (i == selected);
    Line line(sel ? SELECTED_BG : L"");

    if (sel) line.style(BOLD).add(L" ❯ ", ACCENT);
    else line.add(L"   ");

    std::wstring shown = r.name + (r.is_directory ? L"\\" : L"");
    shown = truncateEnd(shown, name_w - 2);
    const bool cut = !shown.empty() && shown.back() == L'…' && shown != r.name;
    std::vector<bool> mask = matchMask(r.name, terms);
    if (cut) mask.resize(shown.size() - 1);
    addHighlighted(line, shown, mask, r.is_directory ? FOLDER : nullptr);

    line.pad(3 + name_w);
    const int path_w = w - line.width - 1;
    if (path_w > 4) line.add(truncateStart(parentOf(r), path_w), MUTED);
    line.pad(w);
    return line;
}

Line App::welcomeLine(int row, int w) const {
    Line line;
    const IndexState s = src.state();
    auto tip = [&](const wchar_t* key, const wchar_t* text) {
        line.add(L"     ");
        line.add(key, ACCENT);
        line.pad(25);
        line.add(text, MUTED);
    };
    switch (row) {
    case 1:
        line.add(L"   ");
        if (isBusy(s)) {
            line.add(L"Building the index… you can start typing, results update live.", MUTED);
        } else if (s == IndexState::Error) {
            line.add(L"The index could not be built. Check the message above.", BAD);
        } else {
            line.add(L"Type to search ", MUTED).add(formatCount(src.fileCount()), BOLD);
            line.add(L" files and folders on ", MUTED).add(std::wstring(1, drive) + L":", BOLD);
        }
        break;
    case 3: line.add(L"   ").add(L"Tips", BOLD); break;
    case 4: tip(L"report 2024", L"several words: all of them must appear"); break;
    case 5: tip(L"tab", L"switch between all · files · folders"); break;
    case 6: tip(L"enter / double click", L"open with its default program"); break;
    case 7: tip(L"ctrl+o", L"show it in Explorer"); break;
    case 8: tip(L"ctrl+y", L"copy the full path"); break;
    case 9: tip(L"esc", L"clear the search · press again to quit"); break;
    default: break;
    }
    (void)w;
    return line;
}

Line App::footerLine(int w) const {
    Line line;
    line.add(L"  ");
    if (!toast.empty()) {
        line.add(L"✔ ", toast_style == BAD ? BAD : GOOD);
        line.add(truncateEnd(toast, w - 6), toast_style);
        return line;
    }
    const wchar_t* sep = L"  ·  ";
    struct Hint { const wchar_t* key; const wchar_t* what; };
    const Hint hints[] = {{L"↑↓", L"move"},  {L"enter", L"open"},  {L"ctrl+o", L"folder"},
                          {L"ctrl+y", L"copy path"}, {L"tab", L"filter"}, {L"esc", L"quit"}};
    bool first = true;
    for (const auto& h : hints) {
        const int need = displayWidth(h.key) + displayWidth(h.what) + 1 + (first ? 0 : 5);
        if (line.width + need > w) break;
        if (!first) line.add(sep, SUBTLE);
        line.add(h.key, MUTED).add(L" ").add(h.what, SUBTLE);
        first = false;
    }
    return line;
}

void App::render() {
    const int w = width - 1; // never write the last column: avoids wrapping/scrolling
    std::wstring frame = L"\x1b[?2026h\x1b[?25l";
    auto put = [&](int row, const std::wstring& text) {
        frame += L"\x1b[" + std::to_wstring(row) + L";1H" + text + RESET + L"\x1b[K";
    };

    if (w < 40 || height < LIST_TOP + 3) {
        frame += L"\x1b[2J";
        put(1, std::wstring(MUTED) + L"Findex: make the window a bit bigger");
        frame += L"\x1b[?2026l";
        out_text(frame);
        return;
    }

    const int inner = w - 4; // "│ " + content + " │"
    auto boxTop = [&] { return std::wstring(SUBTLE) + L"╭" + std::wstring(w - 2, L'─') + L"╮"; };
    auto boxBottom = [&] { return std::wstring(SUBTLE) + L"╰" + std::wstring(w - 2, L'─') + L"╯"; };
    auto boxed = [&](const Line& l) {
        return std::wstring(SUBTLE) + L"│ " + RESET + l.text + RESET + L" " + SUBTLE + L"│";
    };

    put(1, boxTop());
    put(2, boxed(headerLine(inner)));
    put(3, boxBottom());

    int caret_col = 5;
    const Line input = inputLine(inner, caret_col);
    put(4, boxTop());
    put(5, boxed(input));
    put(6, boxBottom());

    put(7, infoLine(w).text);

    const int rows = listRows() - 1; // the last row is the footer
    const bool empty_query = query.find_first_not_of(L" \t") == std::wstring::npos;

    // Name column: as wide as the longest visible name, within limits.
    int name_w = 18;
    for (int r = 0; r < rows && scroll + r < static_cast<int>(results.size()); ++r) {
        const auto& res = results[scroll + r];
        name_w = std::max(name_w, displayWidth(res.name) + (res.is_directory ? 1 : 0) + 3);
    }
    name_w = std::min(name_w, std::max(18, (w - 3) * 45 / 100));

    for (int r = 0; r < rows; ++r) {
        const int i = scroll + r;
        if (empty_query) {
            put(LIST_TOP + r, welcomeLine(r, w).text);
        } else if (i < static_cast<int>(results.size())) {
            put(LIST_TOP + r, resultLine(i, w, name_w).text);
        } else if (results.empty() && r == 1) {
            put(LIST_TOP + r, std::wstring(L"   ") + MUTED + (isBusy(src.state())
                                                                 ? L"No matches yet, still indexing…"
                                                                 : L"No matches"));
        } else {
            put(LIST_TOP + r, L"");
        }
    }

    put(height, footerLine(w).text);

    frame += L"\x1b[5;" + std::to_wstring(caret_col) + L"H\x1b[?25h\x1b[?2026l";
    out_text(frame);
}

// ---------------------------------------------------------------------------

int App::run() {
    setupConsole();
    readSize();

    g_source = &src;
    SetConsoleCtrlHandler(onConsoleClose, TRUE);

    uint64_t last_gen = src.generation();
    while (!quit) {
        if (WaitForSingleObject(in, 50) == WAIT_OBJECT_0) handleInput();

        const auto now = Clock::now();
        src.poll();
        const uint64_t gen = src.generation();
        if (gen != last_gen) {
            last_gen = gen;
            pending_refresh = true;
            dirty = true;
        }
        if (ready_after < 0 && src.state() == IndexState::Ready) {
            ready_after = std::chrono::duration<double>(now - started).count();
            dirty = true;
        }
        if (pending_refresh && now - last_search >= std::chrono::milliseconds(300)) {
            pending_refresh = false;
            if (!query.empty()) {
                doSearch(true);
                dirty = true;
            }
        }
        if (need_search) {
            need_search = false;
            doSearch(false);
            dirty = true;
        }
        if (isBusy(src.state()) && now - last_spin >= std::chrono::milliseconds(100)) {
            last_spin = now;
            ++spinner;
            dirty = true;
        }
        if (!toast.empty() && now >= toast_until) {
            toast.clear();
            dirty = true;
        }
        if (readSize()) dirty = true;

        if (dirty) {
            dirty = false;
            render();
        }
    }

    restoreConsole();

    const bool saving = src.savesOnClose();
    if (saving) term::out(std::wstring(MUTED) + L"Saving index…" + RESET + L"");
    src.close();
    if (saving) term::out(L"[K");

    SetConsoleMode(in, old_in_mode);
    SetConsoleMode(out, old_out_mode);
    SetConsoleCtrlHandler(onConsoleClose, FALSE);
    g_source = nullptr;
    return 0;
}

} // namespace

namespace tui {

int run(IndexSource& source, wchar_t drive) {
    App app(source, drive);
    return app.run();
}

} // namespace tui
