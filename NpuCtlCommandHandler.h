#ifndef NPU_CTL_COMMAND_HANDLER
#define NPU_CTL_COMMAND_HANDLER

#include "INpuCtlCommand.h"

#include <json/json.h>

#include <map>
#include <memory>
#include <string>

class NpuExtractContainer;
class NpuPlayAudioContainer;

class NpuCtlCommandHandler {
protected:
	std::map<std::string, std::unique_ptr<INpuCtlCommand>> _command_table;

public:
	NpuCtlCommandHandler(NpuExtractContainer* ext_container, NpuPlayAudioContainer* play_audio_container);
	virtual ~NpuCtlCommandHandler();

	virtual bool canHandle(const Json::Value& json_data);
	virtual Json::Value process(const Json::Value& json_data);
};

#endif