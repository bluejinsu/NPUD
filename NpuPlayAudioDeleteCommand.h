#ifndef NPU_PLAY_AUDIO_DELETE_COMMAND_H
#define NPU_PLAY_AUDIO_DELETE_COMMAND_H

#include "INpuCtlCommand.h"

#include <json/json.h>

class NpuPlayAudioContainer;

class NpuPlayAudioDeleteCommand : public INpuCtlCommand {
private:
    NpuPlayAudioContainer* _play_audio_container;
    
public:
    NpuPlayAudioDeleteCommand(NpuPlayAudioContainer* play_audio_container);
    virtual ~NpuPlayAudioDeleteCommand();

    virtual Json::Value process(const Json::Value& json_data);
};

#endif