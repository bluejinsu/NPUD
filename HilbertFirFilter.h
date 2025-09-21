#ifndef HILBERT_FIR_FILTER_H
#define HILBERT_FIR_FILTER_H

#include "FirFilter.h"

class HilbertFirFilter : public FirFilter {
    private:
       static const int FILTER_LEN = 61;
       static float m_coeffs[FILTER_LEN];
   
    public:
       virtual int getFilterLength();
       virtual float* getCoefficients();
   };

#endif