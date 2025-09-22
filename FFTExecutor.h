#pragma once

#include "fftw.h"
#include <cstddef>   // size_t
#include <cstdint>

class FFTExecutor
{
private:
    fftw_complex* _input = nullptr;
    fftw_complex* _output = nullptr;
    int _length = 0;
    fftw_plan _plan = nullptr;

public:
    FFTExecutor();
    ~FFTExecutor();

    // 기존 인터페이스 유지
    void init(int fft_length);
    void destroy();

    // [CHANGE] 길이 가변 실행을 위해 length 인자를 받는 execute 추가
    // (기존 execute(double*, double*)는 내부에서 _length 기준으로만 동작해
    //  길이가 달라질 때 SIGSEGV 원인이 되어 삭제/대체)
    void execute(const double* input, double* output, int length);

    // 필요 시 외부에서 현재 길이 확인할 수 있도록 헬퍼(선택)
    inline int length() const { return _length; }
};
