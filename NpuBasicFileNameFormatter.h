#ifndef NPU_BASIC_FILENAME_FORMATTER
#define NPU_BASIC_FILENAME_FORMATTER

#include "INpuFileNameFormatter.h"

class NpuBasicFileNameFormatter : public INpuFileNameFormatter {
private:
    std::string _sitename;
public:
    NpuBasicFileNameFormatter(std::string sitename);
    virtual std::string getFileName(int64_t frequency, int fs, int bw, time_t starttime) override;
};

#endif