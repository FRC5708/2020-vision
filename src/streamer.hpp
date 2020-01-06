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
		std::string command;
	};
	std::vector<GstInstance> gstInstances;
	
	volatile bool handlingLaunchRequest = false;
	void launchGStreamer(const char* recieveAddress, int bitrate, std::string port, std::vector<std::string> files);

	//std::string visionCameraDev, secondCameraDev, loopbackDev;
	std::vector<std::string> cameraDevs;
	std::string loopbackDev;

	int servFd;

	std::vector<VisionTarget> drawTargets;
	DataComm* computer_udp = nullptr;

	VideoWriter videoWriter;

	VideoReader visionCamera;

public:
	int width, height;

	void setDrawTargets(std::vector<VisionTarget>* drawPoints);
	
	void start();
	
	void handleCrash(pid_t pid);
	
	cv::Mat getBGRFrame();

	void run(std::function<void(void)> frameNotifier); // run thread

	bool lowExposure = false;
	void setLowExposure(bool value);
};
extern int clientFd; 