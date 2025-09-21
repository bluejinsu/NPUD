#include "FirFilter.h"

#include <memory.h>

FirFilter::FirFilter() {
}

FirFilter::~FirFilter() {
}

void FirFilter::Init(int dataSize) {
    int size = getFilterLength() + dataSize - 1;
    m_insamp.resize(size);
    memset(&m_insamp[0], 0, size);
}

// the FIR filter function
void FirFilter::Filter(float *input, float *output, int length) {
    int filterLength = getFilterLength();
    float acc;      // accumulator for MACs
    float *coeffp;  // pointer to coefficients
    float *inputp;  // pointer to input samples
    int n;
    int k;

    // put the new samples at the high end of the buffer
    memcpy(&m_insamp[filterLength - 1], input,
        length * sizeof(float));

    // apply the filter to each input sample
    for (n = 0; n < length; n++) {
        // calculate output n
        coeffp = getCoefficients();
        inputp = &m_insamp[filterLength - 1 + n];
        acc = 0;
        for (k = 0; k < filterLength; k++) {
            acc += (*coeffp++) * (*inputp--);
        }
        output[n] = acc;
    }
    // shift input samples back in time for next time
    memmove(&m_insamp[0], &m_insamp[length],
        (filterLength - 1) * sizeof(float));
}

void FirFilter::intToFloat(int16_t *input, double *output, int length) {
    int i;

    for (i = 0; i < length; i++) {
        output[i] = static_cast<double>(input[i]);
    }
}

void FirFilter::floatToInt(double *input, int16_t *output, int length) {
    int i;

    for (i = 0; i < length; i++) {
        if (input[i] > 32767.0) {
            input[i] = 32767.0;
        } else if (input[i] < -32768.0) {
            input[i] = -32768.0;
        }
        // convert
        output[i] = (int16_t)input[i];
    }
}

void FirFilter::PolyphaseFilter(const float *input, float *output, int length, int decimation) {
    int filterLength = getFilterLength();
    float acc;      // accumulator for MACs
    float *coeffp;  // pointer to coefficients
    float *m_coeffs = getCoefficients();
    float *inputp;  // pointer to input samples
    int n;
    int k;

    // put the new samples at the high end of the buffer
    memcpy(&m_insamp[filterLength - 1], input,
        length * sizeof(float));

    // apply the filter to each input sample
    for (n = 0; n < length; n += decimation) {
        // calculate output n
        coeffp = m_coeffs;
        inputp = &m_insamp[filterLength - 1 + n];
        acc = 0;
        for (k = 0; k < filterLength; k++) {
            acc += (*coeffp++) * (*inputp--);
        }
        output[n / decimation] = acc;
    }
    // shift input samples back in time for next time
    memmove(&m_insamp[0], &m_insamp[length],
        (filterLength - 1) * sizeof(float));
}
