// NpuPlayAudioJob.cpp  (PATCH-2: lock RTP SR to constant 44.1kHz + 10ms fixed frames) 250919 03:36

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

// ========== [ADD] 파일 스코프: ServerMediaSession 보관(헤더 수정 없이 누수 방지) ==========
static ServerMediaSession* g_sms = nullptr;

// ===== (ADD) RTP/PCM 고정 샘플레이트 지정: 44,100 Hz =====
static constexpr unsigned kPCM_OUT_SR = 44100;

static constexpr unsigned kPCM_FRAME_MS = 10;
static constexpr size_t   kPCM_SAMPLES_PER_FRAME = (kPCM_OUT_SR * kPCM_FRAME_MS) / 1000;  // == 441

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

    // // 10ms 고정 프레임(= 4410 samples @ 44.1kHz). 부족분은 0 패딩.
    // bool getNextFrame(std::vector<short>& buffer) override {
    //     boost::lock_guard<boost::mutex> lock(_mtx);

    //     if (!_isStarted) return false;

    //     // 150ms 워밍업 버퍼가 쌓일 때까지 재생 보류
    //     const size_t warmupSamples =
    //         static_cast<size_t>((uint64_t)_out_samplerate * 150ULL / 1000ULL);
    //     if (!_warmedUp) {
    //         if (_data.size() < warmupSamples) return false; // 5ms 뒤 재시도(PCMFramedSource에서)
    //         _warmedUp = true;
    //     }

    //     // const size_t want = static_cast<size_t>(
    //     //     (uint64_t)_out_samplerate * _frame_ms / 1000ULL); // 441000

    //     // ★ 441 샘플 고정
    //     const size_t want = kPCM_SAMPLES_PER_FRAME; // 441
    //     buffer.resize(want);

    //     const size_t n = std::min(_data.size(), want);
    //     for (size_t i = 0; i < n; ++i) {
    //         buffer[i] = _data.front();
    //         _data.pop_front();
    //     }
    //     // 부족분 0 패딩 → 항상 10ms 길이 보장
    //     for (size_t i = n; i < want; ++i) {
    //         buffer[i] = 0;
    //     }
    //     return true;
    // }

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

    void onInitialized() override { _isStarted = true; }
    void onClosed() override      { _isStarted = false; }
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
        fft_executor.init((int)ddc_samples);
        std::vector<double> iq_data_d(ddc_samples * 2);
        std::vector<double> fft_output(ddc_samples * 2);
        for (int k = 0; k < (int)ddc_samples; ++k) {
            iq_data_d[2*k]     = (double)ddc_iq[2*k];
            iq_data_d[2*k + 1] = (double)ddc_iq[2*k + 1];
        }
        fft_executor.execute(iq_data_d.data(), fft_output.data());

        double max_power_level = -999.0;
        double fft_level_offset = 20 * log10((double)ddc_samples);

        for (int i = (int)ddc_samples/2; i < (int)ddc_samples; ++i) {
            double real = fft_output[2*i];     if (real == 0) real += 1e-7;
            double imag = fft_output[2*i + 1]; if (imag == 0) imag += 1e-7;
            double power = 10 * log10(real*real + imag*imag) + _level_offset - fft_level_offset;
            if (max_power_level < power) max_power_level = power;
        }
        for (int i = 0; i < (int)ddc_samples/2; ++i) {
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
                1000 /* 5ms */, (TaskFunc*)s_deliverFrame, this);
            return;
        }

    // 기대 프레임 샘플수(441)와 바이트수(882)
        const size_t expectedSamples = kPCM_SAMPLES_PER_FRAME; // 441
        const size_t expectedBytes   = expectedSamples * sizeof(short); // 882

        // 사용할 총 바이트(호스트 메모리 상 short[])
        size_t totalBytes = next_frame.size() * sizeof(short);

        // 수신자 버퍼 한계 반영 (샘플(2바이트) 경계로 정렬)
        size_t bytesToSend = totalBytes;

         // ★ fMaxSize가 882보다 작으면 잘림 → 가능한 만큼만 보냄 (로그 추천)
        if (fMaxSize < expectedBytes) {
            // envir() << "[PCM] WARN: fMaxSize(" << fMaxSize << ") < 882; truncating\n";
            bytesToSend = fMaxSize & ~static_cast<size_t>(1); // 샘플경계 정렬
            fNumTruncatedBytes = (unsigned)(totalBytes > bytesToSend ? totalBytes - bytesToSend : 0);
        } else {
            // 정상: 441 샘플만 송출 (혹시 next_frame이 더 커도 441로 제한)
            bytesToSend = expectedBytes;
            fNumTruncatedBytes = (unsigned)(totalBytes > bytesToSend ? totalBytes - bytesToSend : 0);
        }


        // if (bytesToSend > fMaxSize) {
        //     size_t clipped = fMaxSize & ~static_cast<size_t>(1);
        //     fNumTruncatedBytes = (unsigned)(totalBytes - clipped);
        //     bytesToSend = clipped;
        // } else {
        //     fNumTruncatedBytes = 0;
        // }

        // 실제 보낼 샘플 수 (모노 16-bit PCM → 2바이트/샘플)
        size_t samplesToSend = bytesToSend / 2;

        // (요청에 따라) 오더링 변환 부분은 기존 구현을 그대로 유지
        const uint16_t* src = reinterpret_cast<const uint16_t*>(next_frame.data());
        uint8_t* dst = fTo;

        for (size_t i = 0; i < samplesToSend; ++i) {
            uint16_t u = src[i];
            // (기존 구현 유지)
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
}

NpuPlayAudioJob::~NpuPlayAudioJob() {}

void NpuPlayAudioJob::startRtspServer(IPCMSource* pcm_source) {
    _scheduler = BasicTaskScheduler::createNew(0);
    _env = BasicUsageEnvironment::createNew(*_scheduler);

    _rtspServer = RTSPServer::createNew(*_env, _rtsp_port);
    if (_rtspServer == nullptr) {
        *_env << "Failed to create RTSP server: " << _env->getResultMsg() << "\n";
        return;
    }

    const char* streamName = "pcmSeekableStream";
    ServerMediaSession* sms = ServerMediaSession::createNew(*_env, streamName, streamName, "Seekable PCM audio stream");
    sms->addSubsession(PCMOnDemandSubsession::createNew(*_env, pcm_source, _play_audio_req.starttime));
    _rtspServer->addServerMediaSession(sms);

    // [ADD] 세션 포인터 보관 (헤더 수정 없이 누수 방지)
    g_sms = sms;

    *_env << "Play this stream using the URL: rtsp://" << _rtsp_ip.c_str() << ":" << _rtsp_port << "/" << streamName << "\n";

    _stopLoop = false;
    _serverThread = std::thread([this]() {
        _env->taskScheduler().doEventLoop((char*)&_stopLoop);
    });
}

void NpuPlayAudioJob::work() {
    float level_offset = static_cast<float>(atof(_config->getValue("DATA.LEVEL_OFFSET").c_str()));
    
    DDCExecutor ddc_exe;
    if (!ddc_exe.initDDC(_play_audio_req.starttime,
                         _play_audio_req.endtime,
                         _play_audio_req.frequency,
                         _play_audio_req.bandwidth,
                         _data_storage_info.get())) {
        return;
    }

    // [NOTE] 프리필 주석과 달리 현재 10% 대기 (필요시 조정)
    {
        // const size_t cap = ddc_exe.ringCapacity();
        // if (cap > 0) {
        //     const size_t want = (cap + 4)  * 0.1;
        //     ddc_exe.waitUntilReadable(want, std::chrono::seconds(2));
        // }
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

    // (새 메서드가 없으면 현재 로직 기준으로 고정 판단)
    int effective_in_sr = samplerate;
    //if(true) {
    if (ddc_exe.getDecimateRate() == 64) {        // <-- 없으면 간단한 getter 하나 추가
        effective_in_sr /= 2;
    }

    DemodConduit demod_conduit(effective_in_sr,//samplerate,
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

    // 정리: DDCExecutor 종료 (리더 스레드 포함)
    //ddc_exe.stop();
    ddc_exe.close();
}

std::string NpuPlayAudioJob::start(JOB_COMPLEDTED_CALLBACK completed_callback) {
    boost::lock_guard<boost::mutex> lock(_mtx);
    _guid = generateID();
    _completed_callback = completed_callback;
    _running = true;
    _thread.reset(new std::thread(std::bind(&NpuPlayAudioJob::work, shared_from_this())));
    return _guid;
}

void NpuPlayAudioJob::stop() {
    {
        boost::lock_guard<boost::mutex> lock(_mtx);
        _running = false;
    }

    // 종료 순서 고정: 1) 루프 종료 → 2) event loop 종료/조인 → 3) Medium::close
    _stopLoop = true;

    if (_serverThread.joinable()) {
        _serverThread.join();
    }

    if (g_sms) {
        Medium::close(g_sms);
        g_sms = nullptr;
    }
    if (_rtspServer) {
        Medium::close(_rtspServer);
        _rtspServer = nullptr;
    }

    if (_env) {
        _env->reclaim();
        _env = nullptr;
    }
    if (_scheduler) {
        delete _scheduler;
        _scheduler = nullptr;
    }

    // 작업 스레드 종료(마지막)
    wait();
}

void NpuPlayAudioJob::wait() {
    if (_thread && _thread->joinable()) {
        _thread->join();
    }
}
