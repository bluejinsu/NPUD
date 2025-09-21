#ifndef NPU_EXTRACT_CREATE_COMMAND
#define NPU_EXTRACT_CREATE_COMMAND

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuExtractCreateCommand : public INpuCtlCommand {
private:
	NpuExtractContainer* _ext_container;

public:
	NpuExtractCreateCommand(NpuExtractContainer* ext_container);
	~NpuExtractCreateCommand();

	virtual Json::Value process(const Json::Value& json_data);
};

#endif