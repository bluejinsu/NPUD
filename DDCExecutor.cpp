// DDCExecutor.cpp  (PATCH-2.2: add exec type param; audio=postDecim, watch/extract=no postDecim) 250922 12:10 KST

#include "DDCExecutor.h"  // 250919 03:36

#include <cstring>
#include <iostream>
#include <fstream>
#include <algorithm>

#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>   // for fprintf
#include "SpscRing.h"
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <ctime>    // UTC 포맷용
#include <string>   // std::string

// ========== [ADD] exec type 상수 ==========
enum {
    DDC_EXEC_AUDIO   = 0,
    DDC_EXEC_WATCH   = 1,
    DDC_EXEC_EXTRACT = 2
};

// ========== [ADD] 초간단 로그 유틸 ==========
#include <iomanip>
#include <sstream>

static inline std::string _NowTs_DDC() {
    using namespace std::chrono;
    auto t  = system_clock::now();
    auto tt = system_clock::to_time_t(t);
    auto ms = duration_cast<milliseconds>(t.time_since_epoch()) % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%F %T") << "." << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

#define LOGI(fmt, ...) do { std::fprintf(stderr, "[%s][I][DDC][tid=%zu] " fmt "\n", _NowTs_DDC().c_str(), (size_t)std::hash<std::thread::id>{}(std::this_thread::get_id()), ##__VA_ARGS__ ); } while(0)
#define LOGW(fmt, ...) do { std::fprintf(stderr, "[%s][W][DDC][tid=%zu] " fmt "\n", _NowTs_DDC().c_str(), (size_t)std::hash<std::thread::id>{}(std::this_thread::get_id()), ##__VA_ARGS__ ); } while(0)
#define LOGE(fmt, ...) do { std::fprintf(stderr, "[%s][E][DDC][tid=%zu] " fmt "\n", _NowTs_DDC().c_str(), (size_t)std::hash<std::thread::id>{}(std::this_thread::get_id()), ##__VA_ARGS__ ); } while(0)

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
    // 4MB 청크 (필요시 조정)
    static constexpr size_t kChunkBytes = 1 * 1024 * 1024;
    static thread_local std::vector<uint8_t> t_bigBuf;
  #endif
#endif // FEAT_POSIX_IO

// --------------------------------------------------------------------------------------------

DDCExecutor::DDCExecutor() 
    : _work(ios)
    , _running(false)
    , _pOutIQComplex(nullptr)
    , _pOutSecondaryIQComplex(nullptr)
{
    LOGI("ctor");
}

DDCExecutor::~DDCExecutor() {
    LOGI("dtor: stop+join and free pinned buffers");
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
    LOGI("dtor done");
}

std::shared_ptr<cuDDCIQComplex<int>> DDCExecutor::createDDCIQComplex(
        int source_samplerate, int bandwidth, int samples, float freq_offset, int& decirate, int &channel_bandwidth) {

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
    for (auto it = fir_filters.begin(); it != fir_filters.end(); ++it) {
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

    samples = frame_samples * 1;                       // 32 기존 유지
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
    LOGI("initDDC done: decirate=%d, chFs=%u, ringCap=%zu", _decirate, _channel_samplerate, _ring_capacity);
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

// ====================== [CHANGE] 오버로드 추가: type으로 모드 구분 ======================
bool DDCExecutor::executeDDC(IDDCHandler* ddc_handler, int type /* DDC_EXEC_* */) {
    bool processed_any = false;

    const size_t required_bytes = static_cast<size_t>(ddc_sample_size);

    std::vector<uint8_t>  batch(required_bytes);
    const size_t required_shorts = required_bytes / sizeof(int16_t);
    std::vector<int32_t>  input_iq(required_shorts);

    // 원 신호(float)와 2:1 다운샘플(float) 버퍼를 모두 “루프 바깥”에 둠
    std::vector<float>    iq_data_f(static_cast<size_t>(ddc_samples) * 2);
    static std::vector<float> post_buf; // [KEEP] persistent secondary buffer (I,Q interleaved)

    while (_running) {
        if (!_ring) break;

        const bool ok = waitUntilReadable(required_bytes, std::chrono::milliseconds(200));

        // ioDone 이후 tail 처리
        if (!ok) {
            if (_ring && ioDone()) {
                const size_t avail = _ring->readable();
                if (avail > 0 && avail < required_bytes) {
                    std::fill(batch.begin(), batch.end(), 0);
                    size_t got_tail = _ring->read(batch.data(), avail);
                    if (got_tail != avail) continue;
                } else {
                    break;
                }
            } else {
                continue;
            }
        } else {
            size_t got = _ring->read(batch.data(), required_bytes);
            if (got < required_bytes) {
                if (ioDone()) continue;
                else continue;
            }
        }

        // s16 -> Q31
        const int16_t* src16 = reinterpret_cast<const int16_t*>(batch.data());
        for (size_t k = 0; k < required_shorts; ++k) {
            input_iq[k] = static_cast<int32_t>(src16[k]) << 15;
        }

        // CUDA DDC
        master->input(input_iq.data());
        master->asyncDecimate();
        master->output(_pOutIQComplex);
        master->wait();

        // Q31 -> float
        constexpr float kQ31 = 1.0f / 2147483648.0f; // 2^31
        for (int k = 0; k < ddc_samples; ++k) {
            iq_data_f[2*k]     = _pOutIQComplex[2*k]     * kQ31;
            iq_data_f[2*k + 1] = _pOutIQComplex[2*k + 1] * kQ31;
        }

        // [CHANGE] 모드별 postDecim 적용 여부
        // AUDIO: 64→2, 32→4, 16→8
        // WATCH/EXTRACT: postDecim 강제 1 (추가 다운샘플 금지)
        unsigned postDecim = 1u;
        if (type == DDC_EXEC_AUDIO) {
            if (_decirate == 64u || _decirate == 32u || _decirate == 16u) {
                postDecim = 128u / static_cast<unsigned>(_decirate); // 64→2, 32→4, 16→8
            }
        } else {
            postDecim = 1u; // 강제 비적용
        }

        double eff_fs = static_cast<double>(_fs) / static_cast<double>(_decirate);
        eff_fs /= postDecim; // AUDIO만 영향을 받음(위에서 결정됨)

        unsigned out_samples = static_cast<unsigned>(ddc_samples);
        float*   out_ptr     = iq_data_f.data();

        if (postDecim > 1u) {
            const unsigned inN = static_cast<unsigned>(ddc_samples);
            const unsigned outN = inN / postDecim;       // 보통 정확히 나눠떨어짐
            const float invM = 1.0f / static_cast<float>(postDecim);

            post_buf.resize(static_cast<size_t>(outN) * 2);

            for (unsigned k = 0; k < outN; ++k) {
                const unsigned baseIn = k * postDecim;   // 복소샘플 인덱스
                float accI = 0.0f, accQ = 0.0f;
                for (unsigned m = 0; m < postDecim; ++m) {
                    const unsigned i = (baseIn + m) * 2u;
                    accI += iq_data_f[i];
                    accQ += iq_data_f[i + 1u];
                }
                post_buf[2u * k]     = accI * invM;
                post_buf[2u * k + 1] = accQ * invM;
            }

            out_samples = outN;
            out_ptr     = post_buf.data();
        }

        // 핸들러 호출 (타임스탬프는 최종 eff_fs 기준)
        const int64_t ts_ms = static_cast<int64_t>(_starttime) * 1000
                            + static_cast<int64_t>((_read_samples / eff_fs) * 1000.0);

        ddc_handler->onFullBuffer(static_cast<time_t>(ts_ms), out_ptr, out_samples);
        _read_samples += out_samples;
        processed_any = true;
    }
    return processed_any;
}

// ====================== [KEEP] 하위호환: 기존 시그니처는 AUDIO로 위임 ======================
bool DDCExecutor::executeDDC(IDDCHandler* ddc_handler) {
    return executeDDC(ddc_handler, DDC_EXEC_AUDIO);
}

bool DDCExecutor::getRunning() {
    return _running;
}

bool DDCExecutor::executeDDCDF(IDDCHandler* ddc_handler) {
    // (네가 가진 원본 그대로 — 필요 시 구현)
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

// =====  파일 열기/전환/읽기 루틴들  =================================================

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
        LOGE("reader: openCurrentFileIfNeeded() failed");
        return;
    }
    preopenNextFile();
    LOGI("reader: enter loop");

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
    LOGI("reader: exit loop (ioDone=true)");
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

void DDCExecutor::stop() {
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        if (!_running) return;
        _running = false;
    }
    LOGI("stop(): running=false set");
}

void DDCExecutor::close() {
    LOGI("close(): stop()+join reader");
    stop();
    if (_readerThread.joinable()) {
        _readerThread.join();
        LOGI("reader thread joined");
    }
}
