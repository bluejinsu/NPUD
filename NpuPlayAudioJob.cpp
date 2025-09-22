// NpuPlayAudioJob.cpp  (PATCH-2.2: lock RTP SR to constant 44.1kHz + 10ms fixed frames + robust stop chain) 250922 09:05 KST

#include "NpuPlayAudioJob.h"

#include "AutoGainControl.h"
#include "DataStorageInfo.h"
#include "DDCExecutor.h"
#include "FFTExecutor.h"
#include "HilbertFirFilter.h"
#include "NpuConfigure.h"
#include "StreamingResampler.hpp"
#include "util.hpp"

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

#include <boost/shared_array.hpp>

#include <fstream>
#include <vector>

#include <math.h>
#include <deque>
#include <future>    // [ADD] for std::async / std::shared_future
#include <thread>    // [ADD] for std::thread
#include <atomic>    // [ADD] for std::atomic
#include <cstdint>   // [ADD] for uint16_t>
#include <chrono>    // [ADD] prefill 대기 시간 지정
#include <boost/thread.hpp>
#include <iomanip>
#include <sstream>



// ========== [ADD] 초간단 로그 유틸 ==========
static inline std::string _NowTs() {
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
#define LOGI(fmt, ...) do { std::fprintf(stderr, "[%s][I][tid=%zu] " fmt "\n", _NowTs().c_str(), (size_t)std::hash<std::thread::id>{}(std::this_thread::get_id()), ##__VA_ARGS__ ); } while(0)
#define LOGW(fmt, ...) do { std::fprintf(stderr, "[%s][W][tid=%zu] " fmt "\n", _NowTs().c_str(), (size_t)std::hash<std::thread::id>{}(std::this_thread::get_id()), ##__VA_ARGS__ ); } while(0)
#define LOGE(fmt, ...) do { std::fprintf(stderr, "[%s][E][tid=%zu] " fmt "\n", _NowTs().c_str(), (size_t)std::hash<std::thread::id>{}(std::this_thread::get_id()), ##__VA_ARGS__ ); } while(0)

// ========== [ADD] 파일 스코프: ServerMediaSession 보관(헤더 수정 없이 누수 방지) ==========
static ServerMediaSession* g_sms = nullptr;

// ========== [ADD] 종료 요약/진행 플래그 ==========
static std::atomic<bool> g_rtspLoopStopped{false};
static std::atomic<bool> g_serverThreadJoined{false};
static std::atomic<bool> g_sessionClosed{false};
static std::atomic<bool> g_rtspClosed{false};
static std::atomic<bool> g_envReclaimed{false};
static std::atomic<bool> g_schedulerDeleted{false};
static std::atomic<bool> g_workerJoined{false};
// 재진입 방지(헤더 수정 없이 파일 스코프)
static std::atomic<bool> g_shuttingDown{false};

// ===== (ADD) RTP/PCM 고정 샘플레이트 지정: 44,100 Hz =====
static constexpr unsigned kPCM_OUT_SR = 44100;
static constexpr unsigned kPCM_FRAME_MS = 10;
static constexpr size_t   kPCM_SAMPLES_PER_FRAME = (kPCM_OUT_SR * kPCM_FRAME_MS) / 1000;  // == 441

// event-loop watch var
static volatile char g_watchStop = 0;

// ========== [ADD] 루프 스레드에서 watch-var를 set 하는 태스크 ==========
static void _StopLoopTask(void* /*clientData*/) {
    LOGI("[RTSP] _StopLoopTask fired on event-loop thread");
    g_watchStop = (char)0xFF;
}

// ========== [ADD] 추가 트리거/유틸 ==========
static EventTriggerId g_shutdownTrigger = 0;

static void _Noop(void*) {}
static inline void _PokeLoop(BasicTaskScheduler* sched, UsageEnvironment* env, EventTriggerId trig) {
    if (env && trig != 0) env->taskScheduler().triggerEvent(trig, nullptr);
    if (sched && trig != 0) sched->triggerEvent(trig, nullptr); // 동일 효과, 추가 보강
    if (env) env->taskScheduler().scheduleDelayedTask(0, (TaskFunc*)_StopLoopTask, nullptr); // 같은 스레드에서 watch set
}

// ========== [ADD] select() 영구 block 방지용 주기 틱(100ms) ==========
static void _Tick(void* clientData) {
    auto* env = static_cast<UsageEnvironment*>(clientData);
    // 가벼운 주기 예약만; 로그 없이 계속 재등록
    env->taskScheduler().scheduleDelayedTask(100000 /*100ms*/, (TaskFunc*)_Tick, clientData);
}



static void _ShutdownOnLoopTask(void* clientData) {
    auto* self = static_cast<NpuPlayAudioJob*>(clientData);
    if (!self) return;
    if (g_shuttingDown.exchange(true)) return;

    LOGI("[RTSP] shutdownOnLoop(): begin");

    if (self->_rtspServer) {
        LOGI("[RTSP] Medium::close(_rtspServer)");
        Medium::close(self->_rtspServer);
        self->_rtspServer = nullptr;
        g_rtspClosed = true;
    }

    // 같은 루프 스레드에서 watch-var set → 안전 탈출
    _StopLoopTask(nullptr);
    LOGI("[RTSP] shutdownOnLoop(): end");
}




// ---------------- FMDemod ----------------------------------------------------
class FMDemod {
public:
    FMDemod();
    ~FMDemod();

    float operator()(float inPhase, float quadature);

private:
    float m_oldPcmData;
    std::complex<float> m_c0;
    float m_unwrap;
};

FMDemod::FMDemod()
    : m_c0(0.00000000001f, 0.00000000001f),
      m_unwrap(0.0f),
      m_oldPcmData(0.0f) {}

FMDemod::~FMDemod() {}

float FMDemod::operator()(float inPhase, float quadature) {
    std::complex<float> c1(inPhase, quadature);

    if (m_c0 == std::complex<float>(0, 0)) {
        m_c0 = std::complex<float>(0.0000000000000000001f, 0.0000000000000000001f);
    }

    std::complex<float> delta = c1 / m_c0;

    if (delta.real() == 0) {
        delta = std::complex<float>(0.0000000000000000001f, delta.imag());
    }

    float dTheta = atan2(delta.imag(), delta.real());
    float pcmData = 0.0f;

    if (fabs(dTheta) > M_PI) {
        float unwrap = (dTheta > 0) ? -2 * M_PI : 2 * M_PI;
        pcmData = dTheta + unwrap;
    } else {
        pcmData = dTheta;
    }

    m_c0 = c1;
    m_oldPcmData = pcmData;

    return pcmData;
}

// ---------------- IPCMSource / IDemodulator ---------------------------------
class IPCMSource {
public:
    virtual bool getNextFrame(std::vector<short>& buffer) = 0;
    virtual unsigned getSamplerate() = 0;
    virtual time_t getDuration() = 0;
    virtual void reset() = 0;
};

class IDemodulator {
public:
    virtual void demodulate(float* ddc_iq, size_t ddc_samples, std::vector<float>& demod_data) = 0;
};

// ---------------- CostasLoop / LPF / Demodulators ---------------------------
#include <cmath>
#include <complex>

class CostasLoop {
private:
    float phase;
    float frequency;
    float loop_gain;
    float damping_factor;
    float bandwidth;

public:
    CostasLoop(float loop_bandwidth, float initial_frequency = 0.0f)
        : phase(0.0f), frequency(initial_frequency), loop_gain(0.01f),
          damping_factor(0.707f), bandwidth(loop_bandwidth) {}

    std::complex<float> process(const std::complex<float>& sample) {
        std::complex<float> rotated_sample = sample * std::exp(std::complex<float>(0, -phase));
        float phase_error = std::imag(rotated_sample) * std::real(rotated_sample);
        float proportional = phase_error * loop_gain;
        float integral = (phase_error * loop_gain) * (bandwidth / damping_factor);
        frequency += integral;
        phase += frequency + proportional;
        return rotated_sample;
    }
};

class LowPassFilter {
private:
    std::deque<float> buffer;
    size_t filter_size;
    float sum;

public:
    LowPassFilter(size_t size) : filter_size(size), sum(0.0f) {}

    float process(float sample) {
        buffer.push_back(sample);
        sum += sample;
        if (buffer.size() > filter_size) {
            sum -= buffer.front();
            buffer.pop_front();
        }
        return static_cast<float>(sum / buffer.size());
    }
};

class CostasLoopSyncAMDemodulator : public IDemodulator {
private:
    CostasLoop costasLoop;
    LowPassFilter lpf;

public:
    CostasLoopSyncAMDemodulator(float loop_bandwidth, size_t lpf_size) 
        : costasLoop(loop_bandwidth), lpf(lpf_size) {}

    void demodulate(float* ddc_iq, size_t ddc_samples, std::vector<float>& demod_data) override {
        demod_data.clear();
        demod_data.reserve(ddc_samples);

        for (size_t i = 0; i < ddc_samples; ++i) {
            std::complex<float> sample(ddc_iq[2 * i], ddc_iq[2 * i + 1]);
            std::complex<float> corrected = costasLoop.process(sample);
            float demodulated = std::real(corrected);
            float filtered = lpf.process(demodulated) * 1000.0f; // TODO: 후속 튜닝
            demod_data.push_back(filtered);
        }
    }
};

class AMDemodulator : public IDemodulator {
public:
    AMDemodulator() {}
    ~AMDemodulator() {}

    void demodulate(float* ddc_iq, size_t ddc_samples, std::vector<float>& demod_data) override {
        demod_data.clear();
        demod_data.reserve(ddc_samples);
        for (int k = 0; k < (int)ddc_samples; ++k) {
            float outInphase = ddc_iq[2*k];
            float outQuad    = ddc_iq[2*k + 1];
            float demod_value = sqrtf(outInphase*outInphase + outQuad*outQuad);
            demod_data.push_back(demod_value);
        }
    }
};

class USBDemodulator : public IDemodulator {
public:
    USBDemodulator() : _filter_initialized(false), _delayed(false) {}
    ~USBDemodulator() {}

    void demodulate(float* ddc_iq, size_t ddc_samples, std::vector<float>& demod_data) override {
        if (!_filter_initialized) {
            _hil_filter.Init((int)ddc_samples);
            _filter_initialized = true;
        }

        std::vector<float> inphase_block;
        std::vector<float> quad_block;
        inphase_block.reserve(ddc_samples);
        quad_block.reserve(ddc_samples);

        for (int i = 0; i < (int)ddc_samples; ++i) {
            inphase_block.push_back(ddc_iq[2*i]);
            quad_block.push_back(ddc_iq[2*i + 1]);
        }

        // Inphase
        {
            boost::asio::buffer_copy(_inphase_buffer.prepare(ddc_samples * sizeof(float)),
                                     boost::asio::buffer(inphase_block.data(), ddc_samples * sizeof(float)));
            _inphase_buffer.commit(ddc_samples * sizeof(float));
        }

        // Hilbert Quad
        {
            auto trans_quad_block = _trans_quad_buffer.prepare(ddc_samples * sizeof(float));
            _hil_filter.Filter(quad_block.data(), boost::asio::buffer_cast<float*>(trans_quad_block), (int)ddc_samples);
            _trans_quad_buffer.commit(ddc_samples * sizeof(float));
    
            if (!_delayed) {
                size_t delay_samples = _hil_filter.getFilterLength() / 2;
                size_t delay_size = delay_samples * sizeof(float);
                _trans_quad_buffer.consume(delay_size);
                _delayed = true;
            }
        }

        size_t buffer_samples = _trans_quad_buffer.size() / sizeof(float);
        if (buffer_samples > ddc_samples) {
            const float* inphase = static_cast<const float*>(_inphase_buffer.data().data());
            const float* quad    = static_cast<const float*>(_trans_quad_buffer.data().data());
            demod_data.clear();
            demod_data.reserve(ddc_samples);
            for (int i = 0; i < (int)ddc_samples; ++i) {
                float demod_value = (-1.0f * inphase[i] + quad[i]);
                demod_data.push_back(demod_value);
            }
            _inphase_buffer.consume(ddc_samples * sizeof(float));
            _trans_quad_buffer.consume(ddc_samples * sizeof(float));
        }
    }

private:
    bool _filter_initialized;
    HilbertFirFilter _hil_filter;
    boost::asio::streambuf _inphase_buffer;
    boost::asio::streambuf _trans_quad_buffer;
    bool _delayed;
};

class LSBDemodulator : public IDemodulator {
public:
    LSBDemodulator() : _filter_initialized(false), _delayed(false) {}
    ~LSBDemodulator() {}

    void demodulate(float* ddc_iq, size_t ddc_samples, std::vector<float>& demod_data) override {
        if (!_filter_initialized) {
            _hil_filter.Init((int)ddc_samples);
            _filter_initialized = true;
        }

        std::vector<float> inphase_block;
        std::vector<float> quad_block;
        inphase_block.reserve(ddc_samples);
        quad_block.reserve(ddc_samples);

        for (int i = 0; i < (int)ddc_samples; ++i) {
            inphase_block.push_back(ddc_iq[2*i]);
            quad_block.push_back(ddc_iq[2*i + 1]);
        }

        // Inphase
        {
            boost::asio::buffer_copy(_inphase_buffer.prepare(ddc_samples * sizeof(float)),
                                     boost::asio::buffer(inphase_block.data(), ddc_samples * sizeof(float)));
            _inphase_buffer.commit(ddc_samples * sizeof(float));
        }

        // Hilbert Quad
        {
            auto trans_quad_block = _trans_quad_buffer.prepare(ddc_samples * sizeof(float));
            _hil_filter.Filter(quad_block.data(), boost::asio::buffer_cast<float*>(trans_quad_block), (int)ddc_samples);
            _trans_quad_buffer.commit(ddc_samples * sizeof(float));
    
            if (!_delayed) {
                size_t delay_samples = _hil_filter.getFilterLength() / 2;
                size_t delay_size = delay_samples * sizeof(float);
                _trans_quad_buffer.consume(delay_size);
                _delayed = true;
            }
        }

        size_t buffer_samples = _trans_quad_buffer.size() / sizeof(float);
        if (buffer_samples > ddc_samples) {
            const float* inphase = static_cast<const float*>(_inphase_buffer.data().data());
            const float* quad    = static_cast<const float*>(_trans_quad_buffer.data().data());
            demod_data.clear();
            demod_data.reserve(ddc_samples);
            for (int i = 0; i < (int)ddc_samples; ++i) {
                float demod_value = (inphase[i] + quad[i]);
                demod_data.push_back(demod_value);
            }
            _inphase_buffer.consume(ddc_samples * sizeof(float));
            _trans_quad_buffer.consume(ddc_samples * sizeof(float));
        }
    }

private:
    bool _filter_initialized;
    HilbertFirFilter _hil_filter;
    boost::asio::streambuf _inphase_buffer;
    boost::asio::streambuf _trans_quad_buffer;
    bool _delayed;
};

class FMDemodulator : public IDemodulator {
public:
    void demodulate(float* ddc_iq, size_t ddc_samples, std::vector<float>& demod_data) override {
        demod_data.clear();
        demod_data.reserve(ddc_samples);
        for (int k = 0; k < (int)ddc_samples; ++k) {
            float outInphase = ddc_iq[2*k];
            float outQuad    = ddc_iq[2*k + 1];
            float demod_value = _fm(outInphase, outQuad);
            demod_data.push_back(demod_value);
        }
    }
private:
    FMDemod _fm;
};

// ---------------- DemodConduit ----------------------------------------------
class DemodConduit : public IDDCHandler, public IPCMSource {
public:
    DemodConduit(unsigned int samplerate, IDemodulator* demodulator, bool squelch_mode, float squelch_threshold, float scale, float level_offset)
        : _samplerate(samplerate)
        , _out_samplerate(44100) // 고정 출력 샘플레이트
        , _offset(0)
        , _resampler(samplerate, _out_samplerate)
        , _demodulator(demodulator)
        , _isStarted(false)
        , _scale(scale)
        , _squelch_mode(squelch_mode)
        , _squelch_threshold(squelch_threshold)
        , _level_offset(level_offset)
        , _agc(0.01, 17, _out_samplerate/10, 17) 
        , _warmedUp(false)            // [ADD]
        {}

    // 10ms 고정 프레임(= 441 samples @ 44.1kHz). 
    // 부족하면 false 반환 → PCMFramedSource가 1ms 뒤 재시도
    bool getNextFrame(std::vector<short>& buffer) override {
        boost::lock_guard<boost::mutex> lock(_mtx);

        if (!_isStarted) return false;

        // 150ms 워밍업 버퍼가 쌓일 때까지 재생 보류
        const size_t warmupSamples =
            static_cast<size_t>((uint64_t)_out_samplerate * 150ULL / 1000ULL);
        if (!_warmedUp) {
            if (_data.size() < warmupSamples) {
                return false; // 워밍업 완료 전: 재시도
            }
            _warmedUp = true;
        }

        // ★ 441 샘플 고정
        const size_t want = kPCM_SAMPLES_PER_FRAME; // 441

        // 아직 부족하면 false 반환 (패딩 없음)
        if (_data.size() < want) {
            return false;
        }

        // 441개 꺼내서 반환
        buffer.resize(want);
        for (size_t i = 0; i < want; ++i) {
            buffer[i] = _data.front();
            _data.pop_front();
        }
        return true;
    }

    void setScale(float scale) { _scale = scale; }
    float getScale() { return _scale; }

    // 고정 44100
    unsigned getSamplerate() override {
        return _out_samplerate;
    }

    time_t getDuration() override { return _offset / _samplerate; }
    void reset() { _offset = 0; }

    void onInitialized() override { 
        _isStarted = true; 
        LOGI("[DemodConduit] onInitialized (in=%u -> out=%u)", _samplerate, _out_samplerate);
    }
    void onClosed() override      { 
        _isStarted = false; 
        LOGI("[DemodConduit] onClosed");
    }
    void onReadFrame(int, int) override {}

    void onFullBuffer(time_t /*ts*/, float* ddc_iq, size_t ddc_samples) override {
        // 1) 로컬/멤버 재사용 버퍼로 처리(락 없이)
        std::vector<float> demod_data;      demod_data.reserve(ddc_samples);
        std::vector<float> auto_gain_data;  auto_gain_data.reserve(ddc_samples);

        if (_squelch_mode) {
            const double p = getPowerLevel(ddc_iq, ddc_samples);
            if (p < _squelch_threshold) {
                demod_data.assign(ddc_samples, 0.0f);
            } else {
                _demodulator->demodulate(ddc_iq, ddc_samples, demod_data);
            }
        } else {
            _demodulator->demodulate(ddc_iq, ddc_samples, demod_data);
        }

        auto_gain_data.resize(demod_data.size());
        for (size_t i=0;i<demod_data.size();++i) auto_gain_data[i] = _agc(demod_data[i]);

        std::vector<short> out; 
        out = _resampler.process(auto_gain_data, false);

        // 2) 여기서만 잠깐 락
        {
            boost::lock_guard<boost::mutex> lock(_mtx);
            _data.insert(_data.end(), out.begin(), out.end());
        }
    }
    
    void setSquelchMode(bool mode)      { _squelch_mode = mode; }
    void setSquelchThreshold(float thr) { _squelch_threshold = thr; }

private:
    std::deque<short> _data;
    std::deque<short> _out_ring;  // (사용 안함)
    const unsigned _frame_ms = 10;    
    bool _warmedUp;  // [ADD]

    double getPowerLevel(float* ddc_iq, size_t ddc_samples) {
        FFTExecutor fft_executor;
        const int N = static_cast<int>(ddc_samples);              // [CHANGE] 실제 길이
        fft_executor.init(N);

        std::vector<double> iq_data_d(static_cast<size_t>(N) * 2);
        std::vector<double> fft_output(static_cast<size_t>(N) * 2);
        for (int k = 0; k < N; ++k) {
            iq_data_d[2*k]     = (double)ddc_iq[2*k];
            iq_data_d[2*k + 1] = (double)ddc_iq[2*k + 1];
        }

        // [CHANGE] execute(..., N) : 길이 가변 FFT 안전 실행
        fft_executor.execute(iq_data_d.data(), fft_output.data(), N);

        double max_power_level = -999.0;
        double fft_level_offset = 20 * log10((double)N);

        for (int i = N/2; i < N; ++i) {
            double real = fft_output[2*i];     if (real == 0) real += 1e-7;
            double imag = fft_output[2*i + 1]; if (imag == 0) imag += 1e-7;
            double power = 10 * log10(real*real + imag*imag) + _level_offset - fft_level_offset;
            if (max_power_level < power) max_power_level = power;
        }
        for (int i = 0; i < N/2; ++i) {
            double real = fft_output[2*i];     if (real == 0) real += 1e-7;
            double imag = fft_output[2*i + 1]; if (imag == 0) imag += 1e-7;
            double power = 10 * log10(real*real + imag*imag) + _level_offset - fft_level_offset;
            if (max_power_level < power) max_power_level = power;
        }
        return max_power_level;        
    }

private:
    int _offset;
    unsigned int _samplerate;
    unsigned int _out_samplerate;
    StreamingResampler _resampler;
    boost::mutex _mtx;
    IDemodulator* _demodulator;
    bool _isStarted;
    std::atomic<float> _scale;
    bool _squelch_mode;
    float _squelch_threshold;
    float _level_offset;
    AutoGainControl _agc;
};

// ---------------- DDCRunner --------------------------------------------------
class DDCRunner {
public:
    explicit DDCRunner(DDCExecutor* ddc_exe) : _ddc_exe(ddc_exe) {}

    void setDDCHandler(IDDCHandler* ddc_handler) { _ddc_handler = ddc_handler; }

    void start() {
        if (!_ddc_exe || !_ddc_handler) return;

        _ddc_handler->onInitialized();

        auto fut = std::async(std::launch::async, [this]() {
            while (_ddc_exe->getRunning()) {
                bool didWork = _ddc_exe->executeDDC(_ddc_handler);
                if (!didWork) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            _ddc_handler->onClosed();
        });

        _sfut = fut.share();
    }

private:
    DDCExecutor* _ddc_exe{nullptr};
    IDDCHandler* _ddc_handler{nullptr};
    std::shared_future<void> _sfut;
};

// ---------------- PCMFramedSource -------------------------------------------
class PCMFramedSource : public FramedSource {
public:
    static PCMFramedSource* createNew(UsageEnvironment& env, IPCMSource* pcmSource, time_t timestamp) {
        return new PCMFramedSource(env, pcmSource, timestamp);
    }

    static void s_callAfterGetting(void* clientData) {
        auto* self = static_cast<PCMFramedSource*>(clientData);
        self->afterGetting(self);
    }

    static void s_deliverFrame(void* clientData) {
        static_cast<PCMFramedSource*>(clientData)->deliverFrame();
    }

protected:
    PCMFramedSource(UsageEnvironment& env, IPCMSource* pcm_source, time_t timestamp)
        : FramedSource(env)
        , _pcm_source(pcm_source)
        , _timestamp(timestamp)
        , _ptsInit(false) {
        _nextPTS.tv_sec = 0; _nextPTS.tv_usec = 0;
    }

    // [ADD] 지연 태스크 안전 취소
    ~PCMFramedSource() override {
        if (nextTask() != 0) {
            envir().taskScheduler().unscheduleDelayedTask(nextTask());
            nextTask() = 0;
        }
    }

private:
    void doGetNextFrame() override {
        nextTask() = envir().taskScheduler().scheduleDelayedTask(
            0, (TaskFunc*)s_deliverFrame, this);
    }

    void deliverFrame() {
        std::vector<short> next_frame;
        if (!_pcm_source->getNextFrame(next_frame)) {
            // live555 delay 단위는 µs
            nextTask() = envir().taskScheduler().scheduleDelayedTask(
                1000 /* 1ms */, (TaskFunc*)s_deliverFrame, this);
            return;
        }

        // 기대 프레임 샘플수(441)와 바이트수(882)
        const size_t expectedSamples = kPCM_SAMPLES_PER_FRAME; // 441
        const size_t expectedBytes   = expectedSamples * sizeof(short); // 882

        // 사용할 총 바이트(호스트 메모리 상 short[])
        size_t totalBytes = next_frame.size() * sizeof(short);

        // 수신자 버퍼 한계 반영 (샘플(2바이트) 경계로 정렬)
        size_t bytesToSend = totalBytes;

        if (fMaxSize < expectedBytes) {
            bytesToSend = fMaxSize & ~static_cast<size_t>(1); // 샘플경계 정렬
            fNumTruncatedBytes = (unsigned)(totalBytes > bytesToSend ? totalBytes - bytesToSend : 0);
        } else {
            bytesToSend = expectedBytes;
            fNumTruncatedBytes = (unsigned)(totalBytes > bytesToSend ? totalBytes - bytesToSend : 0);
        }

        size_t samplesToSend = bytesToSend / 2;

        const uint16_t* src = reinterpret_cast<const uint16_t*>(next_frame.data());
        uint8_t* dst = fTo;

        for (size_t i = 0; i < samplesToSend; ++i) {
            uint16_t u = src[i];
            dst[2 * i    ] = static_cast<uint8_t>(u & 0xFF);
            dst[2 * i + 1] = static_cast<uint8_t>(u >> 8);
        }

        fFrameSize = static_cast<unsigned>(samplesToSend * 2);

        // ★ 10ms 고정
        fDurationInMicroseconds = kPCM_FRAME_MS * 1000; // 10000

        // PTS 계산/진행
        if (!_ptsInit) {
            gettimeofday(&_nextPTS, nullptr);
            _ptsInit = true;
        }
        fPresentationTime = _nextPTS;

        unsigned usec = fPresentationTime.tv_usec + fDurationInMicroseconds;
        _nextPTS.tv_sec  = fPresentationTime.tv_sec + usec / 1000000;
        _nextPTS.tv_usec = usec % 1000000;

        // wall-clock에 맞춰 다음 afterGetting 호출 스케줄
        timeval now; gettimeofday(&now, nullptr);
        auto toUsec = [](const timeval& tv){ return (int64_t)tv.tv_sec*1000000LL + tv.tv_usec; };
        int64_t d_us = toUsec(fPresentationTime) - toUsec(now);
        if (d_us < 0) d_us = 0;

        nextTask() = envir().taskScheduler().scheduleDelayedTask(
            static_cast<unsigned>(d_us), (TaskFunc*)s_callAfterGetting, this);
    }

private:
    IPCMSource* _pcm_source;
    time_t _timestamp;
    timeval _nextPTS;
    bool _ptsInit;
};

// ---------------- CustomRTPSink / PCMOnDemandSubsession ---------------------
class CustomRTPSink : public SimpleRTPSink {
public:
    static CustomRTPSink* createNew(UsageEnvironment& env, Groupsock* RTPgs,
                                    unsigned char rtpPayloadFormat, unsigned rtpTimestampFrequency,
                                    char const* sdpMediaTypeString, char const* rtpPayloadFormatName,
                                    unsigned numChannels = 1, Boolean allowMultipleFramesPerPacket = False,
                                    Boolean doNormalMBitRule = True) {
        return new CustomRTPSink(env, RTPgs, rtpPayloadFormat, rtpTimestampFrequency,
                                 sdpMediaTypeString, rtpPayloadFormatName,
                                 numChannels, allowMultipleFramesPerPacket, doNormalMBitRule);
    }

    void setCurrentTimestamp(unsigned newTimestamp) {
        fCurrentTimestamp = newTimestamp;
        envir() << "[CustomRTPSink] fCurrentTimestamp updated to: " << newTimestamp << "\n";
    }

    void setInitialPresentationTime(const timeval& newTime) {
        fInitialPresentationTime = newTime;
    }

protected:
    CustomRTPSink(UsageEnvironment& env, Groupsock* RTPgs,
                  unsigned char rtpPayloadFormat,
                  unsigned rtpTimestampFrequency,
                  char const* sdpMediaTypeString,
                  char const* rtpPayloadFormatName,
                  unsigned numChannels,
                  Boolean allowMultipleFramesPerPacket,
                  Boolean doNormalMBitRule)
        : SimpleRTPSink(env, RTPgs, rtpPayloadFormat, rtpTimestampFrequency,
                        sdpMediaTypeString, rtpPayloadFormatName,
                        numChannels, allowMultipleFramesPerPacket, doNormalMBitRule) {}

    virtual ~CustomRTPSink() {}
};

class PCMOnDemandSubsession : public OnDemandServerMediaSubsession {
public:
    static PCMOnDemandSubsession* createNew(UsageEnvironment& env, IPCMSource* pcm_source, time_t timestamp) {
        return new PCMOnDemandSubsession(env, pcm_source, timestamp);
    }

protected:
    PCMOnDemandSubsession(UsageEnvironment& env, IPCMSource* pcm_source, time_t timestamp)
        : OnDemandServerMediaSubsession(env, True)
        , _pcm_source(pcm_source)
        , _timestamp(timestamp) {}

    FramedSource* createNewStreamSource(unsigned, unsigned& estBitrate) override {
        // 고정 44.1kHz 기준 비트레이트 산정 (16-bit mono)
        estBitrate = static_cast<unsigned>(kPCM_OUT_SR * 16 / 1000); // ≈ 706 kbps
        return PCMFramedSource::createNew(envir(), _pcm_source, _timestamp);
    }

    RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource*) override {
        // RTP 타임스탬프 주파수 = 44,100 Hz 고정
        return CustomRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
                                        kPCM_OUT_SR, "audio", "L16", 1);
    }

    void startStream(unsigned clientSessionId, void* streamToken,
                     TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                     unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                     ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                     void* serverRequestAlternativeByteHandlerClientData) override {
        _pcm_source->reset();
        OnDemandServerMediaSubsession::startStream(clientSessionId, streamToken,
            rtcpRRHandler, rtcpRRHandlerClientData, rtpSeqNum, rtpTimestamp,
            serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
    }

    void seekStream(unsigned, void*, double&, double, u_int64_t&) override {}

    void testScaleFactor(float& scale) override {
        scale = 1.0f; // 서버 페이싱은 항상 정상 속도
    }

private:
    IPCMSource* _pcm_source;
    time_t _timestamp;
};

// ---------------- NpuPlayAudioJob -------------------------------------------
NpuPlayAudioJob::NpuPlayAudioJob(const NpuPlayAudioRequest& play_audio_req, NpuConfigure* config, int rtsp_port, std::unique_ptr<DataStorageInfo> data_storage_info)
    : _play_audio_req(play_audio_req)
    , _config(config)
    , _running(false)
    , _rtsp_port(rtsp_port)
    , _data_storage_info(std::move(data_storage_info)) {
    _rtsp_ip = _config->getValue("RTSP.IPADDRESS");

    // [ADD] 요약 플래그 초기화
    g_rtspLoopStopped   = false;
    g_serverThreadJoined= false;
    g_sessionClosed     = false;
    g_rtspClosed        = false;
    g_envReclaimed      = false;
    g_schedulerDeleted  = false;
    g_workerJoined      = false;
    g_shuttingDown      = false;
}

NpuPlayAudioJob::~NpuPlayAudioJob() {}

void NpuPlayAudioJob::startRtspServer(IPCMSource* pcm_source) {
    LOGI("[RTSP] startRtspServer: creating env/scheduler/server");
    _scheduler = BasicTaskScheduler::createNew(0);
    _env = BasicUsageEnvironment::createNew(*_scheduler);

    _rtspServer = RTSPServer::createNew(*_env, _rtsp_port);
    if (_rtspServer == nullptr) {
        *_env << "Failed to create RTSP server: " << _env->getResultMsg() << "\n";
        LOGE("[RTSP] Failed to create server: %s", _env->getResultMsg());
        return;
    }

    const char* streamName = "pcmSeekableStream";
    ServerMediaSession* sms = ServerMediaSession::createNew(*_env, streamName, streamName, "Seekable PCM audio stream");
    sms->addSubsession(PCMOnDemandSubsession::createNew(*_env, pcm_source, _play_audio_req.starttime));
    _rtspServer->addServerMediaSession(sms);

    // [ADD] 세션 포인터 보관 (헤더 수정 없이 누수 방지)
    g_sms = sms;

    *_env << "Play this stream using the URL: rtsp://" << _rtsp_ip.c_str() << ":" << _rtsp_port << "/" << streamName << "\n";
    LOGI("[RTSP] ready: rtsp://%s:%d/%s", _rtsp_ip.c_str(), _rtsp_port, streamName);

    _stopTrigger = _env->taskScheduler().createEventTrigger(_StopLoopTask);
    g_shutdownTrigger = _env->taskScheduler().createEventTrigger(_ShutdownOnLoopTask);

    g_watchStop = 0;
    g_rtspLoopStopped = false;

    _serverThread = std::thread([this]() {        
        // select 영구 block 방지용 keep-alive tick
        _env->taskScheduler().scheduleDelayedTask(100000, (TaskFunc*)_Tick, _env);

        LOGI("[RTSP] event loop enter");
        _env->taskScheduler().doEventLoop(&g_watchStop);
        LOGI("[RTSP] event loop exit");
        g_rtspLoopStopped = true;
    });
}

void NpuPlayAudioJob::work() {
    LOGI("[Job] work() begin");
    float level_offset = static_cast<float>(atof(_config->getValue("DATA.LEVEL_OFFSET").c_str()));
    
    DDCExecutor ddc_exe;
    if (!ddc_exe.initDDC(_play_audio_req.starttime,
                         _play_audio_req.endtime,
                         _play_audio_req.frequency,
                         _play_audio_req.bandwidth,
                         _data_storage_info.get())) {
        LOGE("[Job] initDDC failed");
        return;
    }

    int samplerate = ddc_exe.getChannelSamplerate();

    FMDemodulator fm_demodulator;
    AMDemodulator  am_demodulator;
    USBDemodulator usb_demodulator;
    LSBDemodulator lsb_demodulator;

    IDemodulator* demodulator = nullptr;
    if (_play_audio_req.demod_type == "FM")       demodulator = &fm_demodulator;
    else if (_play_audio_req.demod_type == "AM")  demodulator = &am_demodulator;
    else if (_play_audio_req.demod_type == "USB") demodulator = &usb_demodulator;
    else if (_play_audio_req.demod_type == "LSB") demodulator = &lsb_demodulator;

    int effective_in_sr = samplerate;
    if (ddc_exe.getDecimateRate() == 64) { // 단순 예시
        effective_in_sr /= 2;
    }

    DemodConduit demod_conduit(effective_in_sr,
                               demodulator,
                               _play_audio_req.squelch_mode, 
                               _play_audio_req.squelch_threshold, 
                               _play_audio_req.scale, level_offset);

    // 소비 시작
    DDCRunner ddc_runner(&ddc_exe);
    ddc_runner.setDDCHandler(&demod_conduit);
    ddc_runner.start();

    // RTSP 오픈
    startRtspServer(&demod_conduit);

    // 수명 보장: stop() 호출까지 유지
    while (_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // [ADD] 세션 정리 완료까지 잠시 대기하여 소스 수명 보장
    for (int i = 0; i < 50 && !g_sessionClosed.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 정리: DDCExecutor 종료
    ddc_exe.close();
}

std::string NpuPlayAudioJob::start(JOB_COMPLEDTED_CALLBACK completed_callback) {
    boost::lock_guard<boost::mutex> lock(_mtx);
    _guid = generateID();
    _completed_callback = completed_callback;
    _running = true;
    LOGI("[Job] start(): guid=%s", _guid.c_str());
    _thread.reset(new std::thread(std::bind(&NpuPlayAudioJob::work, shared_from_this())));
    return _guid;
}

void NpuPlayAudioJob::stop() {
    LOGI("[Job] stop() requested");
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        if (!_running) LOGW("[Job] stop(): already stopped");
        _running = false;
    }

    // after: _stopTrigger는 건드리지 않습니다. g_shutdownTrigger만!
    if (_env && g_shutdownTrigger != 0) {
        LOGI("[RTSP] triggerEvent(g_shutdownTrigger) (only)");
        _env->taskScheduler().triggerEvent(g_shutdownTrigger, this);
    }


    // 2) 서버 스레드 조인 (반드시 doEventLoop 반환 후)
    if (_serverThread.joinable()) {
        if (std::this_thread::get_id() == _serverThread.get_id()) {
            LOGE("[RTSP] stop() called from server thread; skip join to avoid self-join deadlock");
        } else {
            LOGI("[RTSP] joining server thread...");
            _serverThread.join();
            g_serverThreadJoined = true;
            LOGI("[RTSP] server thread joined (rtspLoopStopped=%s)", g_rtspLoopStopped ? "true":"false");
        }
    } else {
        LOGW("[RTSP] server thread not joinable");
    }

    // 3) 트리거 정리
    if (_env) {
        if (_stopTrigger != 0) {
            LOGI("[RTSP] deleteEventTrigger(id=%u)", (unsigned)_stopTrigger);
            _env->taskScheduler().deleteEventTrigger(_stopTrigger);
            _stopTrigger = 0;
        }
        if (g_shutdownTrigger != 0) {
            LOGI("[RTSP] deleteEventTrigger(g_shutdownTrigger)");
            _env->taskScheduler().deleteEventTrigger(g_shutdownTrigger);
            g_shutdownTrigger = 0;
        }
    }

    // 4) env/scheduler 정리 (항상 마지막)
    if (_env) { LOGI("[RTSP] reclaim _env"); _env->reclaim(); _env = nullptr; g_envReclaimed = true; }
    if (_scheduler) { LOGI("[RTSP] delete _scheduler"); delete _scheduler; _scheduler = nullptr; g_schedulerDeleted = true; }

    // 5) 작업 스레드 종료
    wait(); g_workerJoined = true;

    if (_completed_callback) { LOGI("[Job] invoking completed_callback(guid=%s)", _guid.c_str()); try { _completed_callback(_guid); } catch (...) { LOGE("[Job] completed_callback threw"); } }

    LOGI("[Job] shutdown summary: rtspLoopStopped=%s, serverThreadJoined=%s, sessionClosed=%s, rtspClosed=%s, envReclaimed=%s, schedulerDeleted=%s, workerJoined=%s",
        g_rtspLoopStopped ? "true":"false", g_serverThreadJoined ? "true":"false", g_sessionClosed ? "true":"false",
        g_rtspClosed ? "true":"false", g_envReclaimed ? "true":"false", g_schedulerDeleted ? "true":"false", g_workerJoined ? "true":"false");
}

void NpuPlayAudioJob::wait() {
    if (_thread && _thread->joinable()) {
        LOGI("[Job] joining worker thread...");
        _thread->join();
        LOGI("[Job] worker thread joined");
    }
}
