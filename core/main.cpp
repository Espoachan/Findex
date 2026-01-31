#include <iostream>
#include "findex.hpp"
#include "count_volumes.hpp"
#include <string>

int main() {
    Findex findex;
    findex.elevate();
    int volume_number = countVolumes();
    for (int i = 0; i < volume_number; i++) {
        char volume = char(i + 'A');
        findex.run(volume);
    }

    return 0;
}