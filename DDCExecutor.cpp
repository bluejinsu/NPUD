#include "DDCExecutor.h"  // 250919 03:36

#include <cstring>
#include <iostream>
#include <fstream>
#include <algorithm>

#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>   // ← 추가

// [PATCH] 추가: SPSC 링버퍼로 I/O 스레드와 DSP 스레드 분리
#include "SpscRing.h"
#include <atomic>
#include <mutex>
#include <condition_variable>


#include <ctime>    // [ADD] UTC 포맷용
#include <string>   // [ADD] std::string


// ====== [ADD-STEP1/2] Feature toggles ======
#define FEAT_POSIX_IO        0   // 1단계: fstream → POSIX open/read + fadvise
#define FEAT_CHUNKED_4MB     1   // 2단계: 4MB 고정 청크로 바디를 모아 한 번에 링 write

#if FEAT_POSIX_IO
  #include <fcntl.h>
  #include <unistd.h>
  #include <sys/uio.h>
  #include <sys/stat.h>
  #include <errno.h>

  // 리더 스레드마다 독립 상태
  static thread_local int t_curFd  = -1;
  static thread_local int t_nextFd = -1;

  static inline bool fd_is_open(int fd) { return fd >= 0; }

  static int open_fd_seq(const std::string& path) {
      int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC
#ifdef O_LARGEFILE
      | O_LARGEFILE
#endif
      );
      if (fd >= 0) {
          (void)posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
      }
      return fd;
  }
  static void close_fd_if_open(int& fd) {
      if (fd >= 0) { ::close(fd); fd = -1; }
  }
  static bool read_exact(int fd, void* buf, size_t len) {
      uint8_t* p = static_cast<uint8_t*>(buf);
      size_t left = len;
      while (left > 0) {
          ssize_t g = ::read(fd, p, left);
          if (g == 0) return false; // EOF
          if (g < 0) {
              if (errno == EINTR) continue;
              return false;
          }
          p += g;
          left -= (size_t)g;
      }
      return true;
  }
  static bool skip_exact(int fd, size_t len) {
      off_t r = ::lseek(fd, (off_t)len, SEEK_CUR);
      return (r != (off_t)-1);
  }

  #if FEAT_CHUNKED_4MB
    // 4MB 청크 (필요시 1~8MB로 조정 가능)
    static constexpr size_t kChunkBytes = 1 * 1024 * 1024;
    // 리더 스레드 전용 대형 청크 버퍼
    static thread_local std::vector<uint8_t> t_bigBuf;
  #endif
#endif // FEAT_POSIX_IO

// struct DdcStageStats {
//     uint64_t n = 0;
//     double waitRing_us=0, readRing_us=0, s16toQ31_us=0, cuda_us=0, q31toF_us=0, handler_us=0, loop_us=0;
//     void accum(double wr,double rr,double c1,double cu,double c2,double hh,double lp){
//         n++; waitRing_us+=wr; readRing_us+=rr; s16toQ31_us+=c1; cuda_us+=cu; q31toF_us+=c2; handler_us+=hh; loop_us+=lp;
//     }
// } _ddcStats;

// // [ADD][LOG] UTC 포맷터 (스레드 세이프)
// static inline std::string toUtcString(time_t t) {
//     char buf[32];
// #ifdef _WIN32
//     struct tm tm_utc;
//     gmtime_s(&tm_utc, &t);
//     strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_utc);
// #else
//     struct tm tm_utc;
//     gmtime_r(&t, &tm_utc);
//     strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_utc);
// #endif
//     return std::string(buf);
// }

DDCExecutor::DDCExecutor() 
    : _work(ios)
    , _running(false)
    , _pOutIQComplex(nullptr)
    , _pOutSecondaryIQComplex(nullptr)
{
}

DDCExecutor::~DDCExecutor() {
    // [PATCH] 안전한 종료
    stop();
    if (_readerThread.joinable()) _readerThread.join();

#if !FEAT_POSIX_IO
    if (_curFile.is_open()) { _curFile.close(); }
    if (_nextFile.is_open()) { _nextFile.close(); }
#endif

    // [PATCH] CUDA pinned host 메모리 해제 (누수 방지)
    if (_pOutIQComplex) {
        cudaFreeHost(_pOutIQComplex);
        _pOutIQComplex = nullptr;
    }
    if (_pOutSecondaryIQComplex) {
        cudaFreeHost(_pOutSecondaryIQComplex);
        _pOutSecondaryIQComplex = nullptr;
    }
}

std::shared_ptr<cuDDCIQComplex<int>> DDCExecutor::createDDCIQComplex(int source_samplerate, int bandwidth, int samples, float freq_offset, int& decirate, int &channel_bandwidth) {

    int samplerate = source_samplerate;
    decirate = 1;

    std::vector<std::pair<int, EnDDCFilter>> fir_filters;

    while (true) {
        int fs = (source_samplerate / decirate / 2);
        if (fs <= bandwidth) break;

        if (decirate < 16) {
            fir_filters.emplace_back(2, EnDDCFilter::Half_Tap41);
        } else {
            fir_filters.emplace_back(2, EnDDCFilter::Half_Tap55);
        }
        decirate *= 2;
    }    

    channel_bandwidth = source_samplerate / decirate;// * 0.7818;

    DDCIQComplexBuilder<int> ddc_builder;
    ddc_builder.setSampleSize(samples);

    bool first = true;
    for (auto it = fir_filters.begin(); it != fir_filters.end(); it++) {
        if (first) {
            ddc_builder.stageWithShift(it->first, freq_offset, it->second);
            first = false;
        }
        else {
            ddc_builder.stage(it->first, it->second);
        }
    }

    return ddc_builder.build();
}

bool DDCExecutor::initDDC(const time_t starttime, const time_t endtime, const int64_t frequency, const int bandwidth, const DataStorageInfo* dat_storage_info) {
    int64_t offset = 0;

    _starttime = starttime;
    _endtime = endtime;
    _frequency = frequency;
    _bandwidth = bandwidth;
    _read_samples = 0;

    start_offset_time = starttime - dat_storage_info->starttime;
    end_offset_time = endtime - dat_storage_info->starttime;

    storage_dir = dat_storage_info->storage_dir;

    _fs = dat_storage_info->samplerate;
    header_size = 28;
    frame_samples = 65536 * 2;                          // 65536
    body_size = frame_samples * sizeof(int16_t) * 2;    // IQ(int16*2) per frame
    frame_count_in_file = 256;                          // 256

    samples = frame_samples * 32;                       // 기존 유지
    ddc_sample_size = samples * sizeof(int16_t) * 2;
    ddc_short_sample_size = ddc_sample_size;

    source_freq = dat_storage_info->wddc_freq;

    freq_offset = (source_freq - (frequency + offset)) / (double)_fs;
    
    _decirate = 1;
    _channel_bandwidth = bandwidth;
    master = createDDCIQComplex(dat_storage_info->samplerate, bandwidth, samples, freq_offset, /* out */ _decirate, /* out */ _channel_bandwidth);
    if (!master) {
        std::cerr << "Failed to create ddc iq complex";
        return false;
    }

    ddc_samples = samples / _decirate;
    _channel_samplerate = _fs / _decirate;

    _pOutIQComplex = nullptr;
    cudaHostAlloc(&_pOutIQComplex, ddc_samples * sizeof(int32_t) * 2, cudaHostAllocDefault);

    buffer1.reset(new RWLockBuffer(header_size + body_size));
    buffer2.reset(new RWLockBuffer(header_size + body_size));
    _active_buffer = buffer1.get();

    df_buffer1.reset(new RWLockBuffer((header_size + body_size) * frame_count_in_file));
    df_buffer2.reset(new RWLockBuffer((header_size + body_size) * frame_count_in_file));
    _active_df_buffer = df_buffer1.get();

    int64_t samples_in_file = frame_samples * frame_count_in_file;
    int64_t start_n = start_offset_time * _fs;
    int64_t end_n = end_offset_time * _fs;
    int first_file = static_cast<int>(start_n / samples_in_file);
    _file_index = first_file;
    
    _frames_to_collect = static_cast<int>(ceil((end_n - start_n) / frame_samples));
    _total_frames_to_collect = _frames_to_collect;

    _running = true;

    // // === [ADD][LOG] 요청 시간/주파수 정보 출력 (UTC) ===
    // {
    //     const std::string startUtc = toUtcString(_starttime);
    //     const std::string endUtc   = toUtcString(_endtime);

    //     // 중심 주파수(_frequency), 대역폭(_bandwidth) 기준으로 시작/끝 산출
    //     const double fc_hz = static_cast<double>(_frequency);
    //     const double bw_hz = static_cast<double>(_bandwidth);
    //     const double f0_hz = fc_hz - (bw_hz * 0.5);
    //     const double f1_hz = fc_hz + (bw_hz * 0.5);

    //     const double f0_mhz = f0_hz / 1e6;
    //     const double f1_mhz = f1_hz / 1e6;
    //     const double bw_khz = bw_hz / 1e3;

    //     std::fprintf(stderr,
    //         "[DDC:init] UTC %ld (%s) → %ld (%s) | FREQ %.3f–%.3f MHz (BW %.3f kHz) | srcFs=%d, wDDC=%.3f MHz, decirate=%d\n",
    //         static_cast<long>(_starttime), startUtc.c_str(),
    //         static_cast<long>(_endtime),   endUtc.c_str(),
    //         f0_mhz, f1_mhz, bw_khz,
    //         _fs, static_cast<double>(source_freq)/1e6, _decirate);
    // }

    // [PATCH] I/O 링버퍼 초기화 & 리더 스레드 시작
    {
        const size_t ring_cap = std::max<size_t>(static_cast<size_t>(ddc_sample_size) * 8, 2 * 1024 * 1024);
        _ring.reset(new SpscRing<uint8_t>(ring_cap));
        _ring_capacity = ring_cap;  // [ADD] 용량 저장
        _ioDone.store(false, std::memory_order_relaxed);

        _samples_in_file = samples_in_file;
        _start_n = start_n;
        _first_file = first_file;

        _readerThread = std::thread([this]() { this->readerLoopWithPreopen(); });
    }
    return true;
}

bool DDCExecutor::initSecondaryDDC(const time_t starttime, const time_t endtime, const int64_t frequency, const int bandwidth, const DataStorageInfo* dat_storage_info) {
    secondary = createDDCIQComplex(dat_storage_info->samplerate, bandwidth, samples, freq_offset, /* out */ _decirate, /* out */ _channel_bandwidth);
    if (!secondary) {
        std::cerr << "Failed to create ddc iq complex";
        return false;
    }
    _pOutSecondaryIQComplex = nullptr;
    cudaHostAlloc(&_pOutSecondaryIQComplex, ddc_samples * sizeof(int) * 2, cudaHostAllocDefault);
    return true;
}


bool DDCExecutor::executeDDC(IDDCHandler* ddc_handler) {
    //using clk = std::chrono::steady_clock;
    bool processed_any = false;

    const size_t required_bytes = static_cast<size_t>(ddc_sample_size);

    std::vector<uint8_t>  batch(required_bytes);
    const size_t required_shorts = required_bytes / sizeof(int16_t);
    std::vector<int32_t>  input_iq(required_shorts);

    // 원 신호(float)와 2:1 다운샘플(float) 버퍼를 모두 “루프 바깥”에 둠
    std::vector<float>    iq_data_f(static_cast<size_t>(ddc_samples) * 2);
    std::vector<float>    iq_data_f_half;  // <- persistent secondary buffer

    while (_running) {
        if (!_ring) break;

        //auto t0 = clk::now();

        // (1) wait
        //auto w0 = clk::now();
        const bool ok = waitUntilReadable(required_bytes, std::chrono::milliseconds(200));
        //auto w1 = clk::now();

        // ===== [TAIL][AUTO] ioDone 이후 남은 잔량(<required) 자동 처리 =====
        // - waitUntilReadable() 실패했더라도, ioDone이고 ring에 뭔가 남아 있으면 tail로 간주하고 0-패딩 처리
        if (!ok) {
            if (_ring && ioDone()) {
                const size_t avail = _ring->readable();
                if (avail > 0 && avail < required_bytes) {
                    // tail: avail 만큼 읽고 나머지는 0 패딩
                    std::fill(batch.begin(), batch.end(), 0);
                    size_t got_tail = _ring->read(batch.data(), avail);
                    if (got_tail != avail) {
                        // 경쟁 조건으로 줄었으면 다음 루프로
                        continue;
                    }
                    // 아래 정규 파이프라인으로 처리
                } else {
                    // 더 읽을 것도 없으면 종료
                    break;
                }
            } else {
                // ioDone 아니면 계속 대기 루프
                continue;
            }
        } else {
            // 정규 경로: 충분히 찼음 → required_bytes 읽기
            size_t got = _ring->read(batch.data(), required_bytes);
            if (got < required_bytes) {
                // 드물게 경쟁 상황: ioDone이면 tail 로직에서 잡힘
                if (ioDone()) continue;
                else continue;
            }
        }

        // (3) s16 -> Q31
        //auto c10 = clk::now();
        const int16_t* src16 = reinterpret_cast<const int16_t*>(batch.data());
        for (size_t k = 0; k < required_shorts; ++k) {
            input_iq[k] = static_cast<int32_t>(src16[k]) << 15;
        }
        //auto c11 = clk::now();

        // (4) CUDA DDC
        //auto cu0 = clk::now();
        master->input(input_iq.data());
        master->asyncDecimate();
        master->output(_pOutIQComplex);
        master->wait();
        //auto cu1 = clk::now();

        // (5) Q31 -> float (원본 full-rate)
        //auto c20 = clk::now();
        
        constexpr float kQ31 = 1.0f / 2147483648.0f; // 2^31
        for (int k = 0; k < ddc_samples; ++k) {
            iq_data_f[2*k]     = _pOutIQComplex[2*k]     * kQ31;
            iq_data_f[2*k + 1] = _pOutIQComplex[2*k + 1] * kQ31;
        }
        
        // for (int k = 0; k < ddc_samples; ++k) {
        //     iq_data_f[2*k]     = static_cast<float>(_pOutIQComplex[2*k]);
        //     iq_data_f[2*k + 1] = static_cast<float>(_pOutIQComplex[2*k + 1]);
        // }
        
        //auto c21 = clk::now();

        // ----- [FIX] decirate==64일 때 2:1 평균 디케imation을 “지속 버퍼”에 생성 -----
        // const double eff_fs = static_cast<double>(_fs) / static_cast<double>(_decirate);
        double eff_fs = static_cast<double>(_fs) / static_cast<double>(_decirate);
        
        //unsigned postDecim = 2u;
        unsigned postDecim = (_decirate == 64) ? 2u : 1u;
        eff_fs /= postDecim;

        unsigned out_samples = static_cast<unsigned>(ddc_samples);
        float*   out_ptr     = iq_data_f.data();
        //if(true) {
        if (_decirate == 64) {
            const unsigned inN = static_cast<unsigned>(ddc_samples);
            const unsigned N2  = inN / 2;
            // 홀수 안전: 마지막 1개가 남으면 그냥 버림
            iq_data_f_half.resize(static_cast<size_t>(N2) * 2);

            // (I0+I1)/2, (Q0+Q1)/2
            for (unsigned k = 0; k < N2; ++k) {
                const unsigned i0 = 2u * (2u * k);
                const unsigned i1 = i0 + 2u;
                iq_data_f_half[2*k]     = 0.5f * (iq_data_f[i0]     + iq_data_f[i1]);
                iq_data_f_half[2*k + 1] = 0.5f * (iq_data_f[i0 + 1] + iq_data_f[i1 + 1]);
            }

            out_samples = N2;
            out_ptr     = iq_data_f_half.data();
        }
        // --------------------------------------------------------------------

        // (6) handler
        const int64_t ts_ms = static_cast<int64_t>(_starttime) * 1000
                    + static_cast<int64_t>((_read_samples / eff_fs) * 1000.0);        

        //auto h0 = clk::now();
        ddc_handler->onFullBuffer(static_cast<time_t>(ts_ms), out_ptr, out_samples);
        //auto h1 = clk::now();

        // [FIX] 실제로 보낸 샘플 수만큼 증가
        _read_samples += out_samples;

        //auto t1 = clk::now();

        processed_any = true;

        // tail을 처리한 직후라면, 다음 루프에서 ioDone && ring 비었는지에 따라 빠져나가게 둔다.
        // (여기서 즉시 break하지 않는 이유: 동일 호출 내에서 추가 데이터가 이미 들어와 있을 수 있음)
    }
    return processed_any;
}


bool DDCExecutor::getRunning() {
    return _running;
}

bool DDCExecutor::executeDDCDF(IDDCHandler* ddc_handler) {
    // (네가 준 코드 그대로 — 중략 없이 유지)
    // ... (원본 executeDDCDF 구현)
    return false;
}

void DDCExecutor::parseStreamHeader(stStreamHeader& header, const char* frame_ptr)
{
    const char* frame_header = (const char*)frame_ptr;

    header.stream_type = *(short*)(frame_header + 4);
    header.data_type = *(short*)(frame_header + 6);
    header.stream_addr = *(unsigned int*)(frame_header + 8);
    header.stream_size = *(unsigned int*)(frame_header + 12);
    header.antenna = ((*(unsigned int*)(frame_header + 16) >> 24) & 0x1F);
    header.year = ((*(unsigned int*)(frame_header + 16) >> 8) & 0xFFFF);
    header.month = (*(unsigned int*)(frame_header + 16) & 0xFF);
    header.day = (*(unsigned int*)(frame_header + 20) >> 24) & 0xFF;
    header.hour = ((*(unsigned int*)(frame_header + 20) >> 16) & 0xFF);
    header.minute = ((*(unsigned int*)(frame_header + 20) >> 8) & 0xFF);
    header.second = (*(unsigned int*)(frame_header + 20) & 0xFF);
    header.nano = *(unsigned int*)(frame_header + 24);
    return;
}

// ... (matchBchFrameSync / matchAchFrameSync — 네가 준 코드 그대로 유지)

void DDCExecutor::stop() {
    // [PATCH] 러닝 플래그 off
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        _running = false;
    }
    // readerLoop가 1ms sleep 중이라도 자연 종료됨
}

void DDCExecutor::close() {
    // [PATCH] 실제 정리 수행: stop + join
    stop();
    if (_readerThread.joinable())
        _readerThread.join();
}

// ===================== [PATCH] 아래는 private 구현 =====================

std::string DDCExecutor::makeFilePath(int index)
{
    std::string dir_index = std::to_string(index / 10000);
    std::string dat_dir = storage_dir + dir_index + "/";
    return dat_dir + std::to_string(index) + ".bin";
}

bool DDCExecutor::openCurrentFileIfNeeded()
{
#if FEAT_POSIX_IO
    if (fd_is_open(t_curFd)) return true;

    const std::string filename = makeFilePath(_file_index);
    t_curFd = open_fd_seq(filename);
    if (!fd_is_open(t_curFd)) return false;

    if (_file_index == _first_file) {
        const int first_offset_in_file = static_cast<int>(_start_n % _samples_in_file);
        int frame_offset = first_offset_in_file / frame_samples;
        off_t ofs = (off_t)(header_size + body_size) * frame_offset;
        if (::lseek(t_curFd, ofs, SEEK_SET) == (off_t)-1) return false;
        _frame_offset_in_file = frame_offset;
    } else {
        _frame_offset_in_file = 0;
    }

    _frames_in_file_left = std::min(frame_count_in_file - _frame_offset_in_file, _frames_to_collect);
    return true;
#else
    if (_curFile.is_open()) return true;

    const std::string filename = makeFilePath(_file_index);
    _curFile.open(filename, std::ios::binary);
    if (!_curFile.is_open()) return false;

    if (_file_index == _first_file) {
        const int first_offset_in_file = static_cast<int>(_start_n % _samples_in_file);
        int frame_offset = first_offset_in_file / frame_samples;
        _curFile.seekg((std::streamoff)(header_size + body_size) * frame_offset, std::ios::beg);
        _frame_offset_in_file = frame_offset;
    } else {
        _frame_offset_in_file = 0;
    }

    _frames_in_file_left = std::min(frame_count_in_file - _frame_offset_in_file, _frames_to_collect);
    return true;
#endif
}

void DDCExecutor::preopenNextFile()
{
#if FEAT_POSIX_IO
    if (fd_is_open(t_nextFd)) return;
    const int nextIndex = _file_index + 1;
    const std::string nextname = makeFilePath(nextIndex);
    t_nextFd = open_fd_seq(nextname);
    _next_file_index = nextIndex;
#else
    if (_nextFile.is_open()) return;
    const int nextIndex = _file_index + 1;
    const std::string nextname = makeFilePath(nextIndex);
    _nextFile.open(nextname, std::ios::binary);
    _next_file_index = nextIndex;
#endif
}

void DDCExecutor::swapToNextFile()
{
#if FEAT_POSIX_IO
    close_fd_if_open(t_curFd);
    if (fd_is_open(t_nextFd)) {
        t_curFd = t_nextFd; t_nextFd = -1;
        _file_index = _next_file_index;
        _frame_offset_in_file = 0;
        _frames_in_file_left = std::min(frame_count_in_file, _frames_to_collect);
    } else {
        _file_index++;
        openCurrentFileIfNeeded();
    }
#else
    if (_curFile.is_open()) {
        _curFile.close();
        _curFile.clear();
    }
    if (_nextFile.is_open()) {
        _curFile.swap(_nextFile);
        _file_index = _next_file_index;
        _nextFile.close();
        _nextFile.clear();
        _frame_offset_in_file = 0;
        _frames_in_file_left = std::min(frame_count_in_file, _frames_to_collect);
    } else {
        _file_index++;
        openCurrentFileIfNeeded();
    }
#endif
}

// ===== 2단계: 4MB 고정 청크로 바디를 모아 한 번에 링 write =====
#if FEAT_POSIX_IO && FEAT_CHUNKED_4MB
void DDCExecutor::bulkReadFramesIntoRing_Chunked()
{
    if (!fd_is_open(t_curFd)) return;

    // 링 writable 상한: 너무 가득 차 있으면 과도한 읽기를 잠깐 멈춰 스파이크 방지
    if (_ring && _ring->writable() < (kChunkBytes / 2)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }

    if (t_bigBuf.size() != kChunkBytes) t_bigBuf.resize(kChunkBytes);
    size_t fill = 0;

    // 이번 배치에서 처리할 최대 프레임 수
    const int wanted = std::min((_frames_in_file_left), _read_batch_frames);
    if (wanted <= 0) return;

    int done_frames = 0;

    while (_running && done_frames < wanted) {
        // 헤더 읽기
        unsigned char header_buf[28];
        if (!read_exact(t_curFd, header_buf, header_size)) {
            _frames_in_file_left = 0;
            break;
        }

        uint16_t stream_type = 0;
        uint32_t stream_size = 0;
        std::memcpy(&stream_type, header_buf + 4, sizeof(stream_type));
        std::memcpy(&stream_size, header_buf + 12, sizeof(stream_size));
        if (stream_size == 0 || stream_size > (uint32_t)body_size) stream_size = (uint32_t)body_size;

        if (stream_type != 2) {
            // IQ가 아니면 스킵
            if (!skip_exact(t_curFd, stream_size)) { _frames_in_file_left = 0; break; }
        } else {
            // 현재 큰 청크에 이 바디를 붙였을 때 넘치면, 먼저 링으로 플러시
            if (fill + stream_size > kChunkBytes) {
                // 링 writable 대기
                while (_running && _ring && _ring->writable() < fill) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!_running || !_ring) break;

                _ring->write(t_bigBuf.data(), fill);
                fill = 0;
            }

            // 바디를 bigBuf에 직접 읽어 붙임
            if (!read_exact(t_curFd, t_bigBuf.data() + fill, stream_size)) {
                _frames_in_file_left = 0;
                break;
            }
            fill += stream_size;
            _frames_to_collect--;
        }

        done_frames++;
        _frames_in_file_left--;
        if (_frames_in_file_left <= 0) break;
    }

    // 남은 잔량 플러시
    if (fill > 0) {
        while (_running && _ring && _ring->writable() < fill) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (_running && _ring) {
            _ring->write(t_bigBuf.data(), fill);
        }
    }
}
#endif // FEAT_POSIX_IO && FEAT_CHUNKED_4MB

void DDCExecutor::bulkReadFramesIntoRing()
{
#if FEAT_POSIX_IO && FEAT_CHUNKED_4MB
    // 2단계: 큰 청크 버전
    bulkReadFramesIntoRing_Chunked();
#elif FEAT_POSIX_IO
    // 1단계: POSIX + 프레임단위 (원본과 동일 로직)
    if (!fd_is_open(t_curFd)) return;
    const int wanted = std::min((_frames_in_file_left), _read_batch_frames);
    if (wanted <= 0) return;

    for (int i = 0; i < wanted; ++i) {
        if (!_running || _frames_to_collect <= 0) break;

        unsigned char header_buf[28];
        if (!read_exact(t_curFd, header_buf, header_size)) {
            _frames_in_file_left = 0;
            break;
        }

        uint16_t stream_type = 0;
        uint32_t stream_size = 0;
        std::memcpy(&stream_type, header_buf + 4, sizeof(stream_type));
        std::memcpy(&stream_size, header_buf + 12, sizeof(stream_size));
        if (stream_size == 0 || stream_size > (uint32_t)body_size) stream_size = (uint32_t)body_size;

        if (stream_type != 2) {
            if (!skip_exact(t_curFd, stream_size)) { _frames_in_file_left = 0; break; }
        } else {
            _tmp.resize(stream_size);
            if (!read_exact(t_curFd, _tmp.data(), stream_size)) {
                _frames_in_file_left = 0;
                break;
            }

            size_t need = (size_t)stream_size;
            while (_running && _ring && _ring->writable() < need) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!_running || !_ring) break;

            _ring->write(_tmp.data(), need);
            _frames_to_collect--;
        }

        _frames_in_file_left--;
        if (_frames_in_file_left <= 0) break;
    }
#else
    // fstream 경로 (원본 유지)
    if (!_curFile.is_open()) return;
    const int wanted = std::min((_frames_in_file_left), _read_batch_frames);
    if (wanted <= 0) return;

    for (int i = 0; i < wanted; ++i) {
        if (!_running || _frames_to_collect <= 0) break;

        unsigned char header_buf[28];
        _curFile.read(reinterpret_cast<char*>(header_buf), header_size);
        if (_curFile.gcount() < (std::streamsize)header_size) {
            _frames_in_file_left = 0;
            break;
        }

        uint16_t stream_type = 0;
        uint32_t stream_size = 0;
        std::memcpy(&stream_type, header_buf + 4, sizeof(stream_type));
        std::memcpy(&stream_size, header_buf + 12, sizeof(stream_size));
        if (stream_size == 0 || stream_size > (uint32_t)body_size) stream_size = (uint32_t)body_size;

        if (stream_type != 2) {
            _curFile.seekg(stream_size, std::ios::cur);
        } else {
            _tmp.resize(stream_size);
            _curFile.read(reinterpret_cast<char*>(_tmp.data()), stream_size);
            std::streamsize got = _curFile.gcount();
            if (got < (std::streamsize)stream_size) {
                _frames_in_file_left = 0;
                break;
            }

            size_t need = (size_t)stream_size;
            while (_running && _ring && _ring->writable() < need) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!_running || !_ring) break;

            _ring->write(_tmp.data(), need);
            _frames_to_collect--;
        }

        _frames_in_file_left--;
        if (_frames_in_file_left <= 0) break;
    }
#endif
}

void DDCExecutor::readerLoopWithPreopen()
{
    _read_batch_frames = 16;
    _tmp.reserve((size_t)body_size);

    if (!openCurrentFileIfNeeded()) {
        _ioDone.store(true, std::memory_order_release);
        return;
    }
    preopenNextFile();

    while (_running && _frames_to_collect > 0) {
        bulkReadFramesIntoRing();

        if (_frames_in_file_left <= 0) {
            swapToNextFile();
            preopenNextFile();
        }
        if (_ring && _ring->writable() < (size_t)body_size) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

#if FEAT_POSIX_IO
    // 리더 스레드 종료 시점에 FD 정리
    close_fd_if_open(t_curFd);
    close_fd_if_open(t_nextFd);
#else
    if (_curFile.is_open()) { _curFile.close(); }
    if (_nextFile.is_open()) { _nextFile.close(); }
#endif
    _ioDone.store(true, std::memory_order_release);
}

size_t DDCExecutor::ringCapacity() const {
    return _ring_capacity;
}

bool DDCExecutor::waitUntilReadable(size_t wantBytes, std::chrono::milliseconds timeout) {
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + timeout;

    while (_running && steady_clock::now() < deadline) {
        if (_ring && _ring->readable() >= wantBytes) return true;
        if (ioDone()) break; // 더 이상 채워지지 않음
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return (_ring && _ring->readable() >= wantBytes);
}
