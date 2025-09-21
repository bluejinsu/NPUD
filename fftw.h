// Copyright 2022 <LAONSYSTEMS>
#pragma once
#include "fftw3.h"
#include <boost/function.hpp>
#include <boost/shared_array.hpp>

typedef struct {
    boost::shared_array<int16_t> data;
    uint64_t samples;
} stADData;

typedef struct {
    boost::shared_array<int32_t> iq;
    uint64_t samples;
    uint64_t fsHz;
    uint64_t bwHz;
} stIQData;

typedef struct {
    boost::shared_array<float> data;
    uint64_t samples;
} stSpectrum;

typedef stSpectrum(FnFftAD)(stADData);
typedef stSpectrum(FnFftIQ)(stIQData);

class FnfftAD {
    fftw_complex* m_in, * m_out;
    fftw_plan m_fft_plan;

 public:
    explicit FnfftAD(size_t samples);
    ~FnfftAD();
    boost::function<FnFftAD> operator()(size_t samples, double lvOffset);
};

class FnfftIQ {
    fftw_complex* m_in, * m_out;
    fftw_plan m_fft_plan;

 public:
    explicit FnfftIQ(size_t samples);
    ~FnfftIQ();
    boost::function<FnFftIQ> operator()(size_t samples, double lvOffset, bool bShowGuard);
};
