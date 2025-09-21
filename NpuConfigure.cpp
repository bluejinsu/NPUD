#include "NpuConfigure.h"

#include <json/json.h>

#include <fstream>
#include <iostream>


NpuConfigure::NpuConfigure() {

}

NpuConfigure::~NpuConfigure() {

}

bool NpuConfigure::load(std::string config_file_path) {
	std::ifstream config_file(config_file_path, std::ifstream::binary);
	if (!config_file.is_open()) {
		std::cerr << "Unable to open file: " << config_file_path << std::endl;
		return false;
	}

	Json::Value root;
	Json::CharReaderBuilder reader_builder;
	std::string errs;

	if (!Json::parseFromStream(reader_builder, config_file, &root, &errs)) {
		std::cerr << "Failed to parse JSON: " << errs << std::endl;
		return false;
	}

	parseJson(root);

	return true;
}

std::string NpuConfigure::getValue(const std::string& key) const {
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        return it->second;
    }
    return "";
}

void NpuConfigure::parseJson(const Json::Value& node, const std::string& parentKey) {
    for (const auto& key : node.getMemberNames()) {
        std::string fullKey = parentKey.empty() ? key : parentKey + "." + key;
        const Json::Value& value = node[key];

        if (value.isObject()) {
            // ��������� ��ü�� �Ľ�
            parseJson(value, fullKey);
        }
        else if (value.isArray()) {
            // �迭 ���� ���ڿ��� ��ȯ�Ͽ� ����
            for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
                config_map[fullKey + "[" + std::to_string(i) + "]"] = value[i].asString();
            }
        }
        else {
            // ���� ���� ���ڿ��� ����
            config_map[fullKey] = value.asString();
        }
    }
}