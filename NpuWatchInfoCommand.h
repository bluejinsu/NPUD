#ifndef NPUWATCHINFOCOMMAND_H
#define NPUWATCHINFOCOMMAND_H

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuWatchInfoCommand : public INpuCtlCommand
{
public:
    NpuWatchInfoCommand(NpuExtractContainer* ext_container);

    // INpuCtlCommand interface
public:
    Json::Value process(const Json::Value &json_data);

private:
    NpuExtractContainer* _ext_container;
};

#endif // NPUWATCHINFOCOMMAND_H
