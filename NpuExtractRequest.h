#ifndef NPU_EXTRACT_REQUEST_H
#define NPU_EXTRACT_REQUEST_H

#include <string>

#include <inttypes.h>
#include <time.h>

struct NpuExtractRequest {
	int instance;
	int64_t frequency;
	int bandwidth;
	time_t starttime;
	time_t endtime;
	std::string filetype;
};

#endif