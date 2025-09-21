#include "NpuDFCreateCommand.h"

#include "NpuExtractContainer.h"
//#include "NpuSearchRequest.h"

NpuDFCreateCommand::NpuDFCreateCommand(NpuExtractContainer* ext_container)
    : _ext_container(ext_container)
{
}

NpuDFCreateCommand::~NpuDFCreateCommand() {}

Json::Value NpuDFCreateCommand::process(const Json::Value& json_data) {
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
        ret["command"] = "DF-create";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "Bandwidth is not valid";

        return ret;
    } else if (starttime == endtime) {
        ret["command"] = "DF-create";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "Duration time is not valid";

        return ret;
    }

    NpuDFRequest df_req = {
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



    std::string guid = _ext_container->createDFJob(df_req);

    if (guid == "")
    {
        ret["command"] = "DF-create";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "Failed to create df job";
    }
    else
    {
        ret["command"] = "DF-create";
        ret["guid"] = guid;
        ret["success"] = true;
        ret["message"] = "success";
    }

    return ret;
}
// ./npuctl df-create -u administrator -f 90000000 -b 320000 -s 20250812205530 -e 20250812205710 -h -65 -t 16tr
// http://192.168.30.64:3000/npu/df-create?userid=administrator&startfrequency=90000000&bandwidth=320000&starttime=20250812205500&endtime=20250812205700&threshold=-65&filetype=16tr
