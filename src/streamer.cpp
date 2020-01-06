#include "streamer.hpp"
#include "DataComm.hpp"

#include <string>
#include <iostream>
#include <fcntl.h>
#include <chrono>
#include <thread>

#include <opencv2/imgproc.hpp>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>


using std::cout; using std::endl; using std::string; using std::vector;

pid_t runCommandAsync(const std::string& cmd, int closeFd) {
	pid_t pid = fork();
	
	if (pid == 0) {
		// child
		close(closeFd);

		string execCmd = ("exec " + cmd);
		cout << "> " << execCmd << endl;
		
		const char *argv[] = {
			"/bin/sh",
			"-c",
			execCmd.c_str(),
			nullptr
		};
		
		execv("/bin/sh", (char *const* )argv);
		exit(127);
	}
	else return pid;
}

// Called when SIGCHLD is recieved
void Streamer::handleCrash(pid_t pid) {
	if (!handlingLaunchRequest) {
		for (auto i : gstInstances) {
			if (i.pid == pid) {
				runCommandAsync(i.command, servFd);
			}
		}
	}
}

void addSrc(std::stringstream& cmd, const string file, int width, int height) {
	cmd << " v4l2src device=" << file << 
	" video/x-raw-yuv, width=" << width << ",height=" << height
	 << ",framerate=30/1 ! videoscale ! videoconvert "; // might need queue at the end
}

void Streamer::launchGStreamer(const char* recieveAddress, int bitrate, string port, vector<string> files) {
	cout << "launching GStreamer, targeting " << recieveAddress << endl;
	assert(files.size() > 0);

	// Codec is specific to the raspberry pi's gpu
	string codec = "omxh264enc";
	string gstreamCommand = "gst-launch-1.0";	

	std::stringstream command;

	command << gstreamCommand;
	addSrc(command, files[0], width, height);

	int outputWidth, outputHeight;

	// https://www.technomancy.org/gstreamer/playing-two-videos-side-by-side/

	if (files.size() == 1) {
		outputWidth = width; outputHeight = height;
	}
	else if (files.size() == 2) {
		outputWidth = width*2; outputHeight = height;

		command << " ! videobox right=-" << width << " ! videomixer name=mix ";
	}
	else if (files.size() == 3 || files.size() == 4) {
		outputWidth = width*2; outputHeight = height*2;
		command << " ! videobox right=-" << width << " bottom=-" << height
		 << " ! videomixer name=mix ";
	}
	else {
		std::cerr << "too many cameras!" << std::endl;
		return;
	}

	command << " ! queue ! " << codec << " target-bitrate=" << bitrate <<
	" control-rate=variable ! video/x-h264, width=" << outputWidth << ",height=" << outputHeight 
	<< ",framerate=30/1,profile=high ! rtph264pay ! gdppay ! udpsink"
	<< " host=" << recieveAddress << " port=" << port;

	if (files.size() == 2) {
		addSrc(command, files[1], width, height);
		command << " ! videobox left=-" << width << " ! mix ";
	}
	else if (files.size() == 3 || files.size() == 4) {
		addSrc(command, files[1], width, height);
		command << " ! videobox left=-" << width << " bottom=-" << height << " ! mix ";
		
		addSrc(command, files[2], width, height);
		command << " ! videobox right=-" << width << " top=-" << height << " ! mix ";

		if (files.size() == 4) {
			addSrc(command, files[2], width, height);
			command << " ! videobox left=-" << width << " top=-" << height << " ! mix ";
		}
	}

	string strCommand = command.str();
	
	pid_t pid = runCommandAsync(strCommand, servFd);

	gstInstances.push_back({ pid, strCommand });
}

// Finds a video device whose name contains cmp
string getVideoDeviceWithString(string cmp) {

	FILE* videos = popen(("for I in /sys/class/video4linux/*; do if grep -q '" 
	+ cmp + "' $I/name; then basename $I; exit; fi; done").c_str(), "r");

	char output[1035];
	string devname;
	while (fgets(output, sizeof(output), videos) != NULL) {
		// videoX
		if (strlen(output) >= 6) devname = output;
		// remove newlines
		devname.erase(std::remove(devname.begin(), devname.end(), '\n'), devname.end());
	}
	pclose(videos);
	if (!devname.empty()) return "/dev/" + devname;
	else return "";
}

void Streamer::start() {
	
	// These are the model numbers of our cameras
	cameraDevs.push_back(getVideoDeviceWithString("920"));
	cameraDevs.push_back(getVideoDeviceWithString("C525"));
	
	loopbackDev = getVideoDeviceWithString("Dummy");

	
	std::cout << "Cameras detected: " << cameraDevs.size();
		
	if (cameraDevs.size() == 0) {
		std::cerr << "Camera not found" << std::endl;
		exit(1);
	}
	
	std::cout << "main (vision) camera: " << cameraDevs[0] << std::endl;
	for (int i = 0; i < cameraDevs.size(); ++i) {
		std::cout << "Camera " << i << ": " << cameraDevs[i] << std::endl;
	}

	if (cameraDevs.size() == 1) {
		this->width = 800; this->height = 448;
	}
	else {
		this->width = 640; this->height = 360;
	}
	if (loopbackDev.empty()) {
		std::cerr << "v4l2loopback device not found" << std::endl;
		exit(1);
	}
	else std::cout << "video loopback device: " << loopbackDev << std::endl;

	visionCamera.openReader(width, height, cameraDevs[0].c_str());
	videoWriter.openWriter(width, height, loopbackDev.c_str());

	// Start the thread that listens for the signal from the driver station
	std::thread([this]() {
		
		servFd = socket(AF_INET6, SOCK_STREAM, 0);
		if (servFd < 0) {
			perror("socket");
			return;
		}
		
		int flag = 1;
		if (setsockopt(servFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
			perror("setsockopt");
		}
		
		struct sockaddr_in6 servAddr;
		memset(&servAddr, 0, sizeof(servAddr));
		
		servAddr.sin6_family = AF_INET6;
		servAddr.sin6_addr = in6addr_any;
		servAddr.sin6_port = htons(5807); //Port that resets magic
	
		if (bind(servFd, (struct sockaddr*)&servAddr, sizeof(servAddr)) == -1) {
			perror("bind");
			return;
		}
		if (listen(servFd, 10)== -1) {
			perror("listen");
			return;
		}
		
		while (true) {
			struct sockaddr_in6 clientAddr;
			socklen_t clientAddrLen = sizeof(clientAddr);
			int clientFd = accept(servFd, (struct sockaddr*) &clientAddr, &clientAddrLen);
			if (clientFd < 0) {
				perror("accept");
				continue;
			}

			// At this point, a connection has been recieved from the driver station.
			// gStreamer will now be set up to stream to the driver station.

			handlingLaunchRequest = true;

			for (auto i : gstInstances) {
				
				cout << "killing previous instance: " << i.pid << "   " << endl;
				if (kill(i.pid, SIGTERM) == -1) {
					perror("kill");
				}
				waitpid(i.pid, nullptr, 0);
			}
			
			char bitrate[16];
			ssize_t len = read(clientFd, bitrate, sizeof(bitrate));
			bitrate[len] = '\0';
			
			const char message[] = "Launching remote GStreamer...\n";
			if (write(clientFd, message, sizeof(message)) == -1) {
				perror("write");
			}

			if (close(clientFd) == -1) perror("close");
			

			// wait for client's gstreamer to initialize
			sleep(2);

			char strAddr[INET6_ADDRSTRLEN];
			getnameinfo((struct sockaddr *) &clientAddr, sizeof(clientAddr), strAddr,sizeof(strAddr),
    		0,0,NI_NUMERICHOST);

			vector<string> outputVideoDevs = cameraDevs;
			outputVideoDevs[0] = loopbackDev;
			launchGStreamer(strAddr, atoi(bitrate), "5809", outputVideoDevs);

			// It was planned to draw an overlay over the video feed in the driver station.
			// This sends the overlay data.
			cout << "Starting UDP stream..." << endl;
			if (computer_udp) delete computer_udp;
			computer_udp = new DataComm(strAddr, "5806");

			handlingLaunchRequest = false;
		}
	}).detach();
	
}

void Streamer::setDrawTargets(std::vector<VisionTarget>* drawTargets) {
	this->drawTargets = *drawTargets;
	//if (computer_udp) computer_udp->sendDraw(&(*drawTargets)[0].drawPoints);
}

// get frame in the color space that the vision processing uses
cv::Mat Streamer::getBGRFrame() {
	cv::Mat frame;
	cvtColor(visionCamera.getMat(), frame, cv::COLOR_YUV2BGR_YUYV);
	return frame;
}

void Streamer::setLowExposure(bool value) {
	if (value != lowExposure) {
		lowExposure = value;
		if (lowExposure) {
			// probably could be lower, but it's probably sufficent
			visionCamera.setExposureVals(false, 50);
		}
		else visionCamera.setExposureVals(true, 50);
	}
}

void Streamer::run(std::function<void(void)> frameNotifier) {
	
	//std::chrono::steady_clock clock;
	while (true) {

		//auto startTime = clock.now();
		visionCamera.grabFrame();
		//auto writeStart = clock.now();
		//std::cout << "grabFrame took: " << std::chrono::duration_cast<std::chrono::milliseconds>
		//	(writeStart - startTime).count() << " ms" << endl;


		// Draw an overlay on the frame before handing it off to gStreamer
		cv::Mat drawnOn = visionCamera.getMat().clone();
		for (auto i = drawTargets.begin(); i < drawTargets.end(); ++i) {
			drawVisionPoints(i->drawPoints, drawnOn);
		}
	
		videoWriter.writeFrame(drawnOn);

		//std::cout << "drawing and writing took: " << std::chrono::duration_cast<std::chrono::milliseconds>
		//	(clock.now() - writeStart).count() << " ms" << endl;

		frameNotifier();
	}
	
}
