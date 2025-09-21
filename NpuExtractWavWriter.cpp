#include "NpuExtractWavWriter.h"

#include "INpuFileNameFormatter.h"


NpuExtractWavWriter::NpuExtractWavWriter(INpuFileNameFormatter* filename_formatter)
    : _filename_formatter(filename_formatter)
{

}

bool NpuExtractWavWriter::init(std::string output_path, int64_t frequency, int fs, int bw, time_t starttime) {
    std::string filename = _filename_formatter->getFileName(frequency, fs, bw, starttime) + ".wav";
    std::string filepath = output_path + filename;
    _file_stream.open(filepath, std::ios::out | std::ios::binary);

    return _file_stream.good();
}

void NpuExtractWavWriter::write(const float* iq, const int samples) {
    
}

void NpuExtractWavWriter::close() {
    _file_stream.close();
}
