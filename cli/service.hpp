#pragma once

// The Findex Windows service: keeps the index of the configured drives up to
// date as LocalSystem and answers searches over a local named pipe, so the
// findex client never needs administrator rights.

#include <windows.h>

#include <string>

namespace service {

inline const wchar_t* const NAME = L"Findex";
inline const wchar_t* const DISPLAY_NAME = L"Findex Indexer";

enum class Status { NotInstalled, Stopped, Starting, Running, Stopping, Unknown };

// Current status; `pid` receives the service process id when running.
Status query(DWORD* pid = nullptr);

// Entry point when started by the Service Control Manager
// (findex.exe --service --user <SID> --drives <letters>).
int runAsService(const std::wstring& allowed_sid, const std::wstring& drives);

// Install / update and start the service for `drives` (e.g. L"CD").
// Self-elevates (one UAC prompt) when not running as administrator.
int install(const std::wstring& drives, const std::wstring& for_user_sid);
int uninstall();
int printStatus();

// SID of the user running this process, as a string.
std::wstring currentUserSid();

} // namespace service
