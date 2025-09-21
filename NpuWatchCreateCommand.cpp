#include "NpuWatchCreateCommand.h"

#include "NpuExtractContainer.h"
#include "NpuWatchRequest.h"

NpuWatchCreateCommand::NpuWatchCreateCommand(NpuExtractContainer* ext_container) 
    : _ext_container(ext_container)
{
}

NpuWatchCreateCommand::~NpuWatchCreateCommand() {}

Json::Value NpuWatchCreateCommand::process(const Json::Value& json_data) {
    int instance = json_data["instance"].asInt();
	int64_t frequency = json_data["frequency"].asInt64();
	int bandwidth = json_data["bandwidth"].asInt();
	time_t starttime = json_data["starttime"].asInt64();
	time_t endtime = json_data["endtime"].asInt64();
	std::string filetype = json_data["filetype"].asString();
    double threshold = json_data["threshold"].asDouble();

    int holdtime = 1;
    int continue_time = 9999;


    Json::Value ret;

    if (bandwidth == 0) {
        ret["command"] = "watch-create";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "Bandwidth is not valid";

        return ret;
    } else if (starttime == endtime) {
        ret["command"] = "watch-create";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "Duration time is not valid";

        return ret;
    }

    NpuWatchRequest watch_req = {
		instance,
		frequency,
		bandwidth,
		starttime,
		endtime,
        filetype,
        threshold,
        holdtime,
        continue_time
    };


    std::string guid = _ext_container->createWatchJob(watch_req);
	if (guid == "") {
        ret["command"] = "watch-create";
        ret["guid"] = "";
		ret["success"] = false;
		ret["message"] = "Failed to craete extract job";
	}
	else {
        ret["command"] = "watch-create";
		ret["guid"] = guid;
		ret["success"] = true;
		ret["message"] = "";
	}

	return ret;
}
