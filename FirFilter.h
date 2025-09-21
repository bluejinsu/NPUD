#ifndef FIR_FILTER_H
#define FIR_FILTER_H

#include <vector>
#include <stdint.h>

class FirFilter {
    protected:
    // array to hold input samples
    std::vector<float> m_insamp;

    void intToFloat(int16_t *input, double *output, int length);
    void floatToInt(double *input, int16_t *output, int length);

    virtual int getFilterLength() = 0;
    virtual float* getCoefficients() = 0;

 public:
    FirFilter();
    virtual ~FirFilter();

    // FIR init
    void Init(int dataSize);

    // the FIR filter function
    void Filter(float *input, float *output, int length);
    void PolyphaseFilter(const float *input, float *output, int length, int decimation);
};

#endif