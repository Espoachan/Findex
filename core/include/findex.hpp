#pragma once

#include "indexer.hpp"

class Findex {
public:
    void elevate();
    bool run();   

private:
    USNIndexer indexer;
};