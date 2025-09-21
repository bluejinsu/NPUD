#include "NpuExtractDeleteCommand.h"

#include "NpuExtractContainer.h"

NpuExtractDeleteCommand::NpuExtractDeleteCommand(NpuExtractContainer* ext_container)
	: _ext_container(ext_container)
{

}

NpuExtractDeleteCommand::~NpuExtractDeleteCommand() {

}

Json::Value NpuExtractDeleteCommand::process(const Json::Value& json_data) {

	std::string guid = json_data["guid"].asString();

	_ext_container->deleteExtract(guid);

	Json::Value ret;

	ret["success"] = true;
	ret["message"] = "";

	return ret;
}