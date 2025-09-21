#ifndef NPU_WATCH_PROCESS_H
#define NPU_WATCH_PROCESS_H

#include "FFTExecutor.h"
#include "IFDataFormatEncoder.h"
#include "INpuExtractFileWriter.h"
#include "NpuBasicFileNameFormatter.h"
#include "NpuConfigure.h"
#include "NpuWatchInfo.h"
#include "NpuWatchRequest.h"
#include "NpuWatchResult.h"

#include <boost/filesystem.hpp>

#include <fstream>
#include <memory>
#include <vector>

class NpuConfigure;

struct stWatchConfigData {
	double level_offset;
	double ant_volt_ref;
	std::string output_path;
};

struct WatchFileSaveInfo {
    time_t starttime;
    time_t endttime;
    int64_t frequency;
    int samplerate;
    int bandwidth;
    std::ofstream _outDDCFile;
    boost::filesystem::path _output_path;
    std::string _fileName;
    std::string _filepath;
};

class NpuWatchProcess {
public:
    NpuWatchProcess(NpuConfigure* config);

    ~NpuWatchProcess();

    void init(const NpuWatchRequest& watch_req, const NpuWatchInfo& watch_info, const size_t ddc_samples);

    void close();
    
    void loadConfig();
    void onIQDataReceived(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate, const float* ddc_iq, const size_t ddc_samples);

private:
    double getPowerLevel(const float* ddc_iq, size_t ddc_samples);

    void initFile();
    void writeToFile(const float* ddc_iq, const int samples);
    void closeFile();
    void initFileSaveInfo(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate);
    void updateFileSaveInfo(time_t timestamp, int64_t frequency, int bandwidth, const int samplerate);
    bool saveDatabase();

private:
    NpuWatchRequest _watch_req;
    NpuWatchInfo _watch_info;
    FFTExecutor _fft_executor;
    
    double _max_channel_power;
    double _holdtime;

    NpuBasicFileNameFormatter _filename_formatter;
    INpuExtractFileWriter* _file_writer;
    std::unique_ptr<IFDataFormatEncoder> _encoder;

    std::vector<std::vector<double>> _spec_lines;
    std::vector<double> _avg_spec;
    int _avg_count;

    WatchFileSaveInfo _watchFileSaveInfo;

    NpuConfigure* _config;
    stWatchConfigData _config_data;

    enum EnWatchState {
        DEAD,
        ALIVE,
        HOLD,
    } _state;
};
    

#endif // NPU_WATCH_PROCESS_H
