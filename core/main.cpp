#include <iostream>
#include "findex.hpp"
#include "count_volumes.hpp"
#include <string>

int main() {
    Findex findex;
    findex.elevate();
    findex.run('C');

    return 0;
}