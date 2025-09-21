#include <iostream>
#include <vector>
#include <samplerate.h>
#include <cmath>
#include <algorithm>

// Class to manage streaming resampling with libsamplerate
class StreamingResampler {
public:
    StreamingResampler(double inputRate, double outputRate)
        : ratio(outputRate / inputRate), state(nullptr) {
        int error;
        state = src_new(SRC_SINC_BEST_QUALITY, 1, &error);
        if (!state) {
            throw std::runtime_error(src_strerror(error));
        }
    }

    ~StreamingResampler() {
        if (state) {
            src_delete(state);
        }
    }

    std::vector<short> process(const std::vector<float>& input, bool endOfInput = false) {

        // Estimate output size conservatively
        size_t outputSize = static_cast<size_t>(input.size() * ratio) + 1;
        std::vector<float> outputFloat(outputSize);

        // Setup SRC_DATA
        SRC_DATA srcData;
        srcData.data_in = input.data();
        srcData.input_frames = input.size();
        srcData.data_out = outputFloat.data();
        srcData.output_frames = outputSize;
        srcData.src_ratio = ratio;
        srcData.end_of_input = endOfInput ? 1 : 0;

        // Perform resampling
        int error = src_process(state, &srcData);
        if (error) {
            throw std::runtime_error(src_strerror(error));
        }

        // Convert the output float data back to short
        std::vector<short> output(static_cast<size_t>(srcData.output_frames_gen));
        for (size_t i = 0; i < output.size(); ++i) {
            output[i] = static_cast<short>(outputFloat[i]);
        }

        return output;
    }

private:
    double ratio;
    SRC_STATE* state;
};
