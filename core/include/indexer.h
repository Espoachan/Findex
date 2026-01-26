#pragma once

#include <vector>
#include <string>
#include <windows.h>
#include <winioctl.h>

struct FileRecord {
    unsigned long long id;
    unsigned long long parentId;
    std::wstring name;
    bool isDirectory;
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

    USN_JOURNAL_DATA data;
    HANDLE hVolume;
    char drive_letter;
    UsnJournalInfo journal_info;

    bool saveJournalInfo(const UsnJournalInfo& info);
    bool loadJournalInfo(UsnJournalInfo& info);


    bool initVolume(char driveLetter);
    bool createUSNJournal();
    bool getJournalData(USN_JOURNAL_DATA& data);

    std::vector<FileRecord> getAllFiles();

private:    

};