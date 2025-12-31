#include "indexer.h"
#include <filesystem>
#include <iostream>
#include <vector>

void buildIndex(const std::string& rootFolder) {

    struct fileEntry
    {   
        std::string name;
        std::string path;
        uintmax_t size;
        bool isDirectory;
    };

    std::vector<fileEntry> index;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootFolder, std::filesystem::directory_options::skip_permission_denied)) {
        fileEntry f;
        f.name = entry.path().filename().string();
        f.path = entry.path().string();
        f.size = entry.file_size();
        f.isDirectory = entry.is_directory();
        index.push_back(f);

        std::cout << "Indexed files: " << index.size() + 1 << "\r";
    }

    std::cout << std::endl << "Indexing complete. Total files indexed: " << index.size() << std::endl;

    if (!index.empty()) {
        std::cout << "Last indexed file: " << index.back().name << "       " << index.back().path << std::endl;
    } else {
        std::cout << "There are no files to index." << std::endl;
    }


}
