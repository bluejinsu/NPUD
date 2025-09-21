#ifndef DDC_HANDLER_H
#define DDC_HANDLER_H

#include <stddef.h>

class IDDCHandler {
public:
	virtual void onInitialized() = 0;
	virtual void onClosed() = 0;

	virtual void onReadFrame(int total_frame, int frame_num) = 0;
	virtual void onFullBuffer(time_t timestamp, float* ddc_iq, size_t ddc_samples) = 0;
};

#endif