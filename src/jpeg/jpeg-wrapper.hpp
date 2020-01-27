#pragma once

#include <unistd.h>
#include <functional>
#include <condition_variable>
#include <opencv2/core.hpp>


struct userptr_t;
extern "C" {
	void decode_jpeg_video(userptr_t* userptr, size_t (*get_data)(void* buf, size_t size, userptr_t* user), void (*decoded_buffer)(void* buf, size_t length, userptr_t* user));
}


// TODO: Make it deconstruct gracefully, without potentially reading freed memory
class MJpegDecoder {
public:
	// Pushes a frame to the decoder, with a callback taking a frame in YUYV color space
	// buf should remain readable at least until addFrame() is called next
	void addFrame(void* buf, size_t len, std::function<void(cv::Mat)> callback);
	
	MJpegDecoder();

	//void* volatile encodedBuf = nullptr;
	std::vector<uint8_t> encodedBuf;
	std::function<void(cv::Mat)> currentCallback;
	//volatile size_t bufLen = 0;
	volatile size_t bufPos = 0;

	std::mutex waitMutex;
	std::condition_variable condVar;
};

