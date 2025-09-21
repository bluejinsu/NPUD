#ifndef NPU_PLAY_AUDIO_CONTAINER_H
#define NPU_PLAY_AUDIO_CONTAINER_H

#include "NpuPlayAudioRequest.h"

#include <boost/thread.hpp>

#include <map>
#include <string>

class NpuConfigure;
class NpuPlayAudioJob;

class NpuPlayAudioContainer {
private:
    NpuConfigure* _config;
    int _base_port;

    boost::mutex _mtx;
    std::map<std::string, std::shared_ptr<NpuPlayAudioJob>> _job_container;
    std::map<std::string, int> _job_ports;

public:
    NpuPlayAudioContainer(NpuConfigure* config);
    ~NpuPlayAudioContainer();

    std::shared_ptr<NpuPlayAudioJob> createPlayAudioJob(const NpuPlayAudioRequest& play_audio_req);
    void deletePlayAudioJob(const std::string& guid);
};

#endif