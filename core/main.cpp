#include "indexer.h"
#include "appdatapath.h"
#include <iostream>
#include "makeadministrator.h"

int main() {
    if(!isAdministrator()) {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);

        ShellExecuteA(nullptr, "runas", path, nullptr, nullptr, SW_SHOW);
        return 0;
    }

    std::cout << "Starting NTFS USN Journal Indexer..." << std::endl;
    
    getAppDataPath();
    USNIndexer indexer;
    indexer.initVolume('C');
    indexer.createUSNJournal();
    indexer.getJournalData(indexer.data);
    indexer.saveJournalInfo(indexer.journal_info);

    std::cout << "Data saved:" << indexer.journal_info.journal_id << "\n" << indexer.journal_info.next_usn << "\n";

    std::cout << "Press Enter to exit...";
    std::cin.get();
    return 0;
}