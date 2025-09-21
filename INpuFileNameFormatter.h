#ifndef INPU_FILE_NAME_FORMATTER
#define INPU_FILE_NAME_FORMATTER

#include <string>

#include <inttypes.h>
#include <time.h>

class INpuFileNameFormatter {
public:
    virtual std::string getFileName(int64_t frequency, int fs, int bw, time_t starttime) = 0;
};

#endif