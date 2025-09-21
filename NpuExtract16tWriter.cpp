#include "NpuExtract16tWriter.h"

#include "INpuFileNameFormatter.h"

#include <vector>

#include <netinet/in.h>

NpuExtract16tWriter::NpuExtract16tWriter(INpuFileNameFormatter* filename_formatter)
    : _filename_formatter(filename_formatter)
{

}

bool NpuExtract16tWriter::init(std::string output_path, int64_t frequency, int fs, int bw, time_t starttime) {
    std::string filename = _filename_formatter->getFileName(frequency, fs, bw, starttime) + ".16t";
    std::string filepath = output_path + "/" + filename;
    _file_stream.open(filepath, std::ios::out | std::ios::binary);

    return true;
}

void NpuExtract16tWriter::write(const float* iq, const int samples) {
    std::vector<short> iq16tr;
    iq16tr.resize(samples * 2);
    for (int k = 0; k < samples; k++) {
        iq16tr[k * 2] = htons((short)iq[2 * k]);
        iq16tr[k * 2 + 1] = htons((short)iq[2 * k + 1]);
    }

    _file_stream.write((const char*)iq16tr.data(), sizeof(short) * 2 * samples);
}

void NpuExtract16tWriter::close() {
    _file_stream.close();
}
