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

        std::cout << "indexed files: " << index.size() + 1 << "\r";
    }

}
