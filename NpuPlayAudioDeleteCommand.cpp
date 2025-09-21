#include "NpuPlayAudioDeleteCommand.h"

#include "NpuPlayAudioContainer.h"
#include "NpuPlayAudioJob.h"

NpuPlayAudioDeleteCommand::NpuPlayAudioDeleteCommand(NpuPlayAudioContainer* play_audio_container)
    : _play_audio_container(play_audio_container)
{
}

NpuPlayAudioDeleteCommand::~NpuPlayAudioDeleteCommand() {

}

Json::Value NpuPlayAudioDeleteCommand::process(const Json::Value& json_data) {
	std::string guid = json_data["guid"].asString();

    Json::Value ret;

    _play_audio_container->deletePlayAudioJob(guid);
    ret["success"] = true;
    ret["message"] = "";
    
    return ret;
}