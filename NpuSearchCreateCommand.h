#ifndef NPUSEARCHCREATECOMMAND_H
#define NPUSEARCHCREATECOMMAND_H

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuSearchCreateCommand : public INpuCtlCommand {
public:
    NpuSearchCreateCommand(NpuExtractContainer* ext_container);
    virtual ~NpuSearchCreateCommand();

    virtual Json::Value process(const Json::Value& json_data);

private:
    NpuExtractContainer* _ext_container;
};

#endif
