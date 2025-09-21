#include "NpuExtractCreateCommand.h"

#include "NpuExtractContainer.h"

#include <time.h>

NpuExtractCreateCommand::NpuExtractCreateCommand(NpuExtractContainer* ext_container)
	: _ext_container(ext_container)
{
}

NpuExtractCreateCommand::~NpuExtractCreateCommand() {
}

Json::Value NpuExtractCreateCommand::process(const Json::Value& json_data) {
	int instance = json_data["instance"].asInt();
	int64_t frequency = json_data["frequency"].asInt64();
	int bandwidth = json_data["bandwidth"].asInt();
	time_t starttime = json_data["starttime"].asInt64();
	time_t endtime = json_data["endtime"].asInt64();
	std::string filetype = json_data["filetype"].asString();

	NpuExtractRequest ext_req = {
		instance,
		frequency,
		bandwidth,
		starttime,
		endtime,
		filetype
	};

	Json::Value ret;

	std::string guid = _ext_container->createExtractJob(ext_req);
	if (guid == "") {
		ret["success"] = false;
		ret["message"] = "Failed to craete extract job";
	}
	else {
		ret["guid"] = guid;
		ret["success"] = true;
		ret["message"] = "";
	}

	return ret;
}