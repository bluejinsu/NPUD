#include "NpuSearchCreateCommand.h"

#include "NpuExtractContainer.h"
//#include "NpuSearchRequest.h"

NpuSearchCreateCommand::NpuSearchCreateCommand(NpuExtractContainer* ext_container)
    : _ext_container(ext_container)
{
}

NpuSearchCreateCommand::~NpuSearchCreateCommand() {}

Json::Value NpuSearchCreateCommand::process(const Json::Value& json_data) {
    int instance = json_data["instance"].asInt();
    int64_t startFrequency = json_data["startfrequency"].asInt64();
    int64_t endFrequency = json_data["endfrequency"].asInt64();
    int bandwidth = json_data["bandwidth"].asInt();
    time_t starttime = json_data["starttime"].asInt64();
    time_t endtime = json_data["endtime"].asInt64();
    std::string filetype = json_data["filetype"].asString();
    double threshold = json_data["threshold"].asDouble();

    int holdtime = 1;
    int continue_time = 9999;


    Json::Value ret;

    if (bandwidth == 0) {
        ret["command"] = "search-create";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "Bandwidth is not valid";

        return ret;
    } else if (starttime == endtime) {
        ret["command"] = "search-create";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "Duration time is not valid";

        return ret;
    } else if (startFrequency == endFrequency) {
        ret["command"] = "search-create";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "Duration time is not valid";

        return ret;
    }


    ret["command"] = "search-create";
    ret["guid"] = "1122334455";
    ret["success"] = true;
    ret["message"] = "Test OK";

    //std::string guid = _ext_container->createWatchJob(watch_req);

    return ret;
}
// ./npuctl search-create -u administrator -f 90000000 -q 1100 -b 320000 -s 20250812205530 -e 20250812205710 -t -65 -h 16tr
// http://192.168.30.64:3000/npu/search-create?userid=administrator&startfrequency=90000000&endfrequency=110000000&bandwidth=320000&starttime=20250812205500&endtime=20250812205700&threshold=-65&filetype=16tr
