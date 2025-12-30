#include "indexer.h"
#include <filesystem>
#include <iostream>
#include <vector> 

void buildIndex(const std::string& rootFolder) {

    std::vector<int> index;
    index.push_back(6);
fefef
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootFolder)) {
        std::cout << entry.path() << "\n";
    }
    
}