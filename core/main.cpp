#include <iostream>

#include "indexer.hpp"
#include "appdata.hpp"
#include "elevate.hpp"

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

    UsnJournalInfo old_info{};  

    // a true has_old means we already have a jorunal.dat file
    bool has_old = indexer.loadJournalInfo(old_info);

    indexer.createUSNJournal();

    USN_JOURNAL_DATA current_data{};
    if(!indexer.getJournalData(current_data)) return 1;

    bool fullIndex = false;
    if (!has_old) {
        fullIndex = true;
        std::cout << "there is no previous journal.dat; indexing... \n";
    } else if (old_info.journal_id != current_data.UsnJournalID) {
        fullIndex = true;
    } else if (old_info.next_usn < current_data.FirstUsn) {
        fullIndex = true;
    }

    if (fullIndex) {
        indexer.getAllFiles();
    } else {
        // TODO: implement incremental indexing
    }

    indexer.journal_info.journal_id = current_data.UsnJournalID;
    indexer.journal_info.next_usn   = current_data.NextUsn;
    indexer.saveJournalInfo(indexer.journal_info);

    std::cout << "Data saved:" << indexer.journal_info.journal_id << "\n" << indexer.journal_info.next_usn << "\n";

    std::cout << "Press Enter to exit...";
    std::cin.get();
    return 0;
}