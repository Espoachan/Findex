#include "indexer.h"
#include <iostream>
#include <thread>
#include <functional>
#include <fstream>

void buildIndex(const std::string& rootFolder) {

    // a variable to store the number of available CPU cores
    unsigned int numCores;

    // get the number of available hardware threads
    numCores = std::thread::hardware_concurrency();

    // this is a fallback in case hardware_concurrency() fails, if it returns 0, then we set it to 4
    if (numCores == 0) {
        numCores = 4;
    } else {
        numCores = std::max(1u, numCores / 2u);
    }

    // this stores all the indexed files
    std::vector<fileEntry> globalIndex;

    // this stores the root folders for each thread to process
    std::vector<std::filesystem::path> roots;


    // get the root folders that will be used by each thread
    for (const auto& entry : std::filesystem::directory_iterator(rootFolder)) {
        if (entry.is_directory()) {
            roots.push_back(entry.path());
        } else if (entry.is_regular_file()) {
            globalIndex.push_back({
                entry.path().filename().string(),
                entry.path().string(),
                entry.file_size(),
                false
            });
        }
    }

    std::vector<std::vector<std::filesystem::path>> jobs(numCores);

    for (size_t i = 0; i < roots.size(); ++i) {
        jobs[i % numCores].push_back(roots[i]);
    }


    std::vector<std::thread> threads;
    std::vector<std::vector<fileEntry>> results(numCores);

    for (unsigned i = 0; i < numCores; ++i) {
        threads.emplace_back(indexJob, std::cref(jobs[i]), std::ref(results[i]));
    }

    for (auto& t : threads) {
    t.join();
    }

    for (auto& partial : results) {
        globalIndex.insert(
            globalIndex.end(),
            partial.begin(),
            partial.end()
        );
    }

    /*
    for (const auto& files : globalIndex)  {
        std::cout << files.name << " | " << files.path << " | " << files.size << " bytes" << std::endl;

    }
*/
    std::cout << globalIndex.size() << '\n';
    
    // Once it is built we save it to a file
    saveIndex(globalIndex);
}


void indexJob(const std::vector<std::filesystem::path>& folders, std::vector<fileEntry>& output) {
    std::error_code ec;

    for (auto& root : folders) {
        for (auto it = std::filesystem::recursive_directory_iterator(root,
         std::filesystem::directory_options::skip_permission_denied,
         ec);    
     it != std::filesystem::recursive_directory_iterator();
     it.increment(ec)) {
        if (ec) continue;
        if (it->is_regular_file()) {
            output.push_back({
                it->path().filename().string(),
                it->path().string(),
                it->file_size()
            });
        }
    }   
    }
}

void saveIndex(const std::vector<fileEntry>& indexToSave) {
    // this is the file where the index will be saved
    // it is a binary file
    std::ofstream outFile("index.dat", std::ios::binary);
    
    if (!outFile) {
        std::cerr << "An error ocurred while saving the index" << std::endl;
        return;
    }

    // This is the vector size (number of files indexed)
    size_t size = indexToSave.size();
    outFile.write(reinterpret_cast<const char*>(&size), sizeof(size));

    // This gets the info from each indexed file to store it in the .dat file
    for (const auto& entry : indexToSave) {
        // this is the name length
        size_t nameLen = entry.name.size();
        outFile.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen)); // And we store it
        outFile.write(entry.name.c_str(), nameLen); // The name characters

        // This is the path
        size_t pathLen = entry.path.size();
        outFile.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen)); // path length
        outFile.write(entry.path.c_str(), pathLen); // Path characters

        // This stores the file size
        outFile.write(reinterpret_cast<const char*>(&entry.size), sizeof(entry.size));

        // And we need to store if it is a directory or not
        outFile.write(reinterpret_cast<const char*>(&entry.isDirectory), sizeof(entry.isDirectory));
    }

    outFile.close();
    std::cout << "Index correctly saved (" << size << " elements)." << std::endl;
}


bool loadIndex(std::vector<fileEntry>& indexToLoad) {
    std::ifstream inFile("index.dat", std::ios::binary);

    if (!inFile) {
        // if we cant open the file then we return false
        return false;
    }

    // we get the size of the index from the file
    size_t size = 0;
    if (!inFile.read(reinterpret_cast<char*>(&size), sizeof(size))) {
        return false;
    }

    indexToLoad.resize(size);

    // we need to read each entry one by one
    for (size_t i = 0; i < size; ++i) {

        // NO MORE COMMENTS FROM HERE ON BECAUSE IM LAZY XD
        size_t nameLen = 0;
        inFile.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        indexToLoad[i].name.resize(nameLen);
        inFile.read(&indexToLoad[i].name[0], nameLen);

        size_t pathLen = 0;
        inFile.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        indexToLoad[i].path.resize(pathLen);
        inFile.read(&indexToLoad[i].path[0], pathLen);

        inFile.read(reinterpret_cast<char*>(&indexToLoad[i].size), sizeof(indexToLoad[i].size));

        inFile.read(reinterpret_cast<char*>(&indexToLoad[i].isDirectory), sizeof(indexToLoad[i].isDirectory));
    }

    inFile.close();
    std::cout << "Index loaded from cache(" << size << " elements)." << std::endl;
    return true;
}