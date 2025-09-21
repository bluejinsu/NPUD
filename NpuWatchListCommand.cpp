#include "NpuWatchListCommand.h"

#include "NpuExtractContainer.h"

NpuWatchListCommand::NpuWatchListCommand(NpuExtractContainer* ext_container) 
    : _ext_container(ext_container)
 {}

NpuWatchListCommand::~NpuWatchListCommand() {}

Json::Value NpuWatchListCommand::process(const Json::Value &json_data) {
    Json::Value ret;
    ret["command"] = "watch-list";
    ret["list"] = Json::Value(Json::arrayValue);
    
    auto watch_list = _ext_container->getWatchList();
    for (auto watch_info : watch_list) {
        Json::Value item;

        item["guid"] = watch_info.guid;

#if 0
        item["starttime"] = watch_info.starttime;
        item["endtime"] = watch_info.endtime;
        item["frequency"] = watch_info.frequency;
        item["bandwidth"] = watch_info.bandwidth;
        item["samplerate"] = watch_info.samplerate;
        item["threshold"] = watch_info.threshold;
        item["progress"] = watch_info.progress;
#endif        
        ret["success"] = true;
        ret["message"] = "";
        ret["list"].append(item);
    }

    return ret;
}
