#ifndef NPUDFJOB_H
#define NPUDFJOB_H

#include "DDCExecutor.h"
#include "DDCHandler.h"
#include "FFTExecutor.h"
#include "INpuFileNameFormatter.h"

#include "NpuDFInfo.h"
#include "NpuDFRequest.h"
#include "NpuDFProcess.h"

#include <boost/thread.hpp>

class NpuConfigure;
class IDBAccess;
class INpuExtractFileWriter;
class IFDataFormatEncoder;

class NpuDFJob : public IDDCHandler, public std::enable_shared_from_this<NpuDFJob> {
public:
    typedef std::function<void(const std::string&)> JOB_COMPLEDTED_CALLBACK;

public:
    NpuDFJob(const NpuDFRequest& df_req, NpuConfigure* config, std::unique_ptr<DataStorageInfo> data_storage_info);
    ~NpuDFJob();

    std::string start(JOB_COMPLEDTED_CALLBACK complted_callback);
    void stop();
    void wait();
    NpuDFInfo getDFInfo();
    int loadMFPoint(double * freqdata);
    void setMFData(std::vector<std::complex<double>> freqdata);
    void setInterFreq(float hfreq, float lfreq);


private:
    void extractIQ(const DataStorageInfo* dat_storage_info);
    void work();

public:
    virtual void onInitialized() override; // not use
    virtual void onClosed() override; // not use
    virtual void onReadFrame(int total_frame, int frame_num) override; // not use
    virtual void onFullBuffer(time_t timestamp, float* ddc_iq, size_t ddc_samples) override;
    void vaildation(time_t timestamp, float* ddc_iq, size_t ddc_samples);

private:

    NpuDFInfo _df_info;
    NpuDFProcess _dfProcess;
    NpuDFRequest _df_req;
    NpuConfigure* _config;
    std::unique_ptr<DataStorageInfo> _dat_storage_info;

    bool _running;
    std::string _guid;
    boost::mutex _mtx;
    std::unique_ptr<std::thread> _thread;
    JOB_COMPLEDTED_CALLBACK _completed_callback;

    DDCExecutor _ddc_exe;
};

#endif // NPUDFJOB_H
