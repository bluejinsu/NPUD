#include "RestClient.h"

#include <iostream>

size_t RestClient::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    size_t totalSize = size * nmemb;
    response->append((char*)contents, totalSize);
    return totalSize;
}

bool RestClient::parseJson(const std::string& response, Json::Value& jsonData) {
    Json::CharReaderBuilder readerBuilder;
    std::string errs;
    std::istringstream s(response);
    return Json::parseFromStream(readerBuilder, s, &jsonData, &errs);
}

RestClient::RestClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
}

RestClient::~RestClient() {
    if (curl) curl_easy_cleanup(curl);
    curl_global_cleanup();
}

// GET ��û ����

bool RestClient::get(const std::string& url, Json::Value& jsonData) {
    if (!curl) return false;

    std::string response;
    // URL ����
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // ���� ������ �ݹ� ����
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // GET ��û ����
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    // JSON �Ľ�
    return parseJson(response, jsonData);
}
