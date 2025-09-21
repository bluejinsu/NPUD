#ifndef NPUDFCREATECOMMAND_H
#define NPUDFCREATECOMMAND_H


#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuDFCreateCommand : public INpuCtlCommand {
public:
    NpuDFCreateCommand(NpuExtractContainer* ext_container);
    virtual ~NpuDFCreateCommand();

    virtual Json::Value process(const Json::Value& json_data);

private:
    NpuExtractContainer* _ext_container;
};

#endif
