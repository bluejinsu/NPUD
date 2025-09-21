#ifndef NPUDFINFO_H
#define NPUDFINFO_H

#include <string>
#include <vector>

struct NpuDFList {
public:
    int frequency;
    int bandwidth;
    time_t startTime;
    time_t endTime;
    int Azimuth;
};


struct NpuDFInfo {
public:
    std::string guid;
    time_t starttime;
    time_t endtime;
    int frequency;
    int bandwidth;
    int samplerate;
    double threshold;
    double progress;
    std::vector<NpuDFList> dfResults;
};


#endif // NPUDFINFO_H
