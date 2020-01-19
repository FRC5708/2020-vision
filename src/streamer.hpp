#pragma once

#include <unistd.h>
#include <opencv2/core.hpp>
#include <mutex>
#include <condition_variable>

#include <memory>

#include "vision.hpp"
#include "DataComm.hpp"
#include "VideoHandler.hpp"

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
	void launchGStreamer(int width, int height, const char* recieveAddress, int bitrate, std::string port, std::vector<std::string> files);

	std::vector<std::string> cameraDevs;

	
	std::string loopbackDev;

	int servFd;

	DataComm* computer_udp = nullptr;

	VideoWriter videoWriter;

	ThreadedVideoReader* visionCamera;

	std::vector<std::unique_ptr<ThreadedVideoReader>> cameraReaders;

	//void Streamer::gotCameraFrame();
	void pushFrame(int i);
	bool checkFramebufferReadiness(); //Check if we are read to write the framebuffer. If so, do so.
	std::mutex frameLock; // required in order to read from the public flags of ThreadedVideoReader
	std::vector<bool> readyState;

public:
	Streamer(std::function<void(void)>);
	int width, height, outputWidth, outputHeight, correctedWidth, correctedHeight;

	// Every frame from the vision camera will be passed to this function before being passed to gStreamer.
	void (*annotateFrame)(cv::Mat) = nullptr;
	
	// Initializes streamer, scanning for cameras and setting up a socket that listens for the client
	void start();
	
	// Should be called when SIGCHLD is recieved
	void handleCrash(pid_t pid);
	
	// Gets a video frame which is converted to the blue-green-red format usually used by opencv
	cv::Mat getBGRFrame();

	// visionFrameNotifier is called every new frame from the vision camera
	std::function<void(void)> visionFrameNotifier; //visionFrameNotifier is a callback function whose purpose is to let our vision thread know that it has new data.
	// Runs the thread that grabs and forwards frames from the vision camera
	void run(); 

	bool lowExposure = false;
	void setLowExposure(bool value);
	cv::Mat frameBuffer;
};
extern int clientFd; 