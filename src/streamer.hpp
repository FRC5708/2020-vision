#pragma once

#include <unistd.h>
#include <opencv2/core.hpp>
#include <mutex>
#include <condition_variable>

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

	VideoReader* visionCamera;

	std::vector<VideoReader> cameraReaders;

	void gotCameraFrame(int cameraId);
	void pushFrame();

	std::mutex cameraFlagsLock;

	struct cameraFlagsT {
		bool newFrame = false;
		std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
	};
	std::vector<volatile cameraFlagsT> cameraFlags;

public:
	int width, height, outputWidth, outputHeight, correctedWidth, correctedHeight;

	// Every frame from the vision camera will be passed to this function before being passed to gStreamer.
	void (*annotateFrame)(cv::Mat) = nullptr;
	
	// Initializes streamer, scanning for cameras and setting up a socket that listens for the client
	void start();
	
	// Should be called when SIGCHLD is recieved
	void handleCrash(pid_t pid);
	
	// Gets a video frame which is converted to the blue-green-red format usually used by opencv
	cv::Mat getBGRFrame();

	// frameNotifier is called every frame
	std::function<void(void)> frameNotifier;
	// Runs the thread that grabs and forwards frames from the vision camera
	void run(); 

	bool lowExposure = false;
	void setLowExposure(bool value);
};
extern int clientFd; 