#include "indexer.h"
#include <iostream>
#include <thread>
#include <functional>

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