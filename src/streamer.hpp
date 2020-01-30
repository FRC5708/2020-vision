#pragma once

#include <unistd.h>
#include <opencv2/core.hpp>
#include <mutex>
#include <condition_variable>

#include <memory>

#include "vision.hpp"
#include "DataComm.hpp"
#include "VideoHandler.hpp"
#include <string>

// Starts and manages gStreamer processes
// Also intercepts the video stream from one camera to feed into vision processing
class Streamer {	
	cv::Mat image;

	// Holds info about one gStreamer process. 
	struct GstInstance {
		pid_t pid;
		std::string command;
	};
	std::vector<GstInstance> gstInstances;
	
	volatile bool handlingLaunchRequest = false;
	void launchGStreamer(int width, int height, const char* recieveAddress, int bitrate, std::string port, std::string file);

	std::vector<std::string> cameraDevs;

	
	std::string loopbackDev;

	int servFd;

	DataComm* computer_udp = nullptr;

	VideoWriter videoWriter;

	ThreadedVideoReader* visionCamera;

	std::vector<std::unique_ptr<ThreadedVideoReader>> cameraReaders;

	// The thread that listens for the signal from the driver station
	void dsListener();

	//void Streamer::gotCameraFrame();
	void pushFrame(int i);
	
	// Checks if we are read to write the framebuffer. It first creates a list of "synchronization cameras", which are running at the highest framerate of all the cameras (cameras sometimes reduce their framerate in order to increase exposure times) and are not "dead". A camera is considered "dead" if it's running significantly below 15 fps or a frame has not been recieved since 1.5*<average frame interval> ago. If a frame has been recieved from all of them, return true.
	bool checkFramebufferReadiness(); 
	std::mutex frameLock; // required in order to read from the public flags of ThreadedVideoReader
	std::vector<bool> readyState;
	bool initialized=false;

	std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock().now();
	int frameCount = 0;
	std::vector<int> cameraFrameCounts;
	
	void setupFramebuffer();


public:
	Streamer(std::function<void(void)>);
	int outputWidth, outputHeight, correctedWidth, correctedHeight;
	int getVisionCameraWidth() { return visionCamera->getWidth(); }
	int getVisionCameraHeight() { return visionCamera->getHeight(); }

	// Every frame from the vision camera will be passed to this function before being passed to gStreamer.
	void (*annotateFrame)(cv::Mat) = nullptr;
	
	// Initializes streamer, scanning for cameras and setting up a socket that listens for the client
	void start();
	
	// Should be called when SIGCHLD is recieved
	void handleCrash(pid_t pid);
	
	// Gets a video frame which is converted to the blue-green-red format usually used by opencv
	cv::Mat getBGRFrame();

	// visionFrameNotifier is called every new frame from the vision camera.
	//visionFrameNotifier is a callback function whose purpose is to let our vision thread know that it has new data.
	std::function<void(void)> visionFrameNotifier; 

	bool lowExposure = false;
	void setLowExposure(bool value);
	cv::Mat frameBuffer;
	std::string parseControlMessage(char * commandMessage); //Callback function passed into ControlPacketReceiver. Parses control messages to send to appropriate camera/s
	std::string controlMessage(std::string camera, std::string command);
};
extern int clientFd;

void interceptStdio(int toFd, std::string prefix);
