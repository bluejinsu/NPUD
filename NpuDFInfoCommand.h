#ifndef NPUDFINFOCOMMAND_H
#define NPUDFINFOCOMMAND_H


#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuDFInfoCommand : public INpuCtlCommand
{
public:
    NpuDFInfoCommand(NpuExtractContainer* ext_container);

    // INpuCtlCommand interface
public:
    Json::Value process(const Json::Value &json_data);

private:
    NpuExtractContainer* _ext_container;
};



#endif
