#include "NpuCtlCommandHandler.h"

#include "NpuExtractCreateCommand.h"
#include "NpuExtractDeleteCommand.h"
#include "NpuExtractListCommand.h"
#include "NpuExtractUpdateCommand.h"
#include "NpuPlayAudioContainer.h"
#include "NpuPlayAudioCreateCommand.h"
#include "NpuPlayAudioDeleteCommand.h"
#include "NpuWatchCreateCommand.h"
#include "NpuWatchDeleteCommand.h"
#include "NpuWatchInfoCommand.h"
#include "NpuWatchListCommand.h"
#include "NpuWatchUpdateCommand.h"

#include <boost/make_unique.hpp>

NpuCtlCommandHandler::NpuCtlCommandHandler(NpuExtractContainer* ext_container, NpuPlayAudioContainer* play_audio_container) {
    _command_table["extract-list"] = boost::make_unique<NpuExtractListCommand>(ext_container);
    _command_table["extract-create"] = boost::make_unique<NpuExtractCreateCommand>(ext_container);
    _command_table["extract-update"] = boost::make_unique<NpuExtractUpdateCommand>();
    _command_table["extract-delete"] = boost::make_unique<NpuExtractDeleteCommand>(ext_container);

    _command_table["play-audio-create"] = boost::make_unique<NpuPlayAudioCreateCommand>(play_audio_container);
    _command_table["play-audio-delete"] = boost::make_unique<NpuPlayAudioDeleteCommand>(play_audio_container);

    _command_table["watch-info"] = boost::make_unique<NpuWatchInfoCommand>(ext_container);
    _command_table["watch-create"] = boost::make_unique<NpuWatchCreateCommand>(ext_container);
    _command_table["watch-delete"] = boost::make_unique<NpuWatchDeleteCommand>(ext_container);
    _command_table["watch-list"] = boost::make_unique<NpuWatchListCommand>(ext_container);
    _command_table["watch-update"] = boost::make_unique<NpuWatchUpdateCommand>();
}

NpuCtlCommandHandler::~NpuCtlCommandHandler() {

}

bool NpuCtlCommandHandler::canHandle(const Json::Value& json_data) {
	if (!json_data.isMember("command")) {
		return false;
	}

	if (!json_data["command"].isString()) {
		return false;
	}
	std::string command_key = json_data["command"].asString();
	auto it = _command_table.find(command_key);
	if (it == _command_table.end()) {
		return false;
	}

	return true;
}

Json::Value NpuCtlCommandHandler::process(const Json::Value& json_data) {
    if (!json_data.isMember("command")) {
        Json::Value ret_error;
        ret_error["success"] = false;
        ret_error["message"] = "'command' property is not found";
        return ret_error;
    }

    if (!json_data["command"].isString()) {
        Json::Value ret_error;
        ret_error["success"] = false;
        ret_error["message"] = "'command' property is invalid";
        return ret_error;
    }
    std::string command_key = json_data["command"].asString();
    auto it = _command_table.find(command_key);
    if (it == _command_table.end()) {
        Json::Value ret_error;
        ret_error["success"] = false;
        ret_error["message"] = "Unknown command";
        return ret_error;
    }

    return it->second->process(json_data);
}
