#pragma once

#include <string>

namespace shell {

// Opens a file/folder with its default program. Findex runs elevated, so this
// goes through Explorer to launch it with normal (non-admin) rights; falls back
// to a plain ShellExecute if Explorer isn't available.
bool open(const std::wstring& path);

// Opens Explorer with the item selected.
bool reveal(const std::wstring& path);

bool copyToClipboard(const std::wstring& text);

} // namespace shell
