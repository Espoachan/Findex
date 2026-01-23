#include <windows.h>
#include <iostream>
#include <shlobj.h>
#include "include/appdatapath.h"
#include <string>

std::string getAppDataPath() {
    char path[MAX_PATH];

    if (FAILED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path)))
        return "";

    std::string app_path = std::string(path) + "\\Findex\\";
    if (!CreateDirectoryA(app_path.c_str(), NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return "";
        }
    }

    return app_path;
}