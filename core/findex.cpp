#include "appdata.hpp"
#include "findex.hpp"
#include "elevate.hpp"

#include <windows.h>
#include <iostream>

void Findex::elevate() {
    if(!isAdministrator()) {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);

        ShellExecuteA(nullptr, "runas", path, nullptr, nullptr, SW_SHOW);
        return;
    }
}

bool Findex::run(char drive_letter) {
    std::cout << "Starting NTFS USN Journal Indexer..." << std::endl;
    
    getAppDataPath();
    indexer.initVolume(drive_letter);

    UsnJournalInfo old_info{};  

    // has_old as in we have a journal.dat file (old journal)
    bool has_old = indexer.loadJournalInfo(old_info);

    indexer.createUSNJournal();

    USN_JOURNAL_DATA current_data{};
    if(!indexer.getJournalData(current_data)) return false;

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
        indexer.indexFiles(); 
        // indexer.incrementalIndex(0);
    } else {
        std::cout << "found a compatible journal, adding new files\n";
        indexer.incrementalIndex(old_info.next_usn);
    }

    indexer.journal_info.journal_id = current_data.UsnJournalID;
    indexer.journal_info.next_usn = current_data.NextUsn;
    indexer.saveJournalInfo(indexer.journal_info);

    std::cout << "Data saved: \n\n" << "Journal id: " << indexer.journal_info.journal_id << "\n" << "Next USN: " << indexer.journal_info.next_usn << "\n";

    std::cout << "Press Enter to exit...";
    std::cin.get();

    return true;
}

