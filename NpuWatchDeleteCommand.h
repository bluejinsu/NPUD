#ifndef NPU_WATCH_DELETE_COMMAND_H
#define NPU_WATCH_DELETE_COMMAND_H

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuWatchDeleteCommand : public INpuCtlCommand {
public:
    NpuWatchDeleteCommand(NpuExtractContainer* ext_container);
    virtual ~NpuWatchDeleteCommand();

    virtual Json::Value process(const Json::Value& json_data);

private:
    NpuExtractContainer* _ext_container;
};

#endif