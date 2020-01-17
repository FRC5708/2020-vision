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

void Streamer::handleCrash(pid_t pid) {
	if (!handlingLaunchRequest) {
		for (auto i : gstInstances) {
			if (i.pid == pid) {
				runCommandAsync(i.command, servFd);
			}
		}
	}
}
Streamer::Streamer(std::function<void(void)> callback){
	visionFrameNotifier=callback;
}

void addSrc(std::stringstream& cmd, const string file, int width, int height) {
	cmd << " v4l2src device=" << file << 
	" ! video/x-raw,width=" << width << ",height=" << height
	 << ",framerate=30/1 ! videoconvert "; // might need queue at the end
}

void Streamer::launchGStreamer(int width, int height, const char* recieveAddress, int bitrate, string port, vector<string> files) {
	cout << "launching GStreamer, targeting " << recieveAddress << endl;
	assert(files.size() > 0);

	// Codec is specific to the raspberry pi's gpu
	string codec = "omxh264enc";
	string gstreamCommand = "gst-launch-1.0";	

	std::stringstream command;

	command << gstreamCommand;
	addSrc(command, files[0], width, height);

	//int outputWidth, outputHeight;

	// https://www.technomancy.org/gstreamer/playing-two-videos-side-by-side/

	int outputWidth, outputHeight;

	if (files.size() == 1) {
		outputWidth = width; outputHeight = height;
	}
	else if (files.size() == 2) {
		outputWidth = width*2; outputHeight = height;

		command << " ! videobox border-alpha=0 right=-" << width << " ! videomixer name=mix ";
	}
	else if (files.size() == 3 || files.size() == 4) {
		outputWidth = width*2; outputHeight = height*2;
		command << " ! videobox border-alpha=0 right=-" << width << " bottom=-" << height
		 << " ! videomixer name=mix ";
	}
	else {
		std::cerr << "too many cameras!" << std::endl;
		return;
	}

	command << "! videoconvert ! queue ! " << codec << " target-bitrate=" << bitrate <<
	" control-rate=variable ! video/x-h264, width=" << outputWidth << ",height=" << outputHeight 
	<< ",framerate=30/1,profile=high ! rtph264pay ! gdppay ! udpsink"
	<< " host=" << recieveAddress << " port=" << port;

	if (files.size() == 2) {
		addSrc(command, files[1], width, height);
		command << " ! videobox border-alpha=0 left=-" << width << " ! mix. ";
	}
	else if (files.size() == 3 || files.size() == 4) {
		addSrc(command, files[1], width, height);
		command << " ! videobox border-alpha=0 left=-" << width << " bottom=-" << height << " ! mix. ";
		
		addSrc(command, files[2], width, height);
		command << " ! videobox border-alpha=0 right=-" << width << " top=-" << height << " ! mix. ";

		if (files.size() == 4) {
			addSrc(command, files[2], width, height);
			command << " ! videobox border-alpha=0 left=-" << width << " top=-" << height << " ! mix. ";
		}
	}

	string strCommand = command.str();
	
	pid_t pid = runCommandAsync(strCommand, servFd);

	gstInstances.push_back({ pid, strCommand });
}

// Finds a video device whose name contains cmp
vector<string> getVideoDeviceWithString(string cmp) {

	FILE* videos = popen(("for I in /sys/class/video4linux/*; do if grep -q '" 
	+ cmp + "' $I/name; then basename $I; fi; done").c_str(), "r");

	vector<string> devnames;
	char output[1035];
	
	while (fgets(output, sizeof(output), videos) != NULL) {
		// videoX
		if (strlen(output) >= 6) {
			devnames.push_back(output);
			// remove newlines
			devnames.back().erase(std::remove(devnames.back().begin(), devnames.back().end(), '\n'), devnames.back().end());
			// BECAUSE THERE'S TWO VIDEO DEVICES PER CAMERA???
			break;
		}
	}
	
	pclose(videos);

	for (auto i = devnames.begin(); i < devnames.end(); ++i) {
		*i = "/dev/" + *i;
	}
	return devnames;
}

// These are the model numbers of our cameras
// They are matched with device names from sysfs
// Since cameraDevs[0] is always the vision camera, our camera that's most likely to be used for vision comes first

vector<string> cameraNames = {
	"920", "C525", "C615"
};

void Streamer::start() {
	
	vector<string> loopbackDevList = getVideoDeviceWithString("Dummy");
	if (loopbackDevList.empty()) {
		std::cerr << "v4l2loopback device not found" << std::endl;
		exit(1);
	}
	else {
		loopbackDev = loopbackDevList[0];
		std::cout << "video loopback device: " << loopbackDev << std::endl;
	}


	for (auto i = cameraNames.begin(); i < cameraNames.end(); ++i) {
		vector<string> namedCameras = getVideoDeviceWithString(*i);
		cameraDevs.insert(cameraDevs.end(), namedCameras.begin(), namedCameras.end());
	}
	
	std::cout << "Cameras detected: " << cameraDevs.size() << std::endl;
		
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
	else if (cameraDevs.size() == 2) {
		this->width = 640; this->height = 360;
	}
	else {
		this->width = 432; this->height = 240;
	}

	for (int i = 0; i < cameraDevs.size(); ++i) {
		cameraReaders.push_back(
			ThreadedVideoReader(width, height, cameraDevs[i].c_str(),std::bind(pushFrame,i))//Bind callback to relevant id.
		);
		readyState.push_back(false);
		std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give the cameras some time.
	}

	visionCamera = &cameraReaders[0];

	if (cameraDevs.size() > 1) outputWidth = width*2;
	else outputWidth = width;
	if (cameraDevs.size() <= 2) outputHeight = height;
	else outputHeight = height*2;

	correctedWidth = ceil(outputWidth/16.0)*16;
	correctedHeight = ceil(outputHeight/16.0)*16;
	frameBuffer.create(correctedHeight, correctedWidth, CV_8UC2); //Framebuffer matrix

	videoWriter.openWriter(correctedWidth, correctedHeight, loopbackDev.c_str());

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

			//vector<string> outputVideoDevs = cameraDevs;
			//outputVideoDevs[0] = loopbackDev;
			launchGStreamer(correctedWidth, correctedHeight, strAddr, atoi(bitrate), "5809", {loopbackDev});

			// It was planned to draw an overlay over the video feed in the driver station.
			// This sends the overlay data.
			cout << "Starting UDP stream..." << endl;
			if (computer_udp) delete computer_udp;
			computer_udp = new DataComm(strAddr, "5806");

			handlingLaunchRequest = false;
		}
	}).detach();
	
}

// get frame in the color space that the vision processing uses
cv::Mat Streamer::getBGRFrame() {
	cv::Mat frame;
	cvtColor(visionCamera->getMat(), frame, cv::COLOR_YUV2BGR_YUYV);
	return frame;
}

void Streamer::setLowExposure(bool value) {
	if (value != lowExposure) {
		lowExposure = value;
		if (lowExposure) {
			// probably could be lower, but it's probably sufficent
			visionCamera->setExposure(50);
		}
		else visionCamera->setAutoExposure();
	}
}

bool Streamer::checkFramebufferReadiness(){
	auto time = std::chrono::steady_clock().now();
	for(int i=0;i<cameraDevs.size();i++){
		if(!readyState[i]){
			if(time - cameraReaders[i].last_update < std::chrono::milliseconds(30)){
				readyState[i] = true; //Cache result of timeout to avoid unnesccesary syscalls.
			}else{
				return false; 
			}
		}
	}
	return true;
}
void Streamer::pushFrame(int i) {
	/* Updates framebuffer section for camera $i
	** If we are ready to go, write to the videowriter.
	*/
	//TODO: potential video tearing if the ThreadedVideoReader writes to the buffer at the same time getMat is called.
	frameLock.lock(); //We don't want this happening concurrently.
	readyState[i]=true;
	cv::Mat drawnOn;
	switch(i){
		case 0: //Vision camera
			drawnOn = visionCamera->getMat().clone(); // Draw an overlay on the frame before handing it off to gStreamer
			if (annotateFrame != nullptr) annotateFrame(drawnOn);
			drawnOn.copyTo(frameBuffer.colRange(0, width).rowRange(0, height));
			visionFrameNotifier(); //New vision frame
			break;
		case 1: //Second camera
			cameraReaders[1].getMat().copyTo(frameBuffer.colRange(width, outputWidth).rowRange(0, height));
			break;
		case 2: //Third camera
			cameraReaders[2].getMat().copyTo(frameBuffer.colRange(0, width).rowRange(height, outputHeight));
			break;
		default:
			perror("More than three camera output is unsupported at this time.");
			return;
	}
	if(checkFramebufferReadiness()){
		videoWriter.writeFrame(frameBuffer);
		for(int i=0;i<cameraDevs.size();i++){
			readyState[i]=false;
		}
	}
	frameLock.unlock();
}

void Streamer::run() {
	// defunct; doesn't do anything anymore
	while (true) sleep(INT_MAX);
}
