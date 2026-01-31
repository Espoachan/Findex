#include "count_volumes.hpp"

int countVolumes() {
    HANDLE h_find = INVALID_HANDLE_VALUE;

    char volume_name[MAX_PATH];
    int volume_count = 0;

    h_find = FindFirstVolumeA(volume_name, ARRAYSIZE(volume_name));

    if (h_find == INVALID_HANDLE_VALUE) {
        std::cerr << "findfirstvolume failed here is the error " << GetLastError() << std::endl;
    }

    do {
        volume_count++;
    } while (FindNextVolumeA(h_find, volume_name, ARRAYSIZE(volume_name)));

    FindVolumeClose(h_find);
    return volume_count;
}