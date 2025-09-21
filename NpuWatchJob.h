#ifndef NPU_WATCH_JOB_H
#define NPU_WATCH_JOB_H

#include "DDCExecutor.h"
#include "DDCHandler.h"
#include "FFTExecutor.h"
#include "INpuFileNameFormatter.h"
#include "NpuWatchInfo.h"
#include "NpuWatchResult.h"
#include "NpuWatchRequest.h"
#include "NpuWatchProcess.h"

#include <boost/thread.hpp>

class NpuConfigure;
class IDBAccess;
class INpuExtractFileWriter;
class IFDataFormatEncoder;

class NpuWatchJob : public IDDCHandler, public std::enable_shared_from_this<NpuWatchJob> {
public:
	typedef std::function<void(const std::string&)> JOB_COMPLEDTED_CALLBACK;

public:
    NpuWatchJob(const NpuWatchRequest& watch_req, NpuConfigure* config, std::unique_ptr<DataStorageInfo> data_storage_info);
    ~NpuWatchJob();

    std::string start(JOB_COMPLEDTED_CALLBACK completed_callback);
    void stop();
    void wait();
    NpuWatchInfo getWatchInfo();

private:
    void extractIQ(const DataStorageInfo* dat_storage_info);
    void work();

public:
    virtual void onInitialized() override;
    virtual void onClosed() override;
    virtual void onReadFrame(int total_frame, int frame_num) override;
    virtual void onFullBuffer(time_t timestamp, float* ddc_iq, size_t ddc_samples) override;

private:
    NpuWatchProcess _watchProcess;
    NpuWatchRequest _watch_req;
    NpuWatchInfo _watch_info;
    NpuConfigure* _config;
    std::unique_ptr<DataStorageInfo> _dat_storage_info;

    bool _running;
    std::string _guid;
    boost::mutex _mtx;
    std::unique_ptr<std::thread> _thread;
    JOB_COMPLEDTED_CALLBACK _completed_callback;

    DDCExecutor _ddc_exe;
};

#endif
