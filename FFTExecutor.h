#pragma once

#include "fftw.h"

class FFTExecutor
{
private:
    fftw_complex* _input;
    fftw_complex* _output;
    int _length;
    fftw_plan _plan;

public:
    FFTExecutor();
    ~FFTExecutor();

    void init(int fft_length);
    void destroy();
    void execute(double* input, double* output);
};

