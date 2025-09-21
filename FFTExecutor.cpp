#include "FFTExecutor.h"

FFTExecutor::FFTExecutor() {

}

FFTExecutor::~FFTExecutor() {

}

void FFTExecutor::init(int fft_length) {
    _length = fft_length;
    _input = (fftw_complex*)fftw_malloc(_length * sizeof(fftw_complex));
    _output = (fftw_complex*)fftw_malloc(_length * sizeof(fftw_complex));
    _plan = fftw_plan_dft_1d(_length, _input, _output, FFTW_FORWARD, FFTW_ESTIMATE);
}

void FFTExecutor::destroy() {
    fftw_free(_input);
    fftw_free(_output);
    fftw_destroy_plan(_plan);
}

void FFTExecutor::execute(double* input, double* output) {
    memcpy(_input, input, sizeof(fftw_complex) * _length);

    fftw_execute(_plan);

    memcpy(output, _output, sizeof(fftw_complex) * _length);
}