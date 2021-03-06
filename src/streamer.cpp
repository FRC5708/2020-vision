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


// These are the model numbers of our cameras
// They are matched with device names from sysfs
// Since cameraDevs[0] is always the vision camera, our camera that's most likely to be used for vision comes first
vector<string> cameraNames = {
	"C920_99EDB55F", "C615_603161B0", "C525_5FC6DE20", "C615_F961A370"
};
vector<bool> flipCameras = {
	true, false, false, true
};

// --------- Initialization stuff -----------------
Streamer::Streamer(std::function<void(void)> callback)
: visionFrameNotifier(callback) {
	gstreamer_pid=get_previous_gstreamer_pid();
}
void Streamer::start() {
	setupCameras();
	calculateOutputSize();
	setupFramebuffer();
	videoWriter.openWriter(outputWidth, outputHeight, loopbackDev.c_str());
	initialized = true;	
	// Start the thread that listens for the signal from the driver station
	std::thread(&Streamer::dsListener, this).detach();
}

// --------------- Camera stuff -------------------

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

void Streamer::setupCameras(){
	if(initialized) return;

	vector<string> loopbackDevList = getLoopbackDevices();
	if (loopbackDevList.empty()) {
		std::cerr << "v4l2loopback device not found" << std::endl;
		exit(1);
	}
	else {
		loopbackDev = loopbackDevList[0];
		std::cout << "video loopback device: " << loopbackDev << std::endl;
	}


	for (auto& i : cameraNames) {
		vector<string> namedCameras = getVideoDevicesWithString(i);
		cameraDevs.insert(cameraDevs.end(), namedCameras.begin(), namedCameras.end());
		if (visionCameraName.empty() && namedCameras.size() > 0) visionCameraName = i;
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

	newFrames.resize(cameraDevs.size());
	cameraFrameCounts.resize(cameraDevs.size());
	
	for (unsigned int i = 0; i < cameraDevs.size(); ++i) {
		cameraReaders.push_back(std::make_unique<ThreadedVideoReader>(
			targetDims[i].width, targetDims[i].height, cameraDevs[i].c_str(),std::bind(&Streamer::pushFrame,this,i), 
			flipCameras[i])//Bind callback to relevant id.
		);
		if (i == 0) visionCamera = cameraReaders[0].get();
	}
}

void Streamer::calculateOutputSize(){
	switch (cameraDevs.size()) {
	case 1:
		uncorrectedWidth = cameraReaders[0]->getWidth(); uncorrectedHeight = cameraReaders[0]->getHeight();
		break;
	case 2:
		uncorrectedWidth = cameraReaders[0]->getWidth() + cameraReaders[1]->getWidth();
		uncorrectedHeight = std::max(cameraReaders[0]->getHeight(), cameraReaders[1]->getHeight());
		break;
	case 3:
		uncorrectedWidth = std::max(cameraReaders[0]->getWidth() + cameraReaders[1]->getWidth(), cameraReaders[2]->getWidth());
		uncorrectedHeight = std::max(cameraReaders[0]->getHeight(), cameraReaders[1]->getHeight()) + cameraReaders[2]->getHeight();
		break;
	case 4:
		uncorrectedWidth = std::max(cameraReaders[0]->getWidth() + cameraReaders[1]->getWidth(), cameraReaders[2]->getWidth() + cameraReaders[3]->getWidth());
		uncorrectedHeight = std::max(cameraReaders[0]->getHeight() + cameraReaders[2]->getHeight(), cameraReaders[1]->getHeight() + cameraReaders[3]->getHeight());
		break;
	default:
		std::cerr << "Over 4 cameras unsupported" << std::endl;
		exit(1);
	}
	
	// The h.264 encoder doesn't like dimensions that aren't multiples of 16, so our output must be sized this way.
	outputWidth = ceil(uncorrectedWidth/16.0)*16;
	outputHeight = ceil(uncorrectedHeight/16.0)*16;
}

void colorConvertBGR2YUYV(cv::Mat& in, cv::Mat& out) {
	// For some reason, opencv can't convert directly to YUYV (aka YUV 4:2:2 or YUY2), so we must convert to YUV (aka YUV 4:4:4) then downsample that.
	assert(in.type() == CV_8UC3);
	cv::Mat badChromaResTile;
	cv::cvtColor(in, badChromaResTile, cv::COLOR_BGR2YUV, 2);
	assert(badChromaResTile.type() == CV_8UC3);
	out.create(in.rows, in.cols, CV_8UC2);
	for (int x = 0; x < badChromaResTile.cols; x += 2) for (int y = 0; y < badChromaResTile.rows; ++y) {
		
		auto p1 = badChromaResTile.at<cv::Vec3b>(y,x);
		auto p2 = badChromaResTile.at<cv::Vec3b>(y,x+1);
		
		uint8_t avgU = (p1[1] + p2[1]) / 2;
		uint8_t avgV = (p1[2] + p2[2]) / 2;
		out.at<cv::Vec2b>(y,x) = {p1[0], avgU};
		out.at<cv::Vec2b>(y,x+1) = {p2[0], avgV};
	}
	assert(out.type() == CV_8UC2); //Something is horrifically screwed up.
}

void Streamer::setupFramebuffer() {
	
	frameBuffer.create(outputHeight, outputWidth, CV_8UC2);
	frameBuffer.setTo(cv::Scalar{0, 128});
	
	cv::Mat source = cv::imread("/home/pi/vision-code/background.jpg");
	if (source.cols == 0 || source.rows == 0){
		std::cerr << "Background image read failed. (Either corrupted or non-existent file)" << std::endl;
		return;
	}
	constexpr int tileX = 5, tileY = 3;
	
	int tileWidth = uncorrectedWidth / tileX, tileHeight = uncorrectedHeight / tileY;
	
	cv::Mat badColorTile, tile;
	cv::resize(source, badColorTile, {tileWidth, tileHeight});
	if(badColorTile.type() != CV_8UC3){
		std::cerr << "Bad image type for background." << std::endl;
		return;
	}
	colorConvertBGR2YUYV(badColorTile, tile);
	
	for (int x = 0; x < tileX; ++x) for (int y = 0; y < tileY; ++y) {
		tile.copyTo(frameBuffer(cv::Rect2i(x*tileWidth, y*tileHeight, tileWidth, tileHeight)));
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
		bestFrameTime / frameTimes[i] > 0.85 // If the camera is fast
		 // and it's not dead
		 && time - cameraReaders[i]->getLastUpdate() < std::chrono::duration<double>(1.5*std::min(frameTimes[i], 0.07));
		 
	}
	// If there are no synchro cameras (unlikely, but possible if framerates are changing) disregard deadness
	bool hasSynchro = false;
	for(unsigned int i=0;i<cameraDevs.size();i++) hasSynchro |= synchroCameras[i];
	if (!hasSynchro) for(unsigned int i=0;i<cameraDevs.size();i++) {
		synchroCameras[i] = bestFrameTime / frameTimes[i] > 0.85;
	}
	
	for(unsigned int i=0;i<cameraDevs.size();i++) {
		// If we care about the camera, but it's not ready.
		if (synchroCameras[i] && !newFrames[i]) return false;
	}
	return true;
}
void Streamer::pushFrame(int i) {
	if(!initialized) {
		std::cerr << "recieved frame from " << i << " but not initialized yet (this theoretically shouldn't happen)" << endl;
		return;
	}; //We're still setting up.
	//cout << "Logging: received frame from " << i << endl;
	/* Updates framebuffer section for camera $i
	** If we are ready to go, write to the videowriter.
	*/
	frameLock.lock(); //We don't want this happening concurrently.
	newFrames[i]=true;
	++cameraFrameCounts[i];
	try {
		switch(i){
			case 0: { //Vision camera
				
				cv::Mat visionFrame = frameBuffer.colRange(0, visionCamera->getWidth()).rowRange(0, visionCamera->getHeight());
				visionCamera->getMat().copyTo(visionFrame);

				// Draw an overlay on the frame before handing it off to gStreamer
				if (annotateFrame != nullptr) annotateFrame(visionFrame);

				visionFrameNotifier(); //New vision frame
				break;
			}
			case 1: //Second camera
				cameraReaders[1]->getMat().copyTo(frameBuffer
				.colRange(uncorrectedWidth - cameraReaders[1]->getWidth(), uncorrectedWidth)
				.rowRange(0, cameraReaders[1]->getHeight()));
				break;
			case 2: //Third camera
				cameraReaders[2]->getMat().copyTo(frameBuffer
				.colRange(0, cameraReaders[2]->getWidth())
				.rowRange(uncorrectedHeight - cameraReaders[2]->getHeight(), uncorrectedHeight));
				break;
			case 3: //Fourth camera (untested)
				cameraReaders[3]->getMat().copyTo(frameBuffer
				.colRange(uncorrectedWidth - cameraReaders[3]->getWidth(), uncorrectedWidth)
				.rowRange(uncorrectedHeight - cameraReaders[3]->getHeight(), uncorrectedHeight));
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
			for (unsigned int i = 0; i < cameraFrameCounts.size(); ++i) {
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
			newFrames[i]=false;
		}
	}
	frameLock.unlock();
}

void Streamer::restartWriter(){
	videoWriter.closeWriter();
	std::cout << "Closed Writer. Reopening..." << std::endl;
	videoWriter.openWriter(outputWidth, outputHeight, loopbackDev.c_str());
}

// ---------------- GStreamer stuff -----------------------

void Streamer::handleCrash(pid_t pid) {
	if(!handlingLaunchRequest && pid==gstreamer_pid){
		// Theoretically using cout in a signal handler is bad, but it's never caused an issue for us
        std::cout << "Realunching gStreamer after crash..." << std::endl;
		gstreamer_pid = runCommandAsync(gstreamer_command);
	}
}


void Streamer::launchGStreamer(int width, int height, const char* recieveAddress, int bitrate, string port, string file) {
	cout << "launching GStreamer, targeting " << recieveAddress << endl;
	
	// Codec is specific to the raspberry pi's gpu
	string codec = "omxh264enc";
	string gstreamCommand = "gst-launch-1.0";
	
	std::stringstream command;
	command << gstreamCommand << " v4l2src device=" << file << " ! videoscale ! videoconvert ! queue ! " << codec << " target-bitrate=" << bitrate <<
	" control-rate=variable ! video/x-h264, width=" << width << ",height=" << height << ",framerate=30/1,profile=high ! rtph264pay ! " 
	// Normal streaming
	 " udpsink"
	// Tunneled streaming
	// " gdppay ! tcpclientsink"
	<< " host=" << recieveAddress << " port=" << port;

	string strCommand = command.str();
	
	pid_t pid = runCommandAsync(strCommand);

	gstreamer_pid=pid;
	bool retval=write_gstreamer_pid_to_file();
	if(retval==false) std::cerr << "write_gstreamer_pid_to_file() failed..." << std::endl;
	gstreamer_command=strCommand;

}

void Streamer::dsListener() {
	//Threaded listener
	servFd = socket(AF_INET6, SOCK_STREAM, 0);
	if (servFd < 0) {
		perror("socket");
		return;
	}
	fcntl(servFd, F_SETFD, fcntl(servFd, F_GETFD) | FD_CLOEXEC);
	
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
			perror("accept (gstreamer)");
			continue;
		}

		// At this point, a connection has been recieved from the driver station.
		// gStreamer will now be set up to stream to the driver station.
		handlingLaunchRequest = true;
		killGstreamerInstance();
 
		char bitrateStr[16];
		ssize_t len = read(clientFd, bitrateStr, sizeof(bitrateStr)-1);
		bitrateStr[len] = '\0';
		int bitrate = atoi(bitrateStr);
		if (bitrate <= 0) {
			std::cerr << "Invalid bitrate, setting to 1,000,000";
			bitrate = 1000000;
		}
		this->bitrate = bitrate;
		
		const char message[] = "Launching remote GStreamer...\n";
		if (write(clientFd, message, sizeof(message)) == -1) {
			perror("write");
		}
		interceptStdio(clientFd, "Remote: ");
		
		// wait for client's gstreamer to initialize
		sleep(1);

		char strAddr[INET6_ADDRSTRLEN];
		this->strAddr=strAddr;
		getnameinfo((struct sockaddr *) &clientAddr, sizeof(clientAddr), strAddr,sizeof(strAddr),
		0,0,NI_NUMERICHOST);

		launchGStreamer(outputWidth, outputHeight, strAddr, bitrate, "5809", loopbackDev);
		handlingLaunchRequest = false;
	}
}

void Streamer::killGstreamerInstance(){
	cout << "killing previous instance ..." << endl;
	if(gstreamer_pid!=0){//Make sure we actually have a previous instance ...
		if (kill(gstreamer_pid, SIGTERM) == -1) {
			perror("kill");
		}
		waitpid(gstreamer_pid, nullptr, 0);
		gstreamer_pid=0; //We currently have nothing going.
	}
}

/* pid_t Streamer::get_previous_gstreamer_pid()
** This function reads the pid of the previous gstreamer instance from the file /tmp/5708-vision/gstreamer_pid/
** If the file doesn't exist, this function will return 0.
*/
pid_t Streamer::get_previous_gstreamer_pid(){

	int file_fd=-1; //file descriptor of file containing pid
	pid_t gst_pid=0; //Value that will be returned. Remains 0 if unable to succesfully read file.
	ssize_t count=-1; //Count read by read call.
	int close_val=-1; //Return of close attempt.
	int temp_fd=-1; //File descriptor temporarily used to create, and then close the file if it didn't exist.
	
	//Open the file, and create it if it doesn't exist
	file_fd=open(GSTREAMER_PREVIOUS_PID_FILE_PATH, O_RDONLY | O_CLOEXEC);
	if(file_fd==-1){
		if(errno==ENOENT){
			//The file doesn't exist. Let's make it.
			std::cout << "File "<< GSTREAMER_PREVIOUS_PID_FILE_PATH << " doesn't exist. Creating..." << std::endl;
			temp_fd=creat(GSTREAMER_PREVIOUS_PID_FILE_PATH,S_IRUSR | S_IWUSR);
			if(temp_fd == -1){
				perror("pid_t Streamer::get_previous_gstreamer_pid() -- creat");
			}
			close(temp_fd);
			goto CLEANUP;
		}
		else{
			//Who knows what happened. ~(-_-)~
			perror("pid_t Streamer::get_previous_gstreamer_pid() -- open");
			goto CLEANUP;
		}
	}

	//Read from the file.
	count = read(file_fd,&gst_pid,sizeof(pid_t));
	if(count == -1){
		perror("pid_t Streamer::get_previous_gstreamer_pid() -- read");
		gst_pid=0;
		goto CLEANUP;
	}
	if(count != sizeof(pid_t)){
		//Something got screwy.
		std::cerr << "File for gstreamer_pid existed, but had wrong bit count. Something screwy has occured." << std::endl;
		gst_pid=0;
		goto CLEANUP;
	}
	//Cleanup
CLEANUP:
	close_val=close(file_fd);
	if(close_val==-1) perror("pid_t Streamer::get_previous_gstreamer_pid() -- close");
	if(gst_pid!=0) std::cout << "Succesfully read previous gst_pid of " << gst_pid << " from file." << std::endl;
	return gst_pid;

};
/* bool Streamer::write_gstreamer_pid_to_file()
** Attempts to write the pid of the currently-running gstreamer instance to disk.
** Upon failure (either gstreamer_pid is 0, or file I/O fails), the function returns false.
*/
bool Streamer::write_gstreamer_pid_to_file(){
	bool failed = false;
	int retval;

	int write_fd = open(GSTREAMER_PREVIOUS_PID_FILE_PATH, O_TRUNC | O_WRONLY | O_CLOEXEC);
	if(write_fd==-1){
		perror("Streamer::write_gstreamer_pid_to_file() -- open");
		failed=true;
		goto CLEANUP;
	}

	retval=write(write_fd,&gstreamer_pid,sizeof(gstreamer_pid));
	if(retval==-1){
		perror("Streamer::write_gstreamer_pid_to_file() -- write");
		failed=true;
		goto CLEANUP;
	}
CLEANUP:
	close(write_fd);
	if(!failed) std::cout << "Wrote gstreamer pid of " << gstreamer_pid << " to file." << std::endl;
	return !failed;
}

// ------------ Control message stuff ------------------

string Streamer::parseControlMessage(string command, string arguments){

	std::stringstream status=std::stringstream("");

	std::string camera_string;
	std::string parameters;

	size_t delimiter_index = arguments.find(':');
	camera_string=arguments.substr(0,delimiter_index);
	if(delimiter_index==string::npos || delimiter_index==arguments.length()-1){
		//Either the delimiter doesn't exist, or it's at the end of the string with nothing after it.
		parameters="";
	}else{
		parameters=arguments.substr(delimiter_index+1,string::npos);
		//(Yes, this entire if statement is technically redundant, as trying to read from string::npos as a start will return an empty substr.)
		//However, that requires knowing specifics of string implementation that would be confusing to have to decypher. 
	}
	std::stringstream cameraSegment=std::stringstream(camera_string);
	std::string buffer;
	std::vector<unsigned int> cameras;
	while(getline(cameraSegment,buffer,',')){
		unsigned int cam_no;
		try{
			cam_no=std::stoi(buffer);
			cameras.push_back(cam_no);
		}catch(std::exception& e){ // stoi threw an exception
			status << buffer <<  ":-1:INVALID CAMERA NO" << '\n';
		}
	} 
	if(cameras.size()==0){
		//We didn't actually get any camera numbers.
		return "UNPARSABLE MESSAGE (No cameras specified)\n";
	}
	for(unsigned int i : cameras){
		if(i>=cameraReaders.size()){
			status << i << ":-1:INVALID CAMERA NO" << "\n";
			continue;
		}
		string return_status=controlMessage(i,command, parameters);
		status << i << ":" << return_status << "\n";
	}
	return status.str(); //Delightful.

}
string Streamer::controlMessage(unsigned int cam_no, string command, string parameters){
	
	std::stringstream status=std::stringstream("");
	ThreadedVideoReader* camera = cameraReaders.at(cam_no).get();

	//Resolution command
	if(command=="resolution"){
		frameLock.lock(); //Spooky bad times here.

		std::stringstream toParse=std::stringstream(parameters);
		unsigned int width,height;
		toParse >> width;
		toParse >> height;
		if(toParse.fail()){
			frameLock.unlock(); //Goddarn me.
			return "-1:INVALID RESOLUTION (Not unsigned int)";
		}
		int retval = camera->setResolution(width,height);
		status << retval << ":" << ((retval==0) ? "SUCCESS" : "FAILURE");
		if(retval==0){
			handlingLaunchRequest=true;
			bool relaunchingGstreamer = gstreamer_pid != 0;
			if (relaunchingGstreamer) {
				std::cout << "Killing previous gstreamer instance..." << std::endl;
				killGstreamerInstance();
			}
			std::cout << "Calculating modified output width..." << std::endl;
			calculateOutputSize();
			std::cout << "Setting up framebuffer..." << std::endl;
			setupFramebuffer();
			std::cout << "Restarting Video Writer..." << std::endl;
			restartWriter(); //Work Please
			if (relaunchingGstreamer) {
				std::cout << "Restarting new gstreamer stream..." << std::endl;
				launchGStreamer(outputWidth, outputHeight, strAddr.c_str(), bitrate, "5809", loopbackDev);
			}		
			handlingLaunchRequest=false;
		}
		frameLock.unlock();
	}else if(command == "reset"){
		std::cout << "Attempting to reset " << cam_no << "(COMMAND given)" << std::endl;
		camera->reset(true);
		return "0:RESET";
	}
	else{
		status << "-1:Unrecognized command \"" << command << "\"\n";
	}
	return status.str();
}

// ----------- Random helper functions -------------

// Runs a command. Like system(), but it doesn't wait for the process to exit.
pid_t runCommandAsync(const std::string& cmd) {
	pid_t pid = fork();
	
	if (pid == 0) {
		// child

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

// The next three functions are for getting video devices based on various parameters.
vector<string> getOutputsFromCommand(const char * cmd) {

	FILE* cmdStream = popen(cmd, "r");

	vector<string> names;
	char output[1035];
	
	while (fgets(output, sizeof(output), cmdStream) != NULL) {
		// videoX
		if (strlen(output) >= 6) {
			names.push_back(output);
			// remove newlines
			names.back().erase(std::remove(names.back().begin(), names.back().end(), '\n'), names.back().end());
		}
	}
	pclose(cmdStream);
	return names;
}
// Finds a video device whose name contains cmp
vector<string> getVideoDevicesWithString(string cmp) {

	return getOutputsFromCommand(("for I in /dev/v4l/by-id/*; do if basename $I | grep -Fq '" 
	+ cmp + "' && basename $I | grep -Fq index0; then realpath $I; fi; done").c_str());
}
vector<string> getLoopbackDevices() {
	vector<string> devnames = getOutputsFromCommand("for I in /sys/class/video4linux/*; do if grep -q Dummy $I/name; then basename $I; fi; done");
	
	for (auto i = devnames.begin(); i < devnames.end(); ++i) {
		*i = "/dev/" + *i;
	}
	return devnames;
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