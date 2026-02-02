#include <iostream>
#include <string>
#include <memory>

#include "indexer.hpp"
#include "appdata.hpp"

USNIndexer::USNIndexer() : hVolume(INVALID_HANDLE_VALUE)/*, drive_letter('C')*/ {}

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

    hVolume = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hVolume == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open volume " << drive << ". Error: " << GetLastError() << std::endl;
        std::cerr << "Make sure you are running as ADMINISTRATOR." << std::endl;
        return false;
    }

    return true;
}

bool USNIndexer::createUSNJournal() {
    USN_JOURNAL_DATA journal_data = {};
    DWORD bytes_returned = 0;
    BOOL journal_exists = DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &bytes_returned, NULL);

    if(journal_exists) {
        journal_info.journal_id = journal_data.UsnJournalID;
        journal_info.next_usn = journal_data.NextUsn;
        // saveJournalInfo(journal_info);
        return true;
    }

    DWORD err = GetLastError();
    if (err != ERROR_JOURNAL_NOT_ACTIVE) {
        return false;
    }   

    CREATE_USN_JOURNAL_DATA journal_config = {};
    journal_config.MaximumSize = 0;
    journal_config.AllocationDelta = 0;

    journal_exists = DeviceIoControl(hVolume, FSCTL_CREATE_USN_JOURNAL, &journal_config, sizeof(journal_config), nullptr, 0, &bytes_returned, nullptr);

    if(!journal_exists) {
        std::cerr << "Failed to create USN journal. Error: " << GetLastError() << std::endl;
        return false;
    }

    if(!DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_config, sizeof(journal_data), &bytes_returned, nullptr)) {
        std::cerr << "Failed to query USN journal. Error: " << GetLastError() << std::endl;
        return false;
    }

    journal_info.journal_id = journal_data.UsnJournalID;
    journal_info.next_usn = journal_data.NextUsn;

    return true;
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

bool USNIndexer::loadJournalInfo(UsnJournalInfo& info) {
    std::string path = getAppDataPath();
    path += "journal.dat";
    FILE* datfile = fopen(path.c_str(), "rb");
    if (!datfile) return false;

    size_t read = fread(&info, sizeof(UsnJournalInfo), 1, datfile);
    fclose(datfile);

    return (read == 1);
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

FileRecord USNIndexer::createFileRecordFromUSNRecord(USN_RECORD* record) {
    FileRecord file = {};
    file.frn = record->FileReferenceNumber;
    file.parent_frn = record->ParentFileReferenceNumber;

    file.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
    file.is_directory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    return file;
}

void USNIndexer::indexFiles() {    
    std::cout << "this is working fine :) \n"; // debug

    if (hVolume == INVALID_HANDLE_VALUE) return;

    USN_JOURNAL_DATA journal_config{};
    if(!getJournalData(journal_config)) return;

    journal_info.journal_id = journal_config.UsnJournalID;
    journal_info.next_usn   = journal_config.NextUsn;

    MFT_ENUM_DATA_V0 enum_data = {};
    enum_data.StartFileReferenceNumber = 0;
    enum_data.LowUsn = 0;
    enum_data.HighUsn = journal_info.next_usn;

    DWORD bytes_returned = 0;
    const DWORD BUFFER_SIZE = 1024 * 1024; // (this is 1 mega byte)
    auto buffer = std::unique_ptr<BYTE[]>(new BYTE[BUFFER_SIZE]);

    while (true) {
        BOOL success = DeviceIoControl(hVolume, FSCTL_ENUM_USN_DATA, &enum_data, sizeof(enum_data), buffer.get(), BUFFER_SIZE, &bytes_returned, nullptr); 

        if (!success) {
            DWORD err = GetLastError();
            if (err == ERROR_HANDLE_EOF) {
                break;
            } else {
                std::cerr << "FSCTL_ENUM_USN_DATA failed. Error: " << err << std::endl;
                break;
            }
        }

        // FRN stands for File Reference Number
        USN nextFRN = *(USN*)buffer.get();
        DWORD offset = sizeof(USN);

        while (offset < bytes_returned) {
            USN_RECORD* record = (USN_RECORD*)((BYTE*)buffer.get() + offset);

            FileRecord file = {};
            file.frn = record->FileReferenceNumber;
            file.parent_frn = record->ParentFileReferenceNumber;

            file.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
            file.is_directory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            index_map[file.frn] = file;

            offset += record->RecordLength;
        }

        enum_data.StartFileReferenceNumber = nextFRN;
    }
    std::cout << "Indexed " << index_map.size() << " files/folders\n";
}

void USNIndexer::incrementalIndex(USN old_usn) {
    const DWORD BUFFER_SIZE = 1024 * 1024;
    auto buffer = std::unique_ptr<BYTE[]>(new BYTE[BUFFER_SIZE]);
    
    READ_USN_JOURNAL_DATA_V0 read_data = {};
    read_data.StartUsn = old_usn;
    read_data.ReasonMask = 0xFFFFFFFF;
    read_data.ReturnOnlyOnClose = FALSE;
    read_data.Timeout = 0;
    read_data.BytesToWaitFor = 0;
    read_data.UsnJournalID = journal_info.journal_id;

    DWORD bytes_returned = 0;

    while (true) {
        BOOL success = DeviceIoControl(hVolume, FSCTL_READ_USN_JOURNAL, &read_data, sizeof(read_data), buffer.get(), BUFFER_SIZE, &bytes_returned, nullptr);
        if (!success) {
            DWORD err = GetLastError();
            if (err == ERROR_HANDLE_EOF) break;
            
            std::cerr << "FSCTL_READ_USN_JOURNAL failed: " << err << "\n";
            break;
        }
    
        DWORD offset = sizeof(USN);
        while (offset < bytes_returned) {
            USN_RECORD* record = (USN_RECORD*)((BYTE*)buffer.get() + offset);
            updateIndexAfterNewData(record);
            offset += record->RecordLength;
        }

        read_data.StartUsn = *(USN*)buffer.get();
    }
}

void USNIndexer::updateIndexAfterNewData(USN_RECORD* record) {
    USN reason = record->Reason;
    if (!(record->Reason & USN_REASON_CLOSE)) return;
    if (reason & USN_REASON_FILE_DELETE) {
        index_map.erase(record->FileReferenceNumber);

    } else if (reason & USN_REASON_FILE_CREATE) {
        FileRecord file = createFileRecordFromUSNRecord(record);
        index_map[file.frn] = file;
        // std::wcout << file.name << "\n"; // debug
    } else if (reason & USN_REASON_RENAME_NEW_NAME) {
        auto it = index_map.find(record->FileReferenceNumber);
        if (it != index_map.end()) {
            it->second.name = std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR));
            it->second.parent_frn = record->ParentFileReferenceNumber;
        } else {
            FileRecord file = createFileRecordFromUSNRecord(record);
            index_map[file.frn] = file; 
        }
    }
}