#pragma once

#include "indexer.hpp"

class Findex {
public:
    // Relaunches the process elevated (UAC prompt) if it is not already
    // running as administrator. Reading the MFT requires admin rights.
    void elevate();
    USNIndexer indexer;
};
