#include "NpuServer.h"

#include "NpuCtlSession.h"
#include "NpuExtractContainer.h"
#include "NpuPlayAudioContainer.h"

#include <memory>

NpuServer::NpuServer(boost::asio::io_context& io_context, short port, NpuExtractContainer* ext_container, NpuPlayAudioContainer* play_audio_container)
    : _acceptor(io_context, tcp::endpoint(tcp::v4(), port))
    , _command_handler(ext_container, play_audio_container)
{
    asyncAccept();
}

void NpuServer::asyncAccept() {
    _acceptor.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<NpuCtlSession>(std::move(socket), &_command_handler)->start();
            }
            asyncAccept();
        });
}
