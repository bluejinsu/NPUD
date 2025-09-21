#ifndef NPU_SESSION_H
#define NPU_SESSION_H

#include <boost/asio.hpp>
#include <json/json.h>

#include <iostream>
#include <memory>

using boost::asio::ip::tcp;

class NpuCtlCommandHandler;

class NpuCtlSession : public std::enable_shared_from_this<NpuCtlSession> {
private:
    tcp::socket socket_;

    static const int BUFFER_SIZE = 1024;
    char _buffer[BUFFER_SIZE];

    std::string _data_buffer;

    NpuCtlCommandHandler* _command_handler;

public:
    NpuCtlSession(tcp::socket socket, NpuCtlCommandHandler* command_handler);
    ~NpuCtlSession();

    void start();

private:
    void aysnRead();
    void handleCommand(const Json::Value& json_data);
    void sendResponse(const Json::Value& json_data);
};

#endif