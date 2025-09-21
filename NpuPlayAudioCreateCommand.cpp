#include "NpuPlayAudioCreateCommand.h"

#include "NpuPlayAudioContainer.h"
#include "NpuPlayAudioJob.h"

NpuPlayAudioCreateCommand::NpuPlayAudioCreateCommand(NpuPlayAudioContainer* play_audio_container)
    : _play_audio_container(play_audio_container)
{
}

NpuPlayAudioCreateCommand::~NpuPlayAudioCreateCommand() {

}

Json::Value NpuPlayAudioCreateCommand::process(const Json::Value& json_data) {
    int instance = json_data["instance"].asInt();
	int64_t frequency = json_data["frequency"].asInt64();
	int bandwidth = json_data["bandwidth"].asInt();
	time_t starttime = json_data["starttime"].asInt64();
	time_t endtime = json_data["endtime"].asInt64();
	std::string demod_type = json_data["demodtype"].asString();
    float scale = json_data["scale"].asFloat();

#if 1
    int squelch_mode = json_data["squelch"].asInt();
    float squelch_threshold = json_data["squelch-threshold"].asFloat();
#else
    bool squelch_mode = false;
    float squelch_threshold = -80.0f;
#endif

    NpuPlayAudioRequest req = {
        instance,
        frequency,
        bandwidth,
        starttime,
        endtime,
        demod_type,
        squelch_mode == 0 ? false : true,
        squelch_threshold,
        scale
    };

    Json::Value ret;

    auto job = _play_audio_container->createPlayAudioJob(req);
    if (!job) {
        ret["success"] = false;
        ret["message"] = "Failed to create play audio job";
    }
    else {
        ret["guid"] = job->getGuid();
        ret["rtsp-ip"] = job->getRtspIp();
        ret["rtsp-port"] = job->getRtspPort();
        ret["success"] = true;
        ret["message"] = "";
    }

    return ret;
}
