#pragma once

#include <vector>
#include <chrono>
#include <mutex>
#include <thread>

#include <opencv2/core.hpp>
#include <linux/videodev2.h>

// Some helper classes to interface with the Video4Linux (V4L2) API


//Magic struct.
struct resolution {
	int type;
	v4l2_frmsize_discrete discrete; //Only one of these two is actually going to be initialized. FSCK unions.
	v4l2_frmsize_stepwise stepwise;
};

// Reads video from a camera
class VideoReader {

private: //These are internal and should not be mucked about with.
	int camfd;
	void* currentBuffer;
	std::vector<void*> buffers;
	struct v4l2_buffer bufferinfo; 
	struct v4l2_requestbuffers bufrequest; // Not modified outside of openReader()
	bool hasFirstFrame = false;
protected: //Should not be directly called. (ThreadedVideoReader uses these)
	void setExposureVals(bool isAuto, int exposure);
	void openReader();
	bool tryOpenReader();
	void closeReader();
	int width, height; // size of the video
	void queryResolutions(); //Find (and cache in VideoReader::resolutions!) what resolutions our v4l2 device supports.
	std::vector<resolution> resolutions; //Front is also discrete, back stepwise! This is not obvious.
public:
	void reset(); //Actually resets the camera. (Should this be public? This should probably not be called willy-nilly, but it's useful.)
	const std::string deviceFile;
	VideoReader(int width, int height, const char* file);
	virtual ~VideoReader();
	cv::Mat getMat(); // Get the most-recently-grabbed frame in an opencv Mat. The data is not copied.
    bool grabFrame(); // Grab the next frame from the camera.

	// Turns off auto-exposure (on by default) and sets the exposure manually. 
	// value is a camera-specific integer. 50 is "kinda dark" for our cameras.
	void setExposure(int value) { setExposureVals(false, value); }
	// Turns on auto-exposure.
	void setAutoExposure() { setExposureVals(true, 50); }
	class NotInitializedException : public std::exception {};
};


class ThreadedVideoReader : public VideoReader {
public:
	ThreadedVideoReader(int width, int height,const char* file, std::function<void(void)> newFrameCallback);
	virtual ~ThreadedVideoReader() {}; //Does nothing; required to compile?
	int setResolution(unsigned int width, unsigned int height);
	void reset(); //Wrapper for VideoReader reset(). (Should this be public? This should probably not be called willy-nilly, but it's useful.)
	const std::chrono::steady_clock::time_point getLastUpdate();
private:
	bool grabFrame(); //Thread-safe wrapper for VideoReader::grabFrame()
	std::chrono::steady_clock::time_point last_update;
	std::function<void(void)> newFrameCallback; //Callback function called whenever we succesfully get a new frame.
	std::chrono::steady_clock timeout_clock;
	void resetterMonitor(); //Monitors to see if camera should be reset, calls reset() if it should be so.
	static constexpr std::chrono::steady_clock::duration ioctl_timeout = std::chrono::milliseconds(5000); 	// Auto-reset timeout.
	std::mutex resetLock;
	std::thread resetTimeoutThread, mainLoopThread; //Keep ahold of the thread handles
};


// Writes video to a v4l2-loopback device
class VideoWriter {
	unsigned int vidsendsiz;
	int v4l2lo;

public:

	void openWriter(int width, int height, const char* file);
    void writeFrame(cv::Mat& frame);
};