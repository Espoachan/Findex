#include "indexer.h"
#include <iostream>
#include <string>
#include <memory>
#include "appdatapath.h"

USNIndexer::USNIndexer() : hVolume(INVALID_HANDLE_VALUE), drive_letter('C') {}

USNIndexer::~USNIndexer() {
    if (hVolume != INVALID_HANDLE_VALUE) {
        CloseHandle(hVolume);
    }
}

bool USNIndexer::initVolume(char drive) {
    this->drive_letter = drive;
    std::string path = "\\\\.\\";
    path += drive;
    path += ":";

    hVolume = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hVolume == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open volume " << drive << ". Error: " << GetLastError() << std::endl;
        std::cerr << "Make sure you are running as ADMINISTRATOR." << std::endl;
        return false;
    }

    return true;
}

bool USNIndexer::createUSNJournal() {
    USN_JOURNAL_DATA journalData = {};
    DWORD bytes_returned = 0;
    BOOL journal_exists = DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData, sizeof(journalData), &bytes_returned, NULL);

    if(journal_exists) {
        return true;
    }

    DWORD err = GetLastError();
    if (err != ERROR_JOURNAL_NOT_ACTIVE) {
        return false;
    }   

    CREATE_USN_JOURNAL_DATA journal_data = {};
    journal_data.MaximumSize = 0;
    journal_data.AllocationDelta = 0;

    journal_exists = DeviceIoControl(hVolume, FSCTL_CREATE_USN_JOURNAL, &journal_data, sizeof(journal_data), nullptr, 0, &bytes_returned, nullptr);

    journal_info.journal_id = journalData.UsnJournalID;
    journal_info.next_usn = journalData.NextUsn;

    saveJournalInfo(journal_info);
}

bool USNIndexer::saveJournalInfo(const UsnJournalInfo& info) {
    std::string path = getAppDataPath();
    path += "journal.dat";
    FILE* datfile = fopen(path.c_str(), "wb");
    if (!datfile) return false;

    size_t written = fwrite(&info, sizeof(UsnJournalInfo), 1, datfile);
    fclose(datfile);

    return (written == 1);
}

bool USNIndexer::getJournalData(USN_JOURNAL_DATA& data) {
    if (hVolume == INVALID_HANDLE_VALUE) return false;

    DWORD bytes_returned;
    if (!DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &data, sizeof(data), &bytes_returned, nullptr)) {
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