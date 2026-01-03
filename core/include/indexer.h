#pragma once

#include <vector>
#include <string>
#include <windows.h>
#include <winioctl.h>

struct FileRecord {
    unsigned long long id;
    unsigned long long parentId;
    std::string name;
    bool isDirectory;
};

class USNIndexer {
public:
    USNIndexer();
    ~USNIndexer();

    // Initialize the handle to the volume (e.g., 'C')
    bool initVolume(char driveLetter);

    // Create the USN Journal if it doesn't exist
    bool createUSNJournal();

    // Get the basic journal data (ID, Min/Max range)
    bool getJournalData(USN_JOURNAL_DATA& data);

    // Scan all files instantly
    std::vector<FileRecord> getAllFiles();

private:
    HANDLE hVolume;
    char driveLetter;
};