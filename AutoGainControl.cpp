#include "AutoGainControl.h"

#include <math.h>

inline float AutoGainControl::agcDetector(float input, float* sum, double state[], unsigned int* idx, int N)
{
    unsigned int first;
    unsigned int nth;
    float output;
    float val = abs(input) * abs(input);
    *sum += val;
    output = (*sum) * (1.0 / N);
    *sum -= state[*idx - 1];

    if (*sum < 0.0) { *sum = 0.0; }

    state[*idx - 1] = val;
    first = *idx;
    nth = first + 1;

    if (nth < first) { nth = 0xFFFFFFFF; }

    *idx = nth;

    if (*idx > N - 1) { *idx = 1; }

    return output;
}

AutoGainControl::AutoGainControl(double step, double desired_pwr, int avg_len, double max_pwr)
    : _step(step)
    , _desired_pwr(desired_pwr)
    , _avg_len(avg_len)
    , _max_pwr(max_pwr)
{
    _idx = 1;
    filt_len = _avg_len - 1;
    _K = _step;
    _g = 0;
    _sum = 0;
    _dtctr = 0;
    _filterState = new double[filt_len];

    for (int i = 0; i < filt_len; i++) { _filterState[i] = 0; }
}

float AutoGainControl::operator()(float input)
{
    _dtctr = agcDetector(input, &_sum, _filterState, &_idx, _avg_len);
    float out = input * exp(_g);
    _dtctr = log(_dtctr);
    _g += _K * (_desired_pwr - (_dtctr + 2.0 * _g));
    if (_g > _max_pwr) { _g = _max_pwr; }
    return out;
}

void AutoGainControl::process(float* input, float* output, int len)
{
    unsigned int idx = 1;
    int filt_len = _avg_len - 1;
    double K = _step;
    double g = 0;
    float sum = 0;
    double dtctr = 0;
    double* filterState = new double[filt_len];

    for (int i = 0; i < filt_len; i++) { filterState[i] = 0; }

    for (int i = 0; i < len; i++)
    {
        dtctr = agcDetector(input[i], &sum, filterState, &idx, _avg_len);
        float out = input[i] * exp(g);
        output[i] = out;
        dtctr = log(dtctr);
        g += K * (_desired_pwr - (dtctr + 2.0 * g));
        if (g > _max_pwr) { g = _max_pwr; }
    }

    delete[] filterState;
}
