#include "term.hpp"

#include <algorithm>

namespace term {

bool isConsole(HANDLE h) {
    DWORD mode;
    return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
}

int charWidth(wchar_t c) {
    if (c < 0x20) return 0;
    if (c >= 0x0300 && c <= 0x036F) return 0;  // combining marks
    if (c >= 0x200B && c <= 0x200F) return 0;  // zero width
    if (c >= 0xDC00 && c <= 0xDFFF) return 0;  // low surrogate (counted with the high one)
    if (c >= 0xD800 && c <= 0xDBFF) return 2;  // astral plane: mostly emoji / CJK ext
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
        (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
        (c >= 0xFFE0 && c <= 0xFFE6))
        return 2;
    return 1;
}

int displayWidth(const std::wstring& s) {
    int w = 0;
    for (wchar_t c : s) w += charWidth(c);
    return w;
}

std::wstring truncateEnd(const std::wstring& s, int width) {
    if (width <= 0) return L"";
    if (displayWidth(s) <= width) return s;
    std::wstring r;
    int w = 0;
    for (wchar_t c : s) {
        int cw = charWidth(c);
        if (w + cw > width - 1) break;
        r += c;
        w += cw;
    }
    if (!r.empty() && r.back() >= 0xD800 && r.back() <= 0xDBFF) r.pop_back(); // don't split a pair
    return r + L"…";
}

std::wstring truncateStart(const std::wstring& s, int width) {
    if (width <= 0) return L"";
    if (displayWidth(s) <= width) return s;
    std::wstring r;
    int w = 0;
    for (size_t i = s.size(); i-- > 0;) {
        int cw = charWidth(s[i]);
        if (w + cw > width - 1) break;
        r += s[i];
        w += cw;
    }
    if (!r.empty() && r.back() >= 0xDC00 && r.back() <= 0xDFFF) r.pop_back();
    std::reverse(r.begin(), r.end());
    return L"…" + r;
}

std::wstring formatCount(size_t n) {
    std::wstring digits = std::to_wstring(n);
    std::wstring r;
    int count = 0;
    for (size_t i = digits.size(); i-- > 0;) {
        r += digits[i];
        if (++count % 3 == 0 && i > 0) r += L',';
    }
    std::reverse(r.begin(), r.end());
    return r;
}

static void writeTo(DWORD which, const std::wstring& s) {
    HANDLE h = GetStdHandle(which);
    if (isConsole(h)) {
        DWORD written;
        WriteConsoleW(h, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
        return;
    }
    if (s.empty()) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), utf8.data(), len, nullptr, nullptr);
    DWORD written;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void out(const std::wstring& s) { writeTo(STD_OUTPUT_HANDLE, s); }
void err(const std::wstring& s) { writeTo(STD_ERROR_HANDLE, s); }

Line& Line::add(const std::wstring& s, const wchar_t* st) {
    if (st) text += st;
    text += s;
    if (st) {
        text += RESET;
        text += base;
    }
    width += displayWidth(s);
    return *this;
}

} // namespace term
