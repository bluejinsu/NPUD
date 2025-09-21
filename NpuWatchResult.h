#ifndef NPU_WATCH_RESULT_H
#define NPU_WATCH_RESULT_H

#include <string>
#include <time.h>

struct NpuWatchResult {
    std::string guid;
    std::string filepath;
    time_t starttime;
    time_t endtime;
    int64_t frequency;
    int samplerate;
    int bandwidth;
    double power_level;
};

#endif // NPU_WATCH_RESULT_H