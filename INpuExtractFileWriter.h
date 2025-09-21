#ifndef INPU_EXTRACT_FILE_WRITER
#define INPU_EXTRACT_FILE_WRITER

#include <string>

#include <inttypes.h>
#include <time.h>

class INpuExtractFileWriter {
public:
    virtual bool init(std::string output_path, int64_t frequency, int fs, int bw, time_t starttime) = 0;
    virtual void write(const float* iq, const int samples) = 0;
    virtual void close() = 0;
};

#endif
