#include "NpuExtractListCommand.h"

#include "NpuExtractContainer.h"

NpuExtractListCommand::NpuExtractListCommand(NpuExtractContainer* ext_container)
	: _ext_container(ext_container)
{
}

NpuExtractListCommand::~NpuExtractListCommand() {

}

Json::Value NpuExtractListCommand::process(const Json::Value& json_data) {
	time_t starttime = json_data["starttime"].asInt64();
	time_t endtime = json_data["endtime"].asInt64();

	auto ext_list = _ext_container->getExtractList(starttime, endtime);

	Json::Value ret;
	for (auto it = ext_list.begin(); it != ext_list.end(); it++) {
		Json::Value json_ext_info;
		json_ext_info["guid"] = it->guid;
		json_ext_info["frequency"] = it->frequency;
		json_ext_info["bandwidth"] = it->bandwidth;
		json_ext_info["starttime"] = it->starttime;
		json_ext_info["endtime"] = it->endtime;
        json_ext_info["progress"] = it->progress;

		Json::Value files(Json::arrayValue);
		for (const auto& file : it->files) {
			files.append(file);
		}

		json_ext_info["files"] = files;
		ret.append(json_ext_info);
	}

	return ret;
}
