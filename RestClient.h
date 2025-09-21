#ifndef REST_CLIENT_H
#define REST_CLIENT_H

#include <string>

#include <curl/curl.h>
#include <json/json.h>

class RestClient {
private:
    CURL* curl;

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* response);

    bool parseJson(const std::string& response, Json::Value& jsonData);

public:
    RestClient();
    ~RestClient();

    // GET 요청 수행
    bool get(const std::string& url, Json::Value& jsonData);
};

#endif