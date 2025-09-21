#include "NpuExtractUpdateCommand.h"

#include <json/json.h>

NpuExtractUpdateCommand::NpuExtractUpdateCommand() {

}

NpuExtractUpdateCommand::~NpuExtractUpdateCommand() {

}

Json::Value NpuExtractUpdateCommand::process(const Json::Value& json_data) {
	Json::Value ret;

	ret["success"] = false;
	ret["message"] = "Not yet implemented";

	return ret;
}