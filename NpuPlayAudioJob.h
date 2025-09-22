#ifndef NPU_PLAY_AUDIO_H
#define NPU_PLAY_AUDIO_H

#include "NpuPlayAudioRequest.h"

#include <boost/make_unique.hpp>
#include <boost/thread.hpp>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <cstdint>   // uint32_t

class TaskScheduler;
class ServerMediaSession;
class DataStorageInfo;
class ExtractDemodulator;
class IPCMSource;
class NpuConfigure;
class RTSPServer;
class UsageEnvironment;

class NpuPlayAudioJob : public std::enable_shared_from_this<NpuPlayAudioJob> {
public:
    typedef std::function<void(const std::string&)> JOB_COMPLEDTED_CALLBACK;
    RTSPServer* _rtspServer = nullptr;
private:
    TaskScheduler* _scheduler = nullptr;          // live555 scheduler
    ServerMediaSession* _sms = nullptr;           // live555 session
    NpuPlayAudioRequest _play_audio_req;
    NpuConfigure* _config;
    std::unique_ptr<DataStorageInfo> _data_storage_info;

    boost::mutex _mtx;
    std::string _guid;
    std::unique_ptr<std::thread> _thread;
    bool _running = false;

    
    UsageEnvironment* _env = nullptr;
    std::thread _serverThread;

    std::string _rtsp_ip;
    int _rtsp_port = 0;

    JOB_COMPLEDTED_CALLBACK _completed_callback;

    // Event trigger id (live555 TaskScheduler::EventTriggerId와 동일한 너비)
    uint32_t _stopTrigger {0};

private:
    void startRtspServer(IPCMSource* ext_demod);
    void work();

public:
    NpuPlayAudioJob(const NpuPlayAudioRequest& play_audio_req, NpuConfigure* config, int rtsp_port, std::unique_ptr<DataStorageInfo> data_storage_info);
    ~NpuPlayAudioJob();

    std::string start(JOB_COMPLEDTED_CALLBACK completed_callback);
    void stop();
    void wait();

    std::string getGuid() { return _guid; }
    std::string getRtspIp() { return _rtsp_ip; }
    int getRtspPort() { return _rtsp_port; }
};

#endif
