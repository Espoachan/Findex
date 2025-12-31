#include <iostream>
#include "indexer.h"
#include <filesystem>

int main () {

    std::string path = "C:\\Users\\PC\\Documents";

    std::cout << "Type the path to search from (enter to default: C:\\Users\\PC\\Documents): ";
    std::getline(std::cin, path);
    if (path.empty())
    {
        path = "C:\\Users\\PC\\Documents";
    } 
    else {
        if (!std::filesystem::exists(path))
        {
            std::cerr << "Path does not exist." << std::endl; 
            
            return 1;
        }
    }

    buildIndex(path);

    return 0;
}