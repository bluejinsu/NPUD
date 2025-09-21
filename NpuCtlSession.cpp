#include "NpuCtlSession.h"

#include "NpuCtlCommandHandler.h"

NpuCtlSession::NpuCtlSession(tcp::socket socket, NpuCtlCommandHandler* command_handler)
    : socket_(std::move(socket)) 
    , _command_handler(command_handler)
{}

NpuCtlSession::~NpuCtlSession() {

}

void NpuCtlSession::start() {
    aysnRead();
}

void NpuCtlSession::aysnRead() {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(_buffer, sizeof(_buffer) - 1),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                _buffer[length] = '\0';  // null-terminate the buffer

                _data_buffer += std::string(_buffer, length);

                Json::CharReaderBuilder reader_builder;
                Json::Value json_data;
                std::string errs;

                std::istringstream stream(_data_buffer);
                if (Json::parseFromStream(reader_builder, stream, &json_data, &errs)) {
                    // std::cout << ">>> request: " << json_data.toStyledString() << std::endl;
                    handleCommand(json_data);
                    return;
                }

                aysnRead();
            }
        });
}

void NpuCtlSession::sendResponse(const Json::Value& json_data) {
    Json::StreamWriterBuilder writer;
    std::string response = Json::writeString(writer, json_data);

    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(response, response.length()),
        [this, self, response](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                // std::cout << "<<< response: " << response << std::endl;
            }
            else {
                perror("send failed");
            }
        });
}

void NpuCtlSession::handleCommand(const Json::Value& json_data) {
    if (!_command_handler->canHandle(json_data)) {
        Json::Value ret_err;
        ret_err["success"] = false;
        ret_err["message"] = "Not supported command";

        sendResponse(ret_err);
        return;
    }
    auto ret_value = _command_handler->process(json_data);
    sendResponse(ret_value);
}
