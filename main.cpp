#include "NpuConfigure.h"
#include "NpuExtractContainer.h"
#include "NpuPlayAudioContainer.h"
#include "NpuServer.h"
#include "OracleDBAccess.h"
#include "pfb_iq.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <utility>

#include <getopt.h>
#include <inttypes.h>
#include <time.h>

bool initializeGpu() {
    cuStreamPool_init();
    
    if (cudaSuccess != initNCOTable()) {
        std::cerr << "initNCOTable return false" << std::endl;
        return false;
    }

    return true;
}

void signalHandler(const boost::system::error_code& error, int signal_number, boost::asio::io_context& io_context) {
    if (!error) {
        std::cout << "Received signal: " << signal_number << ", stopping io_context..." << std::endl;
        io_context.stop();
    }
}

int main(int argc, char** argv) {
    if (!initializeGpu()) {
        std::cerr << "Failed to initialize GPU" << std::endl;
        return -1;
    }

    std::string config_file_path = "config.json";

    struct option long_options[] = {
      {"config", required_argument, 0, 'c'},
      {0, 0, 0, 0}
    };

    int opt;
    int opt_index = 0;
    while ((opt = getopt_long(argc, argv, "c:", long_options, &opt_index)) != -1) {
        switch (opt) {
            case 'c':
                config_file_path = optarg;
                break;
            default:
                std::cerr << "Usage: " << argv[0] << " [-c Config file]" << std::endl;
                return 1;
        }
    }

    NpuConfigure config;
    if (!config.load(config_file_path)) {
        std::cerr << "exit(-1) : Failed to load " << config_file_path << std::endl;
        return -1;
    }

    NpuExtractContainer ext_container(&config);

    NpuPlayAudioContainer play_audio_container(&config);
    
    try {
        boost::asio::io_context io_context;

        // Boost ASIO 신호 세터 생성
        boost::asio::signal_set signals(io_context, SIGTERM, SIGINT);

        // 신호 처리 핸들러 등록
        signals.async_wait([&](const boost::system::error_code& error, int signal_number) {
            signalHandler(error, signal_number, io_context);
        });

        short port = atoi(config.getValue("SERVER.PORT").c_str());
        NpuServer server(io_context, port, &ext_container, &play_audio_container);

        std::cout << "NpuServer started on port " << port << std::endl;

        io_context.run();
    }
    catch (std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << "\n";
    }

#if 0
    ExtractionInfo ext_info;
    ext_info.instance = 0;
    ext_info.starttime = localToUtcTimestamp("2024-10-24 12:09:33");
    ext_info.endtime = ext_info.starttime + 10;
    ext_info.frequency = 101900000;

    requestHandler(ext_info);
#endif

    return 0;
}
