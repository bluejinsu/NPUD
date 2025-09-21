#include "NpuBasicFileNameFormatter.h"

#include <iomanip>
#include <sstream>

NpuBasicFileNameFormatter::NpuBasicFileNameFormatter(std::string sitename) 
    : _sitename(sitename)
{

}

std::string NpuBasicFileNameFormatter::getFileName(int64_t frequency, int fs, int bw, time_t starttime) {
    std::stringstream ssFileName;
    ssFileName << "T";
    ssFileName << std::put_time(localtime(&starttime), "%Y%m%d_%H%M%S_");
    ssFileName << "C";
    ssFileName << std::setfill('0') << std::setw(11) << frequency;
    ssFileName << "_";
    ssFileName << _sitename;
    ssFileName << ".";

    ssFileName << "cplx";

    ssFileName << ".";
    ssFileName << std::setfill('0') << std::setw(6) << fs;

    return ssFileName.str(); 
}