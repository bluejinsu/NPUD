#ifndef INPU_CTL_COMMAND
#define INPU_CTL_COMMAND

#include <json/json.h>

class INpuCtlCommand {
public:
	virtual Json::Value process(const Json::Value& json_data) = 0;
};

#endif