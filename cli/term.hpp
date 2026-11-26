#pragma once

// Small helpers for drawing on the Windows console with VT sequences.

#include <windows.h>

#include <string>

namespace term {

// Styles (24-bit colour, supported by Windows Terminal and conhost on Win10+).
// Purple palette.
inline const wchar_t* const RESET = L"[0m";
inline const wchar_t* const BOLD = L"[1m";
inline const wchar_t* const DIM = L"[2m";
inline const wchar_t* const ACCENT = L"[38;2;167;139;250m";      // violet: logo, prompt, selection
inline const wchar_t* const MATCH = L"[38;2;240;171;252m";       // fuchsia: matched text
inline const wchar_t* const FOLDER = L"[38;2;196;181;253m";      // lavender: folder names
inline const wchar_t* const MUTED = L"[38;2;139;133;160m";       // purple-gray
inline const wchar_t* const SUBTLE = L"[38;2;91;84;112m";        // darker purple-gray (borders)
inline const wchar_t* const GOOD = L"[38;2;134;239;172m";
inline const wchar_t* const WARN = L"[38;2;253;224;138m";
inline const wchar_t* const BAD = L"[38;2;251;113;133m";
inline const wchar_t* const SELECTED_BG = L"[48;2;46;38;64m";    // dark purple row

bool isConsole(HANDLE h);

// Number of terminal cells a character / string takes (0, 1 or 2).
int charWidth(wchar_t c);
int displayWidth(const std::wstring& s);

// Cut to `width` cells, marking the cut with an ellipsis.
std::wstring truncateEnd(const std::wstring& s, int width);   // "long name…"
std::wstring truncateStart(const std::wstring& s, int width); // "…\long\path"

std::wstring formatCount(size_t n); // 1234567 -> "1,234,567"

// Writes to stdout: WriteConsoleW on a console, UTF-8 otherwise (pipes, files).
void out(const std::wstring& s);
void err(const std::wstring& s);

// Text with styles whose visible width is tracked, to pad and align columns.
struct Line {
    std::wstring text;
    int width = 0;
    std::wstring base; // re-applied after every styled segment (e.g. a row background)

    Line() = default;
    explicit Line(const wchar_t* base_style) : text(base_style), base(base_style) {}

    Line& add(const std::wstring& s, const wchar_t* style = nullptr);
    Line& style(const wchar_t* s) { text += s; return *this; }
    Line& pad(int target) {
        if (width < target) { text.append(static_cast<size_t>(target - width), L' '); width = target; }
        return *this;
    }
};

} // namespace term
