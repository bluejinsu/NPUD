#ifndef NPUDFDELETECOMMAND_H
#define NPUDFDELETECOMMAND_H

#include "INpuCtlCommand.h"

class NpuExtractContainer;

class NpuDFDeleteCommand : public INpuCtlCommand {
public:
    NpuDFDeleteCommand(NpuExtractContainer* ext_container);
    virtual ~NpuDFDeleteCommand();

    virtual Json::Value process(const Json::Value& json_data);

private:
    NpuExtractContainer* _ext_container;
};

#endif // NPUDFDELETECOMMAND_H
