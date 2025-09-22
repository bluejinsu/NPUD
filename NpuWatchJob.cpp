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
#include <thread>    // [ADD]
#include <chrono>    // [ADD]
#include <iostream>  // [ADD] (std::cerr 등)
#include <algorithm> // [ADD] std::max/min

// -------------------- [ADD] 퍼센트 클램프 --------------------
namespace {
    inline int clampPercent(int v) {
        return (v < 0) ? 0 : (v > 100 ? 100 : v);
    }
}
// -------------------------------------------------------------

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
    _watch_info.endtime   = watch_req.endtime;
    _watch_info.threshold = watch_req.threshold;
    _watch_info.progress  = 0.0;
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
    // [PATCH] 러닝 플래그 내리고 DDC도 즉시 중지해 I/O 스레드 종료 유도
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        _running = false;
    }
    _ddc_exe.stop();
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
    unsigned int channel_bandwidth  = _ddc_exe.getChannelBandwidth();
    unsigned int ddc_samples        = _ddc_exe.getDDCSamples();

    _watch_info.samplerate = channel_samplerate;

    _watchProcess.init(_watch_req, _watch_info, ddc_samples);

    // ====== [PATCH] 실행 루프 (ioDone+ringEmpty 재확인 N회) ======
    {
        constexpr int kMaxIdleZeroReadableChecks = 10;   // 재확인 횟수
        constexpr int kIdleSleepMs               = 2;    // 재확인 간격(ms)

        while (_running && _ddc_exe.getRunning()) {
            bool didWork = _ddc_exe.executeDDC(this, 1);

            if (!didWork) {
                // ioDone + ringEmpty → 짧은 재확인 루프
                if (_ddc_exe.ioDone() && _ddc_exe.ringReadable() == 0) {
                    bool stillEmpty = true;
                    for (int retry = 0; retry < kMaxIdleZeroReadableChecks; ++retry) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(kIdleSleepMs));
                        if (_ddc_exe.ringReadable() > 0) {
                            stillEmpty = false;
                            break;  // 다시 읽을 게 있으니 루프 복귀
                        }
                    }
                    if (stillEmpty) {
                        break; // 끝까지 비어 있으면 종료
                    } else {
                        continue; // 다시 executeDDC 시도
                    }
                }

                // 그 외 상황은 짧게 쉼
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    _watchProcess.close();
    _ddc_exe.close(); // stop + join
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
        int64_t end_sample_pos   = (_watch_info.endtime   - _dat_storage_info->starttime) * _dat_storage_info->samplerate;
        int64_t sample_count     = end_sample_pos - start_sample_pos;
        double frame_count       = sample_count / (double)_dat_storage_info->frame_samples;

        std::cout << "start_sample_pos : " << start_sample_pos << std::endl;
        std::cout << "end_sample_pos   : " << end_sample_pos << std::endl;
        std::cout << "frame_samples    : " << _dat_storage_info->frame_samples << std::endl;
        std::cout << "sample_count     : " << sample_count << std::endl;
        std::cout << "frame_count      : " << frame_count << std::endl;

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
    // [PATCH] 진행률 계산을 NpuExtractJob과 동일 스타일로 보정
    const long long start_ms = static_cast<long long>(_watch_req.starttime) * 1000LL;
    const long long end_ms   = static_cast<long long>(_watch_req.endtime)   * 1000LL;
    const long long total_ms = std::max(1LL, end_ms - start_ms); // 0 나눗셈 방지
    const long long cur_ms   = std::max(0LL, std::min(end_ms - start_ms, static_cast<long long>(timestamp) - start_ms));

    int percent = static_cast<int>((100.0L * cur_ms) / total_ms);
    _watch_info.progress = clampPercent(percent);

    _watchProcess.onIQDataReceived(
        timestamp,                                  // ms 단위 (DDC에서 전달된 값)
        _watch_req.frequency,
        _ddc_exe.getChannelBandwidth(),
        _ddc_exe.getChannelSamplerate(),
        ddc_iq,
        ddc_samples
    );
}
