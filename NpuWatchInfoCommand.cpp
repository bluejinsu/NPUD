#include "NpuWatchInfoCommand.h"

#include "NpuExtractContainer.h"
#include "NpuWatchInfo.h"

NpuWatchInfoCommand::NpuWatchInfoCommand(NpuExtractContainer* ext_container)
    : _ext_container(ext_container)
{

}

Json::Value NpuWatchInfoCommand::process(const Json::Value &json_data) {
    std::string guid = json_data["guid"].asString();

    Json::Value ret;

    NpuWatchInfo watch_info;
    if (_ext_container->getWatchInfo(guid, watch_info)) {
        ret["command"] = "watch-info";
        ret["guid"] = watch_info.guid;
        ret["starttime"] = watch_info.starttime;
        ret["endtime"] = watch_info.endtime;
        ret["frequency"] = watch_info.frequency;
        ret["bandwidth"] = watch_info.bandwidth;
        ret["samplerate"] = watch_info.samplerate;
        ret["threshold"] = watch_info.threshold;
        ret["progress"] = watch_info.progress;

        ret["success"] = true;
        ret["message"] = "";
    } else {
        ret["command"] = "watch-info";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "No watch-info";
    }

    return ret;
}
