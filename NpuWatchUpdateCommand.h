#ifndef NPU_WATCH_UPDATE_COMMAND_H
#define NPU_WATCH_UPDATE_COMMAND_H

#include "INpuCtlCommand.h"

class NpuWatchUpdateCommand : public INpuCtlCommand {
public:
    NpuWatchUpdateCommand() {}
    virtual ~NpuWatchUpdateCommand() {}

    virtual Json::Value process(const Json::Value& json_data) {
        Json::Value ret;
        ret["success"] = true;
        ret["message"] = "NpuWatchUpdateCommand";
        return ret;
    }
};

#endif