#pragma once

#include <vector>
#include <string>
#include <windows.h>
#include <winioctl.h>
#include <unordered_map>

struct FileRecord {
    uint64_t frn; // id is the file reference number
    uint64_t parent_frn;
    std::wstring name;
    bool is_directory;
};

#pragma pack(push, 1)
struct UsnJournalInfo {
    DWORDLONG journal_id;
    USN next_usn;
};
#pragma pack(pop)

class USNIndexer {
public:
    USNIndexer();
    ~USNIndexer();

    HANDLE hVolume;
    char drive_letter;
    USN_JOURNAL_DATA data{};
    UsnJournalInfo journal_info{};

    bool saveJournalInfo(const UsnJournalInfo& info);
    bool loadJournalInfo(UsnJournalInfo& info);

    void incrementalIndex(USN old_usn);
    std::wstring updateIndexAfterNewData(USN_RECORD* record);
    FileRecord createFileRecordFromUSNRecord(USN_RECORD* record);

    // testing things
    std::wstring file_name_wstring;
    std::wstring error_text_wstring = L"ERROR";


    bool initVolume(char drive_letter);
    bool createUSNJournal();
    bool getJournalData(USN_JOURNAL_DATA& data);

    void indexFiles();

    std::unordered_map<uint64_t, FileRecord> index_map;

private:    
};