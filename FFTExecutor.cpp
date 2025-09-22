#include "FFTExecutor.h"
#include <cstring>   // memcpy

FFTExecutor::FFTExecutor() {

}

FFTExecutor::~FFTExecutor() {
    destroy();
}

void FFTExecutor::init(int fft_length) {
    // [CHANGE] 안전하게 재생성: 기존 리소스 정리
    destroy();

    _length = fft_length;
    _input  = (fftw_complex*)fftw_malloc(_length * sizeof(fftw_complex));
    _output = (fftw_complex*)fftw_malloc(_length * sizeof(fftw_complex));
    _plan   = fftw_plan_dft_1d(_length, _input, _output, FFTW_FORWARD, FFTW_ESTIMATE);
}

void FFTExecutor::destroy() {
    if (_plan)   { fftw_destroy_plan(_plan); _plan = nullptr; }
    if (_input)  { fftw_free(_input);  _input  = nullptr; }
    if (_output) { fftw_free(_output); _output = nullptr; }
    _length = 0;
}

// [CHANGE] 길이 가변 실행: 들어온 length에 맞춰 plan을 보장
void FFTExecutor::execute(const double* input, double* output, int length) {
    // 길이가 바뀌었으면 plan을 재생성
    if (length != _length) {
        init(length);
    }

    // input/output은 복소수 배열로서 double 2*length 개를 연속 저장한 형태
    std::memcpy(_input, input, sizeof(fftw_complex) * _length);
    fftw_execute(_plan);
    std::memcpy(output, _output, sizeof(fftw_complex) * _length);
}
