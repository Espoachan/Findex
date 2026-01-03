#include <iostream>
#include "indexer.h"
#include <filesystem>

int main () {

    std::string path = "C:\\";
    
    // Loading the cached index
    // In case it doesn't exist we call the function that builds it
    std::vector<fileEntry> cachedIndex;
    if (loadIndex(cachedIndex)) {
    } else {
        buildIndex(path);
    }

    return 0;
}