#pragma once

#include <string>
#include <vector>
#include <filesystem>

struct fileEntry
    {   
        std::string name;
        std::string path;
        uintmax_t size;
        bool isDirectory;
    };

    void indexJob(const std::vector<std::filesystem::path>& folders, std::vector<fileEntry>& out);
    void buildIndex(const std::string& rootFolder);
    void saveIndex(const std::vector<fileEntry>& indexToSave);
    bool loadIndex(std::vector<fileEntry>& indexToLoad);