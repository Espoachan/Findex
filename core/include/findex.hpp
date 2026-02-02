#pragma once

#include "indexer.hpp"

class Findex {
public:
    void elevate();
    bool run(char drive_letter);   
    USNIndexer indexer;
private:

};