#include "NpuCtlCommandHandler.h"

#include <boost/asio.hpp>

using namespace boost::asio::ip;

class NpuExtractContainer;
class NpuPlayAudioContainer;

class NpuServer {
public:
    NpuServer(boost::asio::io_context& io_context, short port, NpuExtractContainer* ext_container, NpuPlayAudioContainer* play_audio_container);

private:
    tcp::acceptor _acceptor;
    NpuCtlCommandHandler _command_handler;

    void asyncAccept();
};
