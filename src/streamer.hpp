#pragma once

#include <unistd.h>
#include <opencv2/core.hpp>
#include <mutex>
#include <condition_variable>

#include "vision.hpp"
#include "DataComm.hpp"
#include "VideoHandler.hpp"


class Streamer {	
	cv::Mat image;

	struct GstInstance {
		pid_t pid;
		std::string file;
		std::string command;
	};
	std::vector<GstInstance> gstInstances;
	
	volatile bool handlingLaunchRequest = false;
	void launchGStreamer(const char* recieveAddress, int bitrate, std::string port, std::string file);

	std::string visionCameraDev, secondCameraDev, loopbackDev;

	pid_t ffmpegPID = 0;
	int servFd;

	//std::vector<VisionTarget> drawTargets;
	std::vector<cv::Point> drawTargets;
	DataComm* computer_udp = nullptr;

	VideoWriter videoWriter;

	VideoReader camera;
	int cameraIdx;

public:
	int width, height;

	//void setDrawTargets(std::vector<VisionTarget>* drawPoints);
	void setDrawTargets(std::vector<cv::Point>* drawPoints);
	
	void start();
	
	void handleCrash(pid_t pid);
	
	cv::Mat getBGRFrame();

	void run(std::function<void(void)> frameNotifier); // run thread
	void launchFFmpeg(); // for loopback video

	bool lowExposure = false;
	void setLowExposure(bool value);
};
extern int clientFd; 
