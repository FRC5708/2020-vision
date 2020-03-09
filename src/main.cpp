#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <string>
#include <chrono>
#include <thread>
#include <pthread.h>

#include <mutex>
#include <condition_variable>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

#include <signal.h>

#include "vision.hpp"
#include "streamer.hpp"

#include "DataComm.hpp"
#include "ControlPacketReceiver.hpp"

#include <dlfcn.h>


using std::cout; using std::cerr; using std::endl; using std::string;


// when false, drastically slows down vision processing
volatile bool visionEnabled = false;

VisionTarget lastResults;
//std::vector<cv::Point> lastResults;

std::chrono::steady_clock timing_clock;
auto currentFrameTime = timing_clock.now();

// c++ doesn't have real semaphores, so we must use the jank condition_variable to synchronize threads.
std::mutex waitMutex; 
std::condition_variable condition;
void visionFrameNotifier(); //Declared later
void annotateVisionPoints(cv::Mat&); //Declared later
Streamer streamer(visionFrameNotifier,annotateVisionPoints);

//Callback function passed into ControlPacketReceiver.
// recieves enable/disable signals from the RIO to conserve thermal capacity.
// Also sets exposure when actively driving to target.
// Also allows control packets to be sent to modify camera values. (See streamer.hpp:parseControlMessage() for more information)
string parseControlMessage(string message) {
	
	std::stringstream status=std::stringstream("");

	unsigned int indexOfDelimiter=message.find(':');
	if (indexOfDelimiter == string::npos && message[message.length() - 1] == '\n') {
		indexOfDelimiter = message.length() - 1;
	}
	std::string command=message.substr(0,indexOfDelimiter);
	if (command == "reset" || command == "resolution") {
		if(indexOfDelimiter >= message.length()){
			//There just isn't a : in there.
			return "UNPARSABLE MESSAGE (No colon-seperator)\n";
		}
		string arguments = message.substr(indexOfDelimiter+1,string::npos);
		if(arguments[arguments.length()-1]=='\n'){
			arguments=arguments.substr(0,arguments.length()-1); //Chop the newline off
		}
		return streamer.parseControlMessage(command, arguments);
	}
	else if (command == "visionEnable") visionEnabled = true;
	else if (command == "visionDisable") visionEnabled = false;
	else if (command == "lowExposureOn") streamer.setLowExposure(true);
	else if (command == "lowExposureOff") streamer.setLowExposure(false);
	else if (command.substr(0,3) == "HUD") {
		string piece;
		string state;
		std::stringstream toParse(command.substr(3,string::npos));
		toParse >> piece;
		toParse >> state;
		if(!toParse.good()) return "-1:PARSE FAILURE";
		// Set your variables here
		if (piece == "POV") {
			if (state == "front") streamer.setPOV(pov_state::front);
			else if (state == "back") streamer.setPOV(pov_state::rear);
			else return ("-1:Invalid POV state '" + state + "'");
		}
		else if (piece == "intake") {
			//if (state == "on")
			//if (state == "off")
			//else success = false
			return "-1: Command unimplemented.";
		}else return "-1:INVALID PIECE";

		return "0:SUCCESS";
		
	}
	else return "-1:Invalid command " + command + "\n";
	
	return "0:Success\n";
}

void VisionThread() {
	errno = 0;
	nice(5);
	if (errno != 0) perror("nice");

	DataComm rioComm=DataComm("10.57.8.2", "5808");

	auto lastFrameTime = currentFrameTime;
	while (true) {
		
		// If no new frame has come from the camera, wait.
		if (lastFrameTime == currentFrameTime) {
			std::unique_lock<std::mutex> uniqueWaitMutex(waitMutex);
			condition.wait(uniqueWaitMutex);
		}
		
		// currentFrameTime serves as a unique marker for this frame
		lastFrameTime = currentFrameTime;
		lastResults = doVision(streamer.getBGRFrame());
				
		if (lastResults.calcs.distance != 0) rioComm.sendData(lastResults.calcs, lastFrameTime);		
	}
}

void setDefaultCalibParams() {
	calib::width = 1280; calib::height = 720;
	
	// Old cameras' FOV is 69°, new camera is 78°
	//constexpr double radFOV = (69.0/180.0)*M_PI;
	constexpr double radFOV = (78.0/180.0)*M_PI;
	const double pixFocalLength = tan((M_PI_2) - radFOV/2) * sqrt(pow(calib::width, 2) + pow(calib::height, 2))/2; // pixels. Estimated from the camera's FOV spec.

	static double cameraMatrixVals[] {
		pixFocalLength, 0, ((double) calib::width)/2,
		0, pixFocalLength, ((double) calib::height)/2,
		0, 0, 1
	};
	calib::cameraMatrix = cv::Mat(3, 3, CV_64F, cameraMatrixVals);
	// distCoeffs is empty matrix
}
bool readCalibParams(const std::string path) {
	cv::FileStorage calibFile;
	calibFile.open(path.c_str(), cv::FileStorage::READ);
	if (!calibFile.isOpened()) {
		std::cerr << "Failed to open camera data " << path << endl;
		return false;
	}
	
	//setDefaultCalibParams();
	calib::cameraMatrix = calibFile["cameraMatrix"].mat();
	calib::distCoeffs = calibFile["dist_coeffs"].mat();
	cv::FileNode calibSize = calibFile["cameraResolution"];

	calib::width = calibSize[0];
	calib::height = calibSize[1];
	
	assert(calib::cameraMatrix.type() == CV_64F);
	
	// correcting for opencv bug?
	//calib::cameraMatrix.at<double>(0,0) *= 2;
	//calib::cameraMatrix.at<double>(1,1) *= 2;
	
	cout << "Loaded camera data: " << path << endl;
	return true;
}
// change camera calibration to match resolution of incoming image
void changeCalibResolution(int width, int height) {
	assert(calib::cameraMatrix.type() == CV_64F);
	if (fabs(calib::width / (double) calib::height - width / (double) height) > 0.03) {
		cerr << "wrong aspect ratio recieved from camera! Vision will be borked!" << endl;
	}
	calib::cameraMatrix.at<double>(0, 0) *= (width / (double) calib::width);
	calib::cameraMatrix.at<double>(0, 2) *= (width / (double) calib::width);
	calib::cameraMatrix.at<double>(1, 1) *= (height / (double) calib::height);
	calib::cameraMatrix.at<double>(1, 2) *= (height / (double) calib::height);

	calib::width = width; calib::height = height;
	
	cout << "Vision camera matrix set to: \n" << calib::cameraMatrix << endl;
}

// Test the vision system, feeding it a static image.
void doImageTesting(const char* path) {
	isImageTesting = true; verboseMode = true;
			
	cv::Mat image=cv::imread(path);
	cout << "image size: " << image.cols << 'x' << image.rows << endl;
	changeCalibResolution(image.cols, image.rows);

	try {
		doVision(image);
	}
	catch (std::exception& e) {
		std::cerr << "doVision threw " << e.what() << std::endl;
	}
	cout << "Testing Path: " << path << std::endl;
}
bool fileIsImage(char* file) {
	string path(file);
	string extension = path.substr(path.find_last_of(".") + 1);
	for (auto & c: extension) c = toupper(c);
	return extension == "PNG" || extension == "JPG" || extension == "JPEG";
}


/* bool isTargetLocked()
** TODO: IMPLEMENT ME!
** Returns true if shooting target is at an acceptable distance and offset angle
*/
bool isTargetLocked(){
	//TODO: Implement me, Sebastian!
	return true;
}

/* void annotateVisionPoints(cv::Mat& drawOn)
** Draws the currently-detected vision overlay points on top of the matrix passed in.
** Draws targeting reticle that is green when isTargetLocked() returns true.
** Also overlays a spinning-circle thingy to make it obvious that the stream is alive.
*/
void annotateVisionPoints(cv::Mat& drawOn) {

	//Draw vision points
	drawVisionPoints(lastResults.drawPoints, drawOn);


	//Draw targeting reticle
	{
		bool targetLock=isTargetLocked(); //Are we good 
		cv::rectangle(
			drawOn, //Target Matrix
			{drawOn.cols/5,drawOn.rows/5}, //Upper-left point
			{drawOn.cols-drawOn.cols/5,drawOn.rows-drawOn.rows/5}, //Lower-right point
			{0,(double) (targetLock ? 0: 128)}, //Green if targetLock, black otherwise.
			2 //Line thickness (pixels)
		);
	}

}
void visionFrameNotifier(){
// This function is called every new frame we get from the vision camera.
// It wakes up the vision processing thread if it is waiting on a new frame.
// if vision processing is disabled, it wakes up the thread only once every 2 seconds.

	auto time = timing_clock.now();
	if (visionEnabled || (time - currentFrameTime) > std::chrono::seconds(2)) {

		currentFrameTime = time;
		waitMutex.unlock();
		condition.notify_one();
	}
}
void chldHandler(int sig, siginfo_t *info, void *ucontext) {
	streamer.handleCrash(info->si_pid);
}
int main(int argc, char** argv) {
	// Enable or disable verbose output
	verboseMode = false;
	
	if (argc >= 3) {
		if (!readCalibParams(argv[1])) exit(1);
		doImageTesting(argv[2]);
		return 0;
	}
	else if (argc == 2) {
		if (fileIsImage(argv[1])) {
			setDefaultCalibParams();
			doImageTesting(argv[1]);
			return 0;
		}
		else {
			if (!readCalibParams(argv[1])) exit(1);
		}
	}
	else if (argc == 1) {
		// Load camera-specific params after we know which camera we're using
		setDefaultCalibParams();
	}
	else {
		cerr << "usage: " << argv[0] << "[test image] [calibration parameters]" << endl;
		return 1;
	}
	

	// Kill other instances of the program
	pid_t myPid = getpid();
	FILE* pidsStream = popen("pgrep 5708-vision", "r");
	char* pidString = nullptr;
	size_t lineAlloced = 0;
	bool killedPrevious = false;
	while (getline(&pidString, &lineAlloced, pidsStream) > 0) {
		
		pid_t otherPid = atoi(pidString);
		if (otherPid > 0 && otherPid != myPid){
			kill(otherPid, SIGTERM);
			std::cout << "Killed older instance of 5708-vision " << otherPid << std::endl;
			killedPrevious = true;
		} 
	}
	fclose(pidsStream);
	
	// Wait for the cameras to fully close
	if (killedPrevious) {
		sleep(2);
	}
	
	// SIGPIPE is sent to the program whenever a connection terminates. We want the program to stay alive if a connection unexpectedly terminates.
	signal(SIGPIPE, SIG_IGN);

	streamer.start();

	// SIGCHLD is recieved if gStreamer unexpectedly terminates.
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP | SA_SIGINFO;
	sa.sa_sigaction = &chldHandler;
	if (sigaction(SIGCHLD, &sa, 0) == -1) {//Note: we have a *lot* of different threads running now. What if something other than gstreamer crashes? - The handler checks the crashed pid to make sure it's gstreamer, and threads within the same process don't send SIGCHLD
		perror("sigaction");
		exit(1);
	}
	signal(SIGUSR1, [](int){
		 /* In order to save profiling information to gmon.out, the program *must* exit cleanly. 
		 ** Since our program will never do that, we are hooking the SIGUSR1 signal into this exit function.
		 */
		std::cout << "SIGUSR1 received. Killing..." << std::endl;
		void (*_mcleanup)(void);
    	_mcleanup = (void (*)(void))dlsym(RTLD_DEFAULT, "_mcleanup");
		if (_mcleanup == NULL) fprintf(stderr, "Unable to find gprof exit hook\n");
   		else {
			_mcleanup();
			std::cout << "Succesfully saved profiling data." << std::endl;
		}
		exit(0);
	});
	signal(SIGUSR2, [](int){
		/* For testing purposes, this makes the program SEGFAULT immedietly upon receiving SIGUSR2.
		** Obviously, don't send SIGUSR2 to the program without cause.
		*/
		std::cout << "SIGUSR2 received. Intentionally segfaulting..." << std::endl;
		
		//Volatile so the compiler doesn't realize this is a terrible idea.
		//Dereferencing a nullptr is a segfault.
		*((volatile int*)nullptr);
 
	});
	
	// will fail if the file doesn't exist, and use the default params instead
	readCalibParams("/home/pi/calib-data/" + streamer.visionCameraName + ".xml");
	// Scale the calibration parameters to match the current resolution
	changeCalibResolution(streamer.getVisionCameraWidth(), streamer.getVisionCameraHeight());

	ControlPacketReceiver receiver=ControlPacketReceiver(&parseControlMessage,5805);
    VisionThread();
	return 0;
}
