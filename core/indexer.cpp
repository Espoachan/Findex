#include "indexer.h"
#include <filesystem>
#include <iostream>
#include <vector> 

void buildIndex(const std::string& rootFolder) {

    std::vector<int> index;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootFolder, std::filesystem::directory_options::skip_permission_denied | std::filesystem::directory_options::follow_directory_symlink)) {
        std::cout << entry.path() << "\n";
    }

}
