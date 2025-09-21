#ifndef DATA_STORAGE_INFO_H
#define DATA_STORAGE_INFO_H

#include <string>

#include <inttypes.h>
#include <time.h>

struct DataStorageInfo {
    int64_t wddc_freq;
    time_t starttime;
    time_t endtime;
    int64_t samplerate;
    int frame_samples;
    std::string storage_dir;
};

#endif