#include "NpuWatchJob.h"

#include "DataStorageInfo.h"
#include "DataStorageSearcher.h"
#include "NpuBasicFileNameFormatter.h"
#include "NpuConfigure.h"
#include "NpuExtract16tWriter.h"
#include "NpuExtractWavWriter.h"
#include "NpuWatchResult.h"
#include "NpuWatchProcess.h"
#include "IFDataFormatEncoder.h"
#include "OracleDBAccess.h"
#include "RestClient.h"
#include "util.hpp"

#include <boost/make_unique.hpp>

NpuWatchJob::NpuWatchJob(const NpuWatchRequest& watch_req, NpuConfigure* config, std::unique_ptr<DataStorageInfo> data_storage_info)
    : _watch_req(watch_req)
    , _config(config)
    , _running(false)
    , _watchProcess(config)
    , _dat_storage_info(std::move(data_storage_info))
{
    _watch_info.frequency = watch_req.frequency;
    _watch_info.bandwidth = watch_req.bandwidth;
    _watch_info.starttime = watch_req.starttime;
    _watch_info.endtime = watch_req.endtime;
    _watch_info.threshold = watch_req.threshold;
    _watch_info.progress = 0.0;
}

NpuWatchJob::~NpuWatchJob() {
}

std::string NpuWatchJob::start(JOB_COMPLEDTED_CALLBACK completed_callback) {
    boost::lock_guard<boost::mutex> lock(_mtx);

    if (_running)
        return _watch_info.guid;

    _watchProcess.loadConfig();

    _watch_info.guid = generateID();
    _completed_callback = completed_callback;
    _running = true;
    _thread.reset(new std::thread(std::bind(&NpuWatchJob::work, shared_from_this())));

    return _watch_info.guid;
}

void NpuWatchJob::stop() {
    _running = false;
}

void NpuWatchJob::wait() {
    if (_thread && _thread->joinable()) {
        _thread->join();
    }
}

NpuWatchInfo NpuWatchJob::getWatchInfo()
{
    return _watch_info;
}

void NpuWatchJob::extractIQ(const DataStorageInfo* dat_storage_info) {
    if (!_ddc_exe.initDDC(_watch_info.starttime, _watch_info.endtime, _watch_info.frequency, _watch_info.bandwidth, dat_storage_info))
        return;

    unsigned int channel_samplerate = _ddc_exe.getChannelSamplerate();
    unsigned int channel_bandwidth = _ddc_exe.getChannelBandwidth();
    unsigned int ddc_samples = _ddc_exe.getDDCSamples();

    _watch_info.samplerate = channel_samplerate;

    _watchProcess.init(_watch_req, _watch_info, ddc_samples);
    while (_running && _ddc_exe.executeDDC(this)) {
        // do nothing
    }

    _watchProcess.close();
    _ddc_exe.close();
} 

void NpuWatchJob::work() {
    while (true) {
		// lock
        {
            boost::lock_guard<boost::mutex> lock(_mtx);
            if (!_running)
                break;
        }

        if (_watch_info.starttime >= _watch_info.endtime) {
            std::cerr << "Invalid time range" << std::endl;
            break;
        }

        if (_watch_info.starttime < _dat_storage_info->starttime) {
            std::cerr << "Invalid starttime" << std::endl;
            break;
        }

        if (_watch_info.endtime > _dat_storage_info->endtime) {
            std::cerr << "Invalid endtime" << std::endl;
            break;
        }

        
        int64_t start_sample_pos = (_watch_info.starttime - _dat_storage_info->starttime) * _dat_storage_info->samplerate;
        int64_t end_sample_pos = (_watch_info.endtime - _dat_storage_info->starttime) * _dat_storage_info->samplerate;
        int64_t sample_count = end_sample_pos - start_sample_pos;
        double frame_count = sample_count / (double)_dat_storage_info->frame_samples;

        std::cout << "start_sample_pos : " << start_sample_pos << std::endl;
        std::cout << "end_sample_pos : " << end_sample_pos << std::endl;
        std::cout << "frame_samples : " << _dat_storage_info->frame_samples << std::endl;
        std::cout << "sample_count : " << sample_count << std::endl;
        std::cout << "frame_count : " << frame_count << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        extractIQ(_dat_storage_info.get());

        auto end_time = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> elapsed = end_time - start_time;

        std::cout << "Execution time: " << elapsed.count() << " sec" << std::endl;

        break;
	}

    if (_completed_callback)
        _completed_callback(_watch_info.guid);
}

void NpuWatchJob::onInitialized() {
    // TODO : Implement
}

void NpuWatchJob::onClosed() {
    // TODO : Implement
}

void NpuWatchJob::onReadFrame(int total_frame, int frame_num) {
}

void NpuWatchJob::onFullBuffer(time_t timestamp, float* ddc_iq, size_t ddc_samples) {
    double duration = _watch_req.endtime - _watch_req.starttime;
    _watch_info.progress = ((timestamp / 1000) - (double)_watch_req.starttime) / duration * 100.0;

    _watchProcess.onIQDataReceived(timestamp, _watch_req.frequency, _ddc_exe.getChannelBandwidth(), _ddc_exe.getChannelSamplerate(), ddc_iq, ddc_samples);
}
