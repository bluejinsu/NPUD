#ifndef NPUDFLISTCOMMAND_H
#define NPUDFLISTCOMMAND_H

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuDFListCommand : public INpuCtlCommand {
public:
    NpuDFListCommand(NpuExtractContainer* ext_container);
    virtual ~NpuDFListCommand();

    virtual Json::Value process(const Json::Value& json_data);

private:
    NpuExtractContainer* _ext_container;
};

#endif
