
#include "NpuDFInfoCommand.h"

#include "NpuExtractContainer.h"
#include "NpuDFInfo.h"

NpuDFInfoCommand::NpuDFInfoCommand(NpuExtractContainer* ext_container)
    : _ext_container(ext_container)
{

}

Json::Value NpuDFInfoCommand::process(const Json::Value &json_data) {
    std::string guid = json_data["guid"].asString();

    Json::Value ret;

    NpuDFInfo df_info;
    if (_ext_container->getDFInfo(guid, df_info)) {
        ret["command"] = "df-info";
        ret["guid"] = df_info.guid;
        ret["starttime"] = df_info.starttime;
        ret["endtime"] = df_info.endtime;
        ret["frequency"] = df_info.frequency;
        ret["bandwidth"] = df_info.bandwidth;
        ret["samplerate"] = df_info.samplerate;
        ret["threshold"] = df_info.threshold;
        ret["progress"] = df_info.progress;

        Json::Value arr(Json::arrayValue);
        for (const auto& d : df_info.dfResults) {
            Json::Value item;
            item["frequency"] = d.frequency;
            item["bandwidth"] = d.bandwidth;
            item["startTime"] = (Json::Int64)d.startTime;  // time_t → Int64
            item["endTime"]   = (Json::Int64)d.endTime;
            item["Azimuth"]   = d.Azimuth;
            arr.append(item);
        }
        ret["dfResults"] = arr;


        ret["success"] = true;
        ret["message"] = "";
    } else {
        ret["command"] = "df-info";
        ret["guid"] = "";
        ret["success"] = false;
        ret["message"] = "No df-info";
    }

    return ret;
}
