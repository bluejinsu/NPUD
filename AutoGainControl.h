#ifndef AUTO_GAIN_CONTROL_H
#define AUTO_GAIN_CONTROL_H

class AutoGainControl {
private:
    double _step;
    double _desired_pwr;
    int _avg_len;
    double _max_pwr;
    unsigned int _idx;
    int filt_len;
    double _K;
    double _g;
    float _sum;
    double _dtctr;
    double* _filterState;

private:
    float agcDetector(float input, float* sum, double state[], unsigned int* idx, int N);

public:
    AutoGainControl(double step, double desired_pwr, int avg_len, double max_pwr);
    ~AutoGainControl() {
        delete[] _filterState;
    }

    float operator()(float input);

    void process(float* input, float* output, int len);
};
    

#endif