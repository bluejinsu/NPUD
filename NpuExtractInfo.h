#ifndef NPU_EXTRACT_INFO_H
#define NPU_EXTRACT_INFO_H

#include <string>
#include <vector>

#include <inttypes.h>
#include <time.h>

struct NpuExtractInfo {
	std::string guid;
	int64_t frequency;
	int bandwidth;
	time_t starttime;
	time_t endtime;
    int progress;
	std::vector<std::string> files;
};

#endif
