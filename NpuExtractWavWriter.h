#ifndef NPU_EXTRACT_WAV_WRITER
#define NPU_EXTRACT_WAV_WRITER

#include "INpuExtractFileWriter.h"

#include <string>
#include <fstream>

class INpuFileNameFormatter;

class NpuExtractWavWriter : public INpuExtractFileWriter {
private:
    INpuFileNameFormatter* _filename_formatter;

    std::fstream _file_stream;

public:
    NpuExtractWavWriter(INpuFileNameFormatter* filename_formatter);
    virtual ~NpuExtractWavWriter() {};
    
    virtual bool init(std::string ouput_path, int64_t frequency, int fs, int bw, time_t starttime) override;
    virtual void write(const float* iq, const int samples) override;
    virtual void close() override;
};

#endif
