#include <windows.h>
#include <iostream>
#include <shlobj.h>
#include <string>

#include "appdata.hpp"

std::string getAppDataPath() {
    char path[MAX_PATH];

    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path)))
        return "";

    std::string app_path = std::string(path) + "\\Findex\\";
    if (!CreateDirectoryA(app_path.c_str(), nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return "";
        }
    }

    return app_path;
}