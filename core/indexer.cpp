#include "indexer.h"
#include <iostream>
#include <string>

USNIndexer::USNIndexer() : hVolume(INVALID_HANDLE_VALUE), driveLetter('C') {}

USNIndexer::~USNIndexer() {
    if (hVolume != INVALID_HANDLE_VALUE) {
        CloseHandle(hVolume);
    }
}

bool USNIndexer::initVolume(char drive) {
    this->driveLetter = drive;
    std::string path = "\\\\.\\";
    path += drive;
    path += ":";

    // OPEN THE VOLUME
    // We need GENERIC_READ | GENERIC_WRITE to send I/O control codes to the FS
    hVolume = CreateFileA(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hVolume == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open volume " << drive << ". Error: " << GetLastError() << std::endl;
        std::cerr << "Make sure you are running as ADMINISTRATOR." << std::endl;
        return false;
    }

    return true;
}

bool USNIndexer::createUSNJournal() {
    // TODO: Implement FSCTL_CREATE_USN_JOURNAL
    return true; 
}

bool USNIndexer::getJournalData(USN_JOURNAL_DATA& data) {
    if (hVolume == INVALID_HANDLE_VALUE) return false;

    DWORD bytesReturned;
    if (!DeviceIoControl(
        hVolume,
        FSCTL_QUERY_USN_JOURNAL,
        NULL,
        0,
        &data,
        sizeof(data),
        &bytesReturned,
        NULL
    )) {
        std::cerr << "Failed to query USN Journal. Error: " << GetLastError() << std::endl;
        return false;
    }
    return true;
}

std::vector<FileRecord> USNIndexer::getAllFiles() {
    std::vector<FileRecord> files;
    // TODO: Implement FSCTL_ENUM_USN_DATA
    return files;
}