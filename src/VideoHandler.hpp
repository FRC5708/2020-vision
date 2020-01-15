#pragma once

#include <vector>
#include <chrono>

#include <opencv2/core.hpp>
#include <linux/videodev2.h>

// Some helper classes to interface with the Video4Linux (V4L2) API

// Reads video from a camera
class VideoReader {
	int camfd;
	void* currentBuffer;
	std::vector<void*> buffers;
	struct v4l2_buffer bufferinfo;
	std::chrono::time_point last_update;
	std::chrono::steady_clock timeout_clock; 
	struct v4l2_requestbuffers bufrequest; //Stuff needs this.

	void setExposureVals(bool isAuto, int exposure);
	void resetTimeout();
	auto ioctl_timeout = std::chrono::milliseconds(200) // 200 milliseconds @ 24 fpms > 4 frames.
	bool resetFlag=false;

public:
	// size of the video
	int width, height;

	void openReader(int width, int height, const char* file);

	// Get the most-recently-grabbed frame in an opencv Mat.
	// The data is not copied.
	cv::Mat getMat();

	// Grab the next frame from the camera. 
    void grabFrame(bool firstTime = false);

	// Turns off auto-exposure (on by default) and sets the exposure manually. 
	// value is a camera-specific integer. 50 is "kinda dark" for our cameras.
	void setExposure(int value) { setExposureVals(false, value); }
	// Turns on auto-exposure.
	void setAutoExposure() { setExposureVals(true, 50); }


};

// Writes video to a v4l2-loopback device
class VideoWriter {
	unsigned int vidsendsiz;
	int v4l2lo;

public:

	void openWriter(int width, int height, const char* file);
    void writeFrame(cv::Mat& frame);
};