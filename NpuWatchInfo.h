#ifndef NPU_WATCH_INFO_H
#define NPU_WATCH_INFO_H

#include <string>

struct NpuWatchInfo {
public:
    std::string guid;
    time_t starttime;
    time_t endtime;
    int frequency;
    int bandwidth;
    int samplerate;
    double threshold;
    double progress;
};

#endif
