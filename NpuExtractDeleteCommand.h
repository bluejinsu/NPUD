#ifndef NPU_EXTRACT_DELETE_COMMAND
#define NPU_EXTRACT_DELETE_COMMAND

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuExtractDeleteCommand : public INpuCtlCommand {
private:
	NpuExtractContainer* _ext_container;

public:
	NpuExtractDeleteCommand(NpuExtractContainer* ext_container);
	~NpuExtractDeleteCommand();

	virtual Json::Value process(const Json::Value& json_data);
};

#endif