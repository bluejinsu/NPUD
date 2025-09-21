#pragma once

#include <json/json.h>

#include <map>
#include <string>

class NpuConfigure
{
private:
	std::map<std::string, std::string> config_map;

private:
	void parseJson(const Json::Value& node, const std::string& parentKey = "");

public:
	NpuConfigure();
	~NpuConfigure();

	bool load(std::string config_file_path);
	std::string getValue(const std::string& key) const;
};

