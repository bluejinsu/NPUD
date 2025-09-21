#include "NpuWatchDeleteCommand.h"

#include "NpuExtractContainer.h"

NpuWatchDeleteCommand::NpuWatchDeleteCommand(NpuExtractContainer* ext_container)
    : _ext_container(ext_container)
{}

NpuWatchDeleteCommand::~NpuWatchDeleteCommand()
{}

Json::Value NpuWatchDeleteCommand::process(const Json::Value& json_data) {
    Json::Value ret;

    std::string guid = json_data["guid"].asString();
    if (!_ext_container->deleteWatchJob(guid)) {
        ret["success"] = false;
        ret["message"] = "Cannot delete watch job";
        return ret;
    } else {
        ret["success"] = true;
        ret["message"] = "";
    }

    return ret;
}