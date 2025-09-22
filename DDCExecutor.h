#ifndef DDC_EXECUTOR_H		// 250919 03:36
#define DDC_EXECUTOR_H

#include "DataStorageInfo.h"
#include "DDCHandler.h"
#include "NpuExtractInfo.h"

#include "pfb_iq.h"
#include "RWLockBuffer.hpp"

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <fstream>
#include <memory>

// [PATCH] 추가: 링버퍼/프리오픈용
#include "SpscRing.h"
#include <atomic>
#include <fstream>
#include <thread>
#include <vector>

// 중복 include 정리 가능하지만 유지
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <memory>

// [ADD]
#include <chrono>   // waitUntilReadable 인자 타입용

struct stStreamHeader {
    unsigned int stream_id;
    unsigned short stream_type;
    unsigned short data_type;
    unsigned int stream_addr;
    unsigned int stream_size;
    int antenna;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int nano;
};

enum channel {
    Ach = 2,
    Bch = 3
};

class DDCExecutor {
private:
    boost::asio::io_service ios;
    boost::asio::io_service::work _work;
    std::unique_ptr<boost::thread> _t;

    boost::mutex _mtx;
    std::unique_ptr<std::thread> _thread;
    bool _running;

    std::unique_ptr<RWLockBuffer> buffer1;
    std::unique_ptr<RWLockBuffer> buffer2;
    RWLockBuffer* _active_buffer;

    RWLockBuffer* _active_df_buffer;
    std::unique_ptr<RWLockBuffer> df_buffer1;
    std::unique_ptr<RWLockBuffer> df_buffer2;

    boost::asio::streambuf buf;

    int* _pOutIQComplex;
    std::shared_ptr<cuDDCIQComplex<int>> master;

    int* _pOutSecondaryIQComplex;
    std::shared_ptr<cuDDCIQComplex<int>> secondary;

    time_t _starttime;
    time_t _endtime;
    int64_t _frequency;
    int _bandwidth;
    int64_t _read_samples;

    int start_offset_time;
    int end_offset_time;

    std::string storage_dir;

    int64_t _fs;
    int header_size;
    int frame_samples;
    int body_size;
    int frame_count_in_file;

    int samples;
    int ddc_short_sample_size;
    int ddc_sample_size;

    int64_t source_freq;

    int bandwidth;
    float freq_offset;
    
    int _decirate;
    int _channel_samplerate;
    int _channel_bandwidth;

    unsigned int ddc_samples;

    int _file_index;

    std::ifstream inFile;
    int _frames_in_file;
    int _frame_index;
    int _frames_to_collect;
    int _total_frames_to_collect;

    // ------------------------------------------------------------------
    // [PATCH] 링버퍼 기반 비동기 I/O + 프리오픈(Pre-open)
    // ------------------------------------------------------------------
    std::unique_ptr<SpscRing<uint8_t>> _ring;   // 바이트 링버퍼
    std::atomic<bool> _ioDone{false};           // I/O 스레드 종료 플래그
    std::thread _readerThread;                  // I/O 리더 스레드
    size_t _ring_capacity{0};                   // [ADD] 생성 시점 용량 저장

    // 프리오픈용 파일 핸들
    std::ifstream _curFile;
    std::ifstream _nextFile;

    // 파일 인덱스/오프셋 관리
    int _first_file{0};
    int64_t _samples_in_file{0};
    int64_t _start_n{0};
    int _frame_offset_in_file{0};
    int _frames_in_file_left{0};
    int _next_file_index{-1};

    // 일괄 읽기 크기 및 임시 버퍼
    int _read_batch_frames{32};
    std::vector<uint8_t> _tmp;

    // Reader loop helpers
    std::string makeFilePath(int index);
    bool openCurrentFileIfNeeded();
    void preopenNextFile();
    void swapToNextFile();
    void bulkReadFramesIntoRing();
    void bulkReadFramesIntoRing_Chunked();      // <-- [ADD] 선언 추가
    void readerLoopWithPreopen();

private:
    std::shared_ptr<cuDDCIQComplex<int>> createDDCIQComplex(
        int source_samplerate, int bandwidth, int samples, float freq_offset,
        int& decirate, int& channel_bandwidth);

public:
    DDCExecutor();
    ~DDCExecutor();
    enum { DDC_EXEC_AUDIO=0, DDC_EXEC_WATCH=1, DDC_EXEC_EXTRACT=2 };
    bool executeDDC(IDDCHandler* ddc_handler, int type);

    bool initDDC(const time_t starttime, const time_t endtime, const int64_t frequency, const int bandwidth, const DataStorageInfo* dat_storage_info);
    bool initSecondaryDDC(const time_t starttime, const time_t endtime, const int64_t frequency, const int bandwidth, const DataStorageInfo* dat_storage_info);
    bool executeDDC(IDDCHandler* ddc_handler);
    bool getRunning();
    bool executeDDCDF(IDDCHandler* ddc_handler);
    void close();
    void stop();

    int getDecimateRate()  { return _decirate; }
    unsigned int getChannelSamplerate() { return _channel_samplerate; }
    unsigned int getChannelBandwidth() { return _channel_bandwidth; }
    unsigned int getDDCSamples() { return ddc_samples; }

    // --- Prefill 유틸 ---
    bool ioDone() const { return _ioDone.load(std::memory_order_acquire); }
    size_t ringReadable() const { return _ring ? _ring->readable() : 0; }
    size_t ringCapacity() const; // [ADD] 선언
    bool waitUntilReadable(size_t wantBytes, std::chrono::milliseconds timeout); // [ADD] 선언

    void parseStreamHeader(stStreamHeader& header, const char* frame_ptr);
    std::shared_ptr<std::vector<int>> matchAchFrameSync(int antNum, int totalFrameCount, int * currentFrameCount, const char * data, int64_t& time);
    std::shared_ptr<std::vector<int>> matchBchFrameSync(int antNum, int totalFrameCount, int * currentFrameCount, const char * data, int64_t& time);
};

#endif
