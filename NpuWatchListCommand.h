#ifndef NPU_WATCH_LIST_COMMAND_H
#define NPU_WATCH_LIST_COMMAND_H

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuWatchListCommand : public INpuCtlCommand {
public:
    NpuWatchListCommand(NpuExtractContainer* ext_container);
    virtual ~NpuWatchListCommand();

    virtual Json::Value process(const Json::Value& json_data);

private:
    NpuExtractContainer* _ext_container;
};

#endif
