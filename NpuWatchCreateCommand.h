#ifndef NPU_WATCH_CREATE_COMMAND_H
#define NPU_WATCH_CREATE_COMMAND_H

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuWatchCreateCommand : public INpuCtlCommand {
public:
    NpuWatchCreateCommand(NpuExtractContainer* ext_container);
    virtual ~NpuWatchCreateCommand();

    virtual Json::Value process(const Json::Value& json_data);

private:
    NpuExtractContainer* _ext_container;
};

#endif