#pragma once

#include <vector>
#include <chrono>
#include <mutex>

#include <opencv2/core.hpp>
#include <linux/videodev2.h>

// Some helper classes to interface with the Video4Linux (V4L2) API



// Reads video from a camera
class VideoReader {

protected:
	int camfd;
	void* currentBuffer;
	std::vector<void*> buffers;
	struct v4l2_buffer bufferinfo; 
	struct v4l2_requestbuffers bufrequest; //Stuff needs this.

	void startStreaming();
	void setExposureVals(bool isAuto, int exposure);

public:
	// size of the video
	int width, height;
	std::string deviceFile;

	VideoReader(int width, int height, const char* file);
	virtual ~VideoReader();

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

class ThreadedVideoReader : public VideoReader {
public:
	ThreadedVideoReader(int width, int height, const char* file, std::function<void(void)> newFrameCallback);
	void grabFrame(bool firstTime = false);
	std::function<void(void)> newFrameCallback;
	std::chrono::steady_clock::time_point last_update;
	volatile bool hasNewFrame = false;
    
    

private:
	std::chrono::steady_clock timeout_clock;

	void resetTimeout();
	void mainLoop();
	// Only reset the camera if it's been dead for over a second.
	static constexpr std::chrono::steady_clock::duration ioctl_timeout = std::chrono::milliseconds(1000); 
	std::mutex resetLock;
	std::thread resetTimeoutThread, mainLoopThread; //Keep ahold of the thread handle
};


// Writes video to a v4l2-loopback device
class VideoWriter {
	unsigned int vidsendsiz;
	int v4l2lo;

public:

	void openWriter(int width, int height, const char* file);
    void writeFrame(cv::Mat& frame);
};