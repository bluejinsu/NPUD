#include "NpuDFListCommand.h"

#include "NpuExtractContainer.h"

NpuDFListCommand::NpuDFListCommand(NpuExtractContainer* ext_container)
    : _ext_container(ext_container)
{}

NpuDFListCommand::~NpuDFListCommand() {}

Json::Value NpuDFListCommand::process(const Json::Value &json_data) {
    Json::Value ret;
    ret["command"] = "df-list";
    ret["list"] = Json::Value(Json::arrayValue);

    auto df_list = _ext_container->getDFList();
    for (auto df_info : df_list) {
        Json::Value item;

        item["guid"] = df_info.guid;

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
