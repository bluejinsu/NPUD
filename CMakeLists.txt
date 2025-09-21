cmake_minimum_required(VERSION 3.14 FATAL_ERROR)
project(NeosProcessingUnitDaemon LANGUAGES CXX CUDA)

add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

set(EXECUTION_NAME npud)
set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_INCLUDE_CURRENT_DIR ON)

# CUDA 라이브러리
set(CMAKE_CUDA_ARCHITECTURES 75)
find_package(CUDAToolkit REQUIRED)


set(OpenCV_STATIC ON)
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include directories: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")


# Boost 라이브러리
set(BOOST_ROOT "c:\\local\\boost_1_72_0")
# add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)
find_package(Boost 1.72.0 REQUIRED COMPONENTS system thread filesystem)

link_directories(${Boost_LIBRARY_DIRS})

find_package(OpenSSL REQUIRED)

include(GNUInstallDirs)
include(fftw.cmake)
include(la_ddc.cmake)
include(jsoncpp.cmake)
include(curl.cmake)
include(oracle.cmake)
include(liveMedia.cmake)
include(samplerate.cmake)

include_directories(${Boost_INCLUDE_DIRS}
                    ${CMAKE_CURRENT_SOURCE_DIR}/include
                    ${CUDA_INCLUDE_DIRS}
                    ${FFTW_DIR}
                    ${JSONCPP_DIR}/include
                    ${CURL_DIR}/include
                    ${ORACLE_INCLUDE_DIR}
                    /usr/local/include/opencv4
)


# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Sources
set(SOURCES
    main.cpp
    AutoGainControl.cpp
    DataStorageSearcher.cpp
    DDCExecutor.cpp
    FFTExecutor.cpp
    FirFilter.cpp
    HilbertFirFilter.cpp
    IFDataFormatEncoder.cpp
    NpuBasicFileNameFormatter.cpp
    NpuConfigure.cpp
    NpuCtlCommandHandler.cpp
    NpuCtlSession.cpp
    NpuExtract16tWriter.cpp
    NpuExtractContainer.cpp
    NpuExtractCreateCommand.cpp
    NpuExtractDeleteCommand.cpp
    NpuExtractJob.cpp
    NpuExtractListCommand.cpp
    NpuExtractResult.cpp
    NpuExtractUpdateCommand.cpp
    NpuExtractWavWriter.cpp
    NpuPlayAudioContainer.cpp
    NpuPlayAudioCreateCommand.cpp
    NpuPlayAudioDeleteCommand.cpp
    NpuPlayAudioJob.cpp
    NpuWatchCreateCommand.cpp
    NpuWatchDeleteCommand.cpp
    NpuWatchInfoCommand.cpp
    NpuWatchJob.cpp
    NpuWatchListCommand.cpp
    NpuWatchProcess.cpp
    NpuWatchUpdateCommand.cpp
    NpuServer.cpp
    OracleDBAccess.cpp
    RestClient.cpp
)

# Headers
set(HEADERS
    AutoGainControl.h
    DataStorageSearcher.h
    DDCExecutor.h
    FFTExecutor.h
    FirFilter.h
    HilbertFirFilter.h
    IFDataFormatEncoder.h
    IDBAccess.h
    INpuCtlCommand.h
    INpuExtractFileWriter.h
    INpuFileNameFormatter.h
    NpuBasicFileNameFormatter.h
    NpuConfigure.h
    NpuCtlCommandHandler.h
    NpuCtlSession.h
    NpuExtract16tWriter.h
    NpuExtractContainer.h
    NpuExtractCreateCommand.h
    NpuExtractDeleteCommand.h
    NpuExtractInfo.h
    NpuExtractJob.h
    NpuExtractListCommand.h
    NpuExtractResult.h
    NpuExtractRequest.h
    NpuExtractUpdateCommand.h
    NpuExtractWavWriter.h
    NpuPlayAudioContainer.h
    NpuPlayAudioCreateCommand.h
    NpuPlayAudioDeleteCommand.h
    NpuPlayAudioJob.h
    NpuPlayAudioRequest.h
    NpuWatchCreateCommand.h
    NpuWatchDeleteCommand.h
    NpuWatchInfoCommand.h
    NpuWatchJob.h
    NpuWatchListCommand.h
    NpuWatchProcess.h
    NpuWatchRequest.h
    NpuWatchUpdateCommand.h
    NpuServer.h
    OracleDBAccess.h
    RestClient.h
    RWLockBuffer.hpp
    StreamingResampler.hpp
    util.hpp
)

add_executable(${EXECUTION_NAME} ${SOURCES} ${HEADERS})

# target_link_libraries(${EXECUTION_NAME} ${LA_DDC_LIBRARIES} ${FFTW_LIBRARIES} ${JSONCPP_LIBRARIES} ${CURL_LIBRARIES} ${ORACLE_LIBRARIES} ${CUDA_LIBRARIES} ${CUDA_CUFFT_LIBRARIES})
target_link_libraries(${EXECUTION_NAME} PRIVATE
                                        
                                        ${FFTW_LIBRARIES}
                                        ${JSONCPP_LIBRARIES}
                                        ${CURL_LIBRARY}
                                        ${SAMPLERATE_LIBRARY}
                                        ${LA_DDC_LIB}
                                        ${ORACLE_LIBRARIES}
                                        pthread
                                        Boost::thread 
                                        Boost::filesystem 
                                        CUDA::cudart
                                        ${LIVE_MEDIA_LIBRARIES}
                                        OpenSSL::SSL
                                        ${OpenCV_LIBS}
                                        )

# CUDA 헤더 경로 추가 (필요시 명시적으로 추가)
target_include_directories(${EXECUTION_NAME} PRIVATE /home/administrator/src/la_ddc/include)
target_include_directories(${EXECUTION_NAME} PRIVATE ${CUDAToolkit_INCLUDE_DIRS})

install(TARGETS ${EXECUTION_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
