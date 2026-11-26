#include "findex.hpp"
#include "elevate.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <cstdlib>

void Findex::elevate() {
    if (isAdministrator()) return;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    // Relaunch with the same arguments.
    const wchar_t* args = PathGetArgsW(GetCommandLineW());
    ShellExecuteW(nullptr, L"runas", path, (args && *args) ? args : nullptr, nullptr, SW_SHOW);
    std::exit(0);
}
