#include "streamer.hpp"
#include "DataComm.hpp"

#include <string>
#include <iostream>
#include <fcntl.h>
#include <chrono>
#include <thread>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>


using std::cout; using std::cerr; using std::endl; using std::string; using std::vector;

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

void Streamer::launchGStreamer(int width, int height, const char* recieveAddress, int bitrate, string port, string file) {
	cout << "launching GStreamer, targeting " << recieveAddress << endl;
	
	// Codec is specific to the raspberry pi's gpu
	string codec = "omxh264enc";
	string gstreamCommand = "gst-launch-1.0";
	
	std::stringstream command;
	command << gstreamCommand << " v4l2src device=" << file << " ! videoscale ! videoconvert ! queue ! " << codec << " target-bitrate=" << bitrate <<
	" control-rate=variable ! video/x-h264, width=" << width << ",height=" << height << ",framerate=30/1,profile=high ! rtph264pay ! udpsink"
	<< " host=" << recieveAddress << " port=" << port;

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
	for (unsigned int i = 0; i < cameraDevs.size(); ++i) {
		std::cout << "Camera " << i << ": " << cameraDevs[i] << std::endl;
	}

	std::vector<cv::Size2i> targetDims;
	
	if (cameraDevs.size() == 1) {
		targetDims = std::vector<cv::Size2i>(cameraDevs.size(), {800, 448});
	}
	else if (cameraDevs.size() == 2) {
		targetDims = std::vector<cv::Size2i>(cameraDevs.size(), {640, 360});
	}
	else {
		targetDims = std::vector<cv::Size2i>(cameraDevs.size(), {432, 240});
		targetDims[0] = {640, 360};
	}

	readyState.resize(cameraDevs.size());
	cameraFrameCounts.resize(cameraDevs.size());
	
	for (unsigned int i = 0; i < cameraDevs.size(); ++i) {
		cameraReaders.push_back(std::make_unique<ThreadedVideoReader>(
			targetDims[i].width, targetDims[i].height, cameraDevs[i].c_str(),std::bind(&Streamer::pushFrame,this,i))//Bind callback to relevant id.
		);
		if (i == 0) visionCamera = cameraReaders[0].get();
	}
	
	switch (cameraDevs.size()) {
	case 1:
		outputWidth = cameraReaders[0]->width; outputHeight = cameraReaders[0]->height;
		break;
	case 2:
		outputWidth = cameraReaders[0]->width + cameraReaders[1]->width;
		outputHeight = std::max(cameraReaders[0]->height, cameraReaders[1]->height);
		break;
	case 3:
		outputWidth = std::max(cameraReaders[0]->width + cameraReaders[1]->width, cameraReaders[2]->width);
		outputHeight = std::max(cameraReaders[0]->height, cameraReaders[1]->height) + cameraReaders[2]->height;
		break;
	case 4:
		outputWidth = std::max(cameraReaders[0]->width + cameraReaders[1]->width, cameraReaders[2]->width + cameraReaders[3]->width);
		outputHeight = std::max(cameraReaders[0]->height + cameraReaders[2]->height, cameraReaders[1]->height + cameraReaders[3]->height);
		break;
	default:
		std::cerr << "Over 4 cameras unsupported" << std::endl;
		exit(1);
	}
	
	// The h.264 encoder doesn't like dimensions that aren't multiples of 16, so our output must be sized this way.
	correctedWidth = ceil(outputWidth/16.0)*16;
	correctedHeight = ceil(outputHeight/16.0)*16;
	setupFramebuffer();
	
	videoWriter.openWriter(correctedWidth, correctedHeight, loopbackDev.c_str());

	initialized = true;	

	// Start the thread that listens for the signal from the driver station
	std::thread(&Streamer::dsListener, this).detach();
}

void Streamer::setupFramebuffer() {
	
	frameBuffer.create(correctedHeight, correctedWidth, CV_8UC2);
	frameBuffer.setTo(cv::Scalar{0, 128});
	
	cv::Mat source = cv::imread("/home/pi/vision-code/background.jpg");
	if (source.cols == 0 || source.rows == 0) return;
	constexpr int tileX = 5, tileY = 3;
	
	int tileWidth = outputWidth / tileX, tileHeight = outputHeight / tileY;
	
	cv::Mat badColorTile, badChromaResTile, tile;
	cv::resize(source, badColorTile, {tileWidth, tileHeight});
	assert(badColorTile.type() == CV_8UC3);
	cv::cvtColor(badColorTile, badChromaResTile, cv::COLOR_BGR2YUV, 2);
	tile.create(tileHeight, tileWidth, CV_8UC2);
	for (int x = 0; x < badChromaResTile.cols; x += 2) for (int y = 0; y < badChromaResTile.rows; ++y) {
		
		auto p1 = badChromaResTile.at<cv::Vec3b>(y,x);
		auto p2 = badChromaResTile.at<cv::Vec3b>(y,x+1);
		
		uint8_t avgU = (p1[1] + p2[1]) / 2;
		uint8_t avgV = (p1[2] + p2[2]) / 2;
		tile.at<cv::Vec2b>(y,x) = {p1[0], avgU};
		tile.at<cv::Vec2b>(y,x+1) = {p2[0], avgV};
	}
	
	assert(tile.type() == CV_8UC2);
	
	for (int x = 0; x < tileX; ++x) for (int y = 0; y < tileY; ++y) {
		tile.copyTo(frameBuffer(cv::Rect2i(x*tileWidth, y*tileHeight, tileWidth, tileHeight)));
	}
}

void Streamer::dsListener() {
	
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
		interceptStdio(clientFd, "Remote: ");

		//if (close(clientFd) == -1) perror("close");
		
		// wait for client's gstreamer to initialize
		sleep(2);

		char strAddr[INET6_ADDRSTRLEN];
		getnameinfo((struct sockaddr *) &clientAddr, sizeof(clientAddr), strAddr,sizeof(strAddr),
		0,0,NI_NUMERICHOST);

		//vector<string> outputVideoDevs = cameraDevs;
		//outputVideoDevs[0] = loopbackDev;
		launchGStreamer(correctedWidth, correctedHeight, strAddr, atoi(bitrate), "5809", loopbackDev);

		// It was planned to draw an overlay over the video feed in the driver station.
		// This sends the overlay data.
		cout << "Starting UDP stream..." << endl;
		if (computer_udp) delete computer_udp;
		computer_udp = new DataComm(strAddr, "5806");

		handlingLaunchRequest = false;
	}
}

// Forwards everything in fromFd to toFd, with prefix before every line
void interceptFile(int fromFd, int toFd, string prefix) {
	std::thread([=]() {

		// The device that fromFd was previously written to
		int oldOut = dup(fromFd);
		
		int pipefds[2];
		pipe(pipefds);

		// move the write end of the pipe 
		dup2(pipefds[1], fromFd);
		close(pipefds[1]);

		FILE* fromOutput = fdopen(pipefds[0], "r");
		if (fromOutput == nullptr) {
			perror("fdopen");
			return;
		}
		char* line = nullptr;
		size_t bufsize = 0;
		while (true) {
			ssize_t len = getline(&line, &bufsize, fromOutput);
			if (len == -1) continue;
			write(oldOut, line, len);

			if (write(toFd, (prefix + std::string(line, len)).c_str(), len + prefix.size()) < 0 &&  
				(errno == EBADF || errno == EPIPE)) {

				// toFd was closed. Clean up...
				dup2(oldOut, fromFd);
				close(oldOut);
				fclose(fromOutput);

				cout << "exiting interceptFile" << endl;
				return;
			}
		}
	}).detach();
}
void interceptStdio(int toFd, string prefix) {
	interceptFile(STDOUT_FILENO, toFd, prefix);
	interceptFile(STDERR_FILENO, toFd, prefix);
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


	double bestFrameTime = INFINITY;
	double frameTimes[cameraDevs.size()];

	for(unsigned int i=0;i<cameraDevs.size();i++) { 
		frameTimes[i] = cameraReaders[i]->getMeanFrameInterval();
		if (frameTimes[i] < bestFrameTime) {
			bestFrameTime = frameTimes[i];
		}
	}
	
	// These are the cameras we care if are ready or not.
	vector<bool> synchroCameras(cameraDevs.size(), false);
	for(unsigned int i=0;i<cameraDevs.size();i++) {
		synchroCameras[i] = 
		frameTimes[i] / bestFrameTime > 0.85 // If the camera is fast
		 // and it's not dead
		 && time - cameraReaders[i]->last_update < std::chrono::duration<double>(1.5*std::min(frameTimes[i], 0.07));
		 
	}
	// If there are no synchro cameras (unlikely, but possible if framerates are changing) disregard deadness
	bool hasSynchro = false;
	for(unsigned int i=0;i<cameraDevs.size();i++) hasSynchro |= synchroCameras[i];
	if (!hasSynchro) for(unsigned int i=0;i<cameraDevs.size();i++) {
		synchroCameras[i] = frameTimes[i] / bestFrameTime > 0.85;
	}
	
	for(unsigned int i=0;i<cameraDevs.size();i++) {
		// If we care about the camera, but it's not ready.
		if (synchroCameras[i] && !readyState[i]) return false;
	}
	return true;
}
void Streamer::pushFrame(int i) {
	if(!initialized) {
		cout << "recieved frame from " << i << " but not initialized yet (this theoretically shouldn't happen)" << endl;
		return;
	}; //We're still setting up.
	//cout << "Logging: received frame from " << i << endl;
	/* Updates framebuffer section for camera $i
	** If we are ready to go, write to the videowriter.
	*/
	frameLock.lock(); //We don't want this happening concurrently.
	readyState[i]=true;
	++cameraFrameCounts[i];
	try {
		switch(i){
			case 0: { //Vision camera
				
				cv::Mat visionFrame = frameBuffer.colRange(0, visionCamera->width).rowRange(0, visionCamera->height);
				visionCamera->getMat().copyTo(visionFrame);

				// Draw an overlay on the frame before handing it off to gStreamer
				if (annotateFrame != nullptr) annotateFrame(visionFrame);

				visionFrameNotifier(); //New vision frame
				break;
			}
			case 1: //Second camera
				cameraReaders[1]->getMat().copyTo(frameBuffer
				.colRange(outputWidth - cameraReaders[1]->width, outputWidth)
				.rowRange(0, cameraReaders[1]->height));
				break;
			case 2: //Third camera
				cameraReaders[2]->getMat().copyTo(frameBuffer
				.colRange(0, cameraReaders[2]->width)
				.rowRange(outputHeight - cameraReaders[2]->height, outputHeight));
				break;
			case 3: //Fourth camera (untested)
				cameraReaders[3]->getMat().copyTo(frameBuffer
				.colRange(outputWidth - cameraReaders[3]->width, outputWidth)
				.rowRange(outputHeight - cameraReaders[3]->height, outputHeight));
				break;
			default:
				cerr << "More than four cameras are unsupported at this time." << endl;
				return;
		}
	} catch (VideoReader::NotInitializedException& e) {
		frameLock.unlock();
		return;
	}

	if(checkFramebufferReadiness()){
		videoWriter.writeFrame(frameBuffer);
		
		auto now = std::chrono::steady_clock().now();
		auto elapsed = now - lastReport;
		if (elapsed >= std::chrono::seconds(1)) {
			cout << "In the past " << std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count()
			<< " seconds, " << frameCount << " pushed frames: ";
			for (int i = 0; i < cameraFrameCounts.size(); ++i) {
				cout << cameraFrameCounts[i] << " from cam " << i;
				if (i != cameraFrameCounts.size() - 1) cout << ", ";
				cameraFrameCounts[i] = 0;
			}
			cout << endl;
			frameCount = 0;
			lastReport = now;
		}
		++frameCount;

		for(unsigned int i=0;i<cameraDevs.size();i++){
			readyState[i]=false;
		}
	}
	frameLock.unlock();
}