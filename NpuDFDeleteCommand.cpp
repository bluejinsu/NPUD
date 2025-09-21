#include "NpuDFDeleteCommand.h"

#include "NpuExtractContainer.h"

NpuDFDeleteCommand::NpuDFDeleteCommand(NpuExtractContainer* ext_container)
    : _ext_container(ext_container)
{}

NpuDFDeleteCommand::~NpuDFDeleteCommand()
{}

Json::Value NpuDFDeleteCommand::process(const Json::Value& json_data) {
    Json::Value ret;

    std::string guid = json_data["guid"].asString();
    if (!_ext_container->deleteDFJob(guid)) {
        ret["success"] = false;
        ret["message"] = "Cannot delete df job";
        return ret;
    } else {
        ret["success"] = true;
        ret["message"] = "";
    }

    return ret;
}
