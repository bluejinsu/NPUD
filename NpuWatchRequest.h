#ifndef NPU_WATCH_REQUEST_H
#define NPU_WATCH_REQUEST_H

#include <string>

struct NpuWatchRequest {
    int instance;
    int64_t frequency;
    int bandwidth;
    time_t starttime;
    time_t endtime;
    std::string filetype;
    double threshold;
    int holdtime;
    int continuetime;
};

#endif
