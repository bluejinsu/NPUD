#ifndef NPUDFREQUEST_H
#define NPUDFREQUEST_H

#include <string>

struct NpuDFRequest {
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

#endif // NPUDFREQUEST_H
