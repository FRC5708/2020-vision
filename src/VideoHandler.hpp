#pragma once

#include <vector>
#include <chrono>
#include <mutex>
#include <thread>

#include <opencv2/core.hpp>
#include <linux/videodev2.h>

// Some helper classes to interface with the Video4Linux (V4L2) API


/* struct resolution
** Holds a single v4l2 supported resolution from a camera
** The type property specifies the type of the resolution (Either V4L2_FRMSIZE_TYPE_DISCRETE, V4L2_FRMSIZE_TYPE_STEPWISE, V4L2_FRMSIZE_TYPE_DISCRETE, or V4L2_FRMSIZE_TYPE_CONTINUOUS)
** (Currently, this will only ever hold discrete resolutions. This is because queryResolutions simply discards any non-discrete values)
*/
struct resolution {
	int type;
	v4l2_frmsize_discrete discrete; 
	v4l2_frmsize_stepwise stepwise; //Currently unused. (queryResolutions simply discards stepwise resolution values right now)
};

/* class VideoReader
** VideoReader is a simple class that initializes and encapsulates a v4l2 videocamera device.
** It is unwise to use this directly, as functions like grabFrame can hang indefinitely. 
*/
class VideoReader {

private: //These are internal and should not be mucked about with.
	int camfd;
	void* currentBuffer;
	std::vector<void*> buffers;
	struct v4l2_buffer bufferinfo; 
	struct v4l2_requestbuffers bufrequest; // Not modified outside of openReader(). 

protected: //Should not be directly called. (ThreadedVideoReader uses these)
	bool hasFirstFrame = false;
	void setExposureVals(bool isAuto, int exposure);
	void openReader(bool isClosed = true);
	bool tryOpenReader(bool isClosed);
	void stopStreaming();
	void closeReader();
	int width, height; // size of the video
	void queryResolutions(); //Find (and cache in VideoReader::resolutions!) what resolutions our v4l2 device supports.
	bool hasResolutions=false; //Kind of jank, but the above function should only get called once. (This is protected, not private in case we want to undo this restriction for some reason)
	std::vector<resolution> resolutions; //We only save discrete resolutions right now.
	bool grabFrame(); // Grab the next frame from the camera.
	const std::string deviceFile; //Name of camera

public:
	virtual void reset(bool hard = false); //Actually resets the camera. (Should this be public? This should probably not be called willy-nilly, but it's useful.)
	VideoReader(int width, int height, const char* file);
	virtual ~VideoReader();
	cv::Mat getMat(); // Get the most-recently-grabbed frame in an opencv Mat. The data is not copied.
	int getWidth();
	int getHeight(); 
	/* Turns off auto-exposure (on by default) and sets the exposure manually. 
	** value is a camera-specific integer. 50 is "kinda dark" for our cameras.
	*/
	void setExposure(int value) { setExposureVals(false, value); }
	void setAutoExposure() { setExposureVals(true, 50); } // Turns on auto-exposure.
	class NotInitializedException : public std::exception {};
	const std::string getName(){return deviceFile;}
};

/* class ThreadedVideoReader: public VideoReader
** ThreadedVideoReader extends VideoReader via a callback-based thread safe approach.
** It also runs a monitor that automatically resets the camera if it exceeds the new frame timeout.
*/
class ThreadedVideoReader : public VideoReader {

private:
std::chrono::steady_clock::time_point last_update;
std::function<void(void)> newFrameCallback; //Callback function called whenever we succesfully get a new frame.
std::chrono::steady_clock timeout_clock;
static constexpr int frameTimeCount = 100;
std::chrono::steady_clock::time_point frameTimes[frameTimeCount]; // Ring buffer with last frameTimeCount frame times
int frameTimeIdx = 0;
void resetterMonitor(); //Monitors to see if camera should be reset, calls reset() if it should be so. (Started automatically within constructor)
static constexpr std::chrono::steady_clock::duration ioctl_timeout = std::chrono::milliseconds(5000);
std::mutex resetLock;
std::thread resetTimeoutThread, mainLoopThread; //Keep ahold of the thread handles

protected:
bool grabFrame(); //Thread-safe wrapper for VideoReader::grabFrame()

public:
	ThreadedVideoReader(int width, int height,const char* file, std::function<void(void)> newFrameCallback);
	virtual ~ThreadedVideoReader() {}; //Does nothing; required to compile?
	int setResolution(unsigned int width, unsigned int height);
	void reset(bool hard = false) override; //Wrapper for VideoReader reset(). (Should this be public? This should probably not be called willy-nilly, but it's useful.)
	const std::chrono::steady_clock::time_point getLastUpdate();
	double getMeanFrameInterval(); // Get a rolling average of the frame interval from the past second, which includes the time since the most recent frame

};


// Writes video to a v4l2-loopback device
class VideoWriter {
	unsigned int vidsendsiz;
	int v4l2lo;

public:
	void openWriter(int width, int height, const char* file);
	void closeWriter();
    void writeFrame(cv::Mat& frame);
};