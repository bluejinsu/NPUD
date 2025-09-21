#ifndef NPU_PLAY_AUDIO_REQUEST_H
#define NPU_PLAY_AUDIO_REQUEST_H

#include <string>

#include <inttypes.h>
#include <time.h>

struct NpuPlayAudioRequest {
    int instance;
	int64_t frequency;
	int bandwidth;
	time_t starttime;
	time_t endtime;
	std::string demod_type;
	bool squelch_mode;
	float squelch_threshold;
	float scale;
};

#endif