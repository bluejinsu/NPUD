#ifndef NPU_EXTRACT_RESULT_H
#define NPU_EXTRACT_RESULT_H

#include <string>
#include <time.h>

struct NpuExtractResult {
    std::string guid;
    std::string filepath;
    time_t starttime;
    time_t endtime;
    int64_t frequency;
    int samplerate;
    int bandwidth;
    double power_level;
};

#endif