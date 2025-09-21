#ifndef NPU_EXTRACT_LIST_COMMAND
#define NPU_EXTRACT_LIST_COMMAND

#include "INpuCtlCommand.h"

#include <memory>

class NpuExtractContainer;

class NpuExtractListCommand : public INpuCtlCommand {
private:
	NpuExtractContainer* _ext_container;

public:
	NpuExtractListCommand(NpuExtractContainer* ext_container);
	~NpuExtractListCommand();

	virtual Json::Value process(const Json::Value& json_data);
};

#endif