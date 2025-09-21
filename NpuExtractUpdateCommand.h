#ifndef NPU_EXTRACT_UPDATE_COMMAND
#define NPU_EXTRACT_UPDATE_COMMAND

#include "INpuCtlCommand.h"

class NpuExtractUpdateCommand : public INpuCtlCommand {
public:
	NpuExtractUpdateCommand();
	~NpuExtractUpdateCommand();

	virtual Json::Value process(const Json::Value& json_data);
};

#endif