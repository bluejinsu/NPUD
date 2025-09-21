#ifndef NPU_EXTRACT_16TR_WRITER
#define NPU_EXTRACT_16TR_WRITER

#include "INpuExtractFileWriter.h"

#include <fstream>
#include <string>

class INpuFileNameFormatter;

class NpuExtract16tWriter : public INpuExtractFileWriter {
private:
    INpuFileNameFormatter* _filename_formatter;

    std::fstream _file_stream;

public:
    NpuExtract16tWriter(INpuFileNameFormatter* filename_formatter);
    virtual ~NpuExtract16tWriter() {};

    virtual bool init(std::string output_path, int64_t frequency, int fs, int bw, time_t starttime) override;
    virtual void write(const float* iq, const int samples) override;
    virtual void close() override;
};

#endif
