#pragma once

#include <unistd.h>
#include <opencv2/core.hpp>
#include <mutex>
#include <condition_variable>

#include <memory>

#include "DataComm.hpp"
#include "VideoHandler.hpp"
#include <string>

// Broadly split into two parts: managing the different cameras, and managing the gStreamer instance.
class Streamer {
	
	// Common initialization stuff:
public:
	Streamer(std::function<void(void)>, std::function<void(cv::Mat&)>);
	// Initializes streamer, scanning for cameras and setting up a socket that listens for the client
	void start();
	
	
	// Camera stuff:
public:
	// Gets a video frame which is converted to the blue-green-red format usually used by opencv
	cv::Mat getBGRFrame();

	// visionFrameNotifier is called every new frame from the vision camera.
	//visionFrameNotifier is a callback function whose purpose is to let our vision thread know that it has new data.
	std::function<void(void)> visionFrameNotifier; 
	// Every frame from the vision camera will be passed to this function before being passed to gStreamer.
	void (*annotateFrame)(cv::Mat) = nullptr;

	// These are used by main() to determine camera calibration parameters
	std::string visionCameraName;
	int getVisionCameraWidth() { return visionCamera->getWidth(); }
	int getVisionCameraHeight() { return visionCamera->getHeight(); }
	
	bool lowExposure = false;
	void setLowExposure(bool value);
	
private:
	// All the camera streams go into this buffer, then it's pushed to the VideoWriter
	cv::Mat frameBuffer;
	
	void setupCameras(); // Initializes the VideoReaders. (Only called once)
	void calculateOutputSize(); //Calculates and updates values of uncorrectedWidth, uncorrectedHeight
	// Sizes the framebuffer and sets the background.
	void setupFramebuffer();

	// outputWidth/Height are the size of the framebuffer which is outputted to VideoWriter. uncorrectedWidth/Height is what outputWidth/Height *would* be if the H.264 encoder on the raspberry pi was less buggy.
	int uncorrectedWidth, uncorrectedHeight, outputWidth, outputHeight;
	// False until setupCameras() is called
	bool initialized=false;
	
	// Camera device file paths (e.g. /dev/video1)
	std::vector<std::string> cameraDevs;
	std::string loopbackDev;
	
	std::vector<std::unique_ptr<ThreadedVideoReader>> cameraReaders;
	// Always cameraReaders[0]
	ThreadedVideoReader* visionCamera;
	
	
	VideoWriter videoWriter;
	void pushFrame(int i);
	// Checks if we are read to write the framebuffer. It first creates a list of "synchronization cameras", which are running at the highest framerate of all the cameras (cameras sometimes reduce their framerate in order to increase exposure times) and are not "dead". A camera is considered "dead" if it's running significantly below 15 fps or a frame has not been recieved since 1.5*<average frame interval> ago. If a frame has been recieved from all of them, return true.
	bool checkFramebufferReadiness(); 
	// required in order to read from the public flags of ThreadedVideoReader
	std::mutex frameLock; 
	// Indicates whether a frame has been recieved from each camera since the last frame was outputted to the VideoWriter.
	std::vector<bool> newFrames;

	// Time since framerate was printed to the console
	std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock().now();
	// Counts the number of frames recieved from each camera since the last time framerate was printed
	int frameCount = 0;
	// Counts the number of frames pushed to the VideoWriter since the last time framerate was printed
	std::vector<int> cameraFrameCounts;
	
	// Restarts VideoWriter, maybe with a different resolution.
	void restartWriter();
	
	
//------------------------------------------------------------------------------------------
	// gStreamer stuff:
private:
	std::string strAddr;
	int bitrate;

	pid_t gstreamer_pid=0;
	std::string gstreamer_command; //Do we actually need/want this to be saved?
	
	volatile bool handlingLaunchRequest = false;
	// The thread that listens for the signal from the driver station
	void dsListener();
	// File descriptor used by dsListener
	int servFd, clientFd;
	
	void launchGStreamer(int width, int height, const char* recieveAddress, int bitrate, std::string port, std::string file);
	void killGstreamerInstance();// Kill the previous instance of gsteamer, that we may start anew.

	// Stuff for persisting the gstreamer instance if the program crashes.
	static constexpr char GSTREAMER_PREVIOUS_PID_FILE_PATH[]="/tmp/5708_gstreamer_pid";
	pid_t get_previous_gstreamer_pid(); //Get PID of previous gst instance if it exists, return 0 otherwise.
	bool write_gstreamer_pid_to_file(); //Writes gstremer pid to file, returning false upon failure.	
	
public:
	// Should be called when SIGCHLD is recieved
	void handleCrash(pid_t pid);
	
// ------------------------------------------------------------------------------
	// Control message stuff, which doesn't fit in either category:
public:
	
	/*Control message syntax: CONTROL MESSAGE:Camerano1,[camerano2,...] 
	**Returned status syntax: 
	**	Camerano1:RETNO:STATUS MESSAGE
	**  Camerano2:RETNO:STATUS MESSAGE
	**  ...
	**If the original control message is completely unparseable, the return status is
	**  UNPARSABLE MESSAGE
	** RETNO is 0 upon success, something else upon failure (detrmined by videoHandler functions). The STATUS MESSAGE *SHOULD* return more information.
	
	Available control messages are: reset, resolution <width> <height>
	*/
	std::string parseControlMessage(std::string command, std::string arguments); 
private:
	std::string controlMessage(unsigned int camera, std::string command, std::string parameters);
};


// Some functions used by streamer that aren't part of the streamer class
pid_t runCommandAsync(const std::string& cmd);

void interceptStdio(int toFd, std::string prefix);
void interceptFile(int fromFd, int toFd, std::string prefix);

std::vector<std::string> getVideoDevicesWithString(std::string cmp);
std::vector<std::string> getLoopbackDevices();
