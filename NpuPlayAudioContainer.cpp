#include "NpuPlayAudioContainer.h"

#include "DataStorageSearcher.h"
#include "NpuConfigure.h"
#include "NpuPlayAudioJob.h"

NpuPlayAudioContainer::NpuPlayAudioContainer(NpuConfigure* config) 
    : _config(config)
    , _base_port(8554)
{

}
NpuPlayAudioContainer::~NpuPlayAudioContainer() {

}

std::shared_ptr<NpuPlayAudioJob> NpuPlayAudioContainer::createPlayAudioJob(const NpuPlayAudioRequest& play_audio_req) {
    int port = _base_port;

    // lock
    {
        auto last_job_port_iter = _job_ports.rbegin();
        if (last_job_port_iter == _job_ports.rend()) {
            port = _base_port;
        } else {
            int last_port =  last_job_port_iter->second;
            port = last_port + 1;
        }
    }

    std::string nsud_ipaddress = _config->getValue("NSUD.IPADDRESS");
    DataStorageSearcher searcher(nsud_ipaddress);
    auto data_storage_info = searcher.search(play_audio_req.starttime, play_audio_req.endtime, play_audio_req.frequency);

    if (!data_storage_info) {
        return nullptr;
    }

    auto job = std::make_shared<NpuPlayAudioJob>(play_audio_req, _config, port, std::move(data_storage_info));
    std::string guid = job->start(
        [this](const std::string& guid) {

        }
    );

    if (guid == "")
        return nullptr;
    
    // lock
    {
        std::lock_guard<boost::mutex> lock(_mtx);
        _job_container[guid] = job;
        _job_ports[guid] = port;
    }
    return job;
}

void NpuPlayAudioContainer::deletePlayAudioJob(const std::string& guid) {
    // lock
    {
        std::lock_guard<boost::mutex> lock(_mtx);
        if (_job_container.find(guid) != _job_container.end()) {
            _job_container[guid]->stop();

            _job_container.erase(guid);
            _job_ports.erase(guid);
        }
    }
}