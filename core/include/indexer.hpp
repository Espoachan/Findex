#pragma once

#include <vector>
#include <string>
#include <windows.h>
#include <winioctl.h>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <filesystem>

#include "wstring_to_utf8.hpp"

struct FileRecord {
    uint64_t frn; // id is the file reference number
    uint64_t parent_frn;
    std::string name;
    bool is_directory;
    std::string path;
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

    std::mutex dataMutex;

    std::string buildFilePath(FileRecord& file, std::unordered_map<uint64_t, FileRecord>& index_map);
    std::string resolvePathByFRN(uint64_t frn);

    bool saveJournalInfo(const UsnJournalInfo& info);
    bool loadJournalInfo(UsnJournalInfo& info);
    void incrementalIndex(USN old_usn);
    void updateIndexAfterNewData(USN_RECORD* record);
    FileRecord createFileRecordFromUSNRecord(USN_RECORD* record);

    bool initVolume(char drive_letter);
    bool createUSNJournal();
    bool getJournalData(USN_JOURNAL_DATA& data);

    void indexFiles();

    std::unordered_map<uint64_t, FileRecord> index_map;

    std::function<void(uint64_t)> onFileRemoved;

private:    
};