#include "VideoHandler.hpp"

#include <iostream>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread> // I hate everything.

/*
Magic and jankyness lies here. This class communicates to the cameras and to gStreamer with the Video4Linux API.
 The API is poorly documented. You'll notice various links to some blogposts
 which were helpful but did a few things that were broken in one way or another. This class was 
 created with trial, error, and pain.
 Different drivers implement the API in subtly different ways, so cameras that we 
 haven't tested (especially non-usb cameras) might not work.
*/

VideoReader::VideoReader(int width, int height, const char* file) : deviceFile(std::string(file)){
	this->width = width; this->height = height;
}
bool VideoReader::tryOpenReader() {

	// http://jwhsmith.net/2014/12/capturing-a-webcam-stream-using-v4l2/
	// https://jayrambhia.com/blog/capture-v4l2

	while ((camfd = open(deviceFile.c_str(), O_RDWR|O_CLOEXEC)) < 0) {
		perror("open");
		if (errno == EBUSY) {
			sleep(1);
		}
		else return false;
	}
	//TODO:MOVEME!
	queryResolutions();
	struct v4l2_capability cap;
	if(ioctl(camfd, VIDIOC_QUERYCAP, &cap) < 0){
		perror("VIDIOC_QUERYCAP");
		return false;
	}
	if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)){
		fprintf(stderr, "The device does not handle single-planar video capture.\n");
		return false;
	}

	struct v4l2_format format;
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	format.fmt.pix.width = width;
	format.fmt.pix.height = height;
	format.fmt.pix.field = V4L2_FIELD_INTERLACED;

	while (ioctl(camfd, VIDIOC_S_FMT, &format) < 0){
		perror((deviceFile + " VIDIOC_S_FMT").c_str());
		if (errno == EBUSY) {
			sleep(1);
			continue;
		}
		else {
			return false;
		}
	}

	// set framerate
	struct v4l2_streamparm streamparm;
	memset(&streamparm, 0, sizeof(streamparm));
	streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(camfd, VIDIOC_G_PARM, &streamparm) != 0){
		perror("Setting framerate: VIDIOC_G_PARM");
	}
	else {
		std::cout << "Attempting to maximize framerate by setting frame time to 1/120" << std::endl;
		streamparm.parm.capture.capturemode |= V4L2_CAP_TIMEPERFRAME;
		streamparm.parm.capture.timeperframe.numerator = 1;
		streamparm.parm.capture.timeperframe.denominator = 1000;
		if(ioctl(camfd, VIDIOC_S_PARM, &streamparm) !=0) {
			perror("Setting framerate: VIDIOC_S_PARM");
		}
		else std::cout << "Frame time is: " << streamparm.parm.capture.timeperframe.numerator 
		<< "/" << streamparm.parm.capture.timeperframe.denominator << std::endl;
	}

	// request memory buffers from the kernel
	bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufrequest.memory = V4L2_MEMORY_MMAP;
	bufrequest.count = 4;

	if(ioctl(camfd, VIDIOC_REQBUFS, &bufrequest) < 0){
		perror("VIDIOC_REQBUFS");
		return false;
	}
	memset(&bufferinfo, 0, sizeof(bufferinfo));

	std::cout << "buffer count: " << bufrequest.count << std::endl;
	buffers.resize(bufrequest.count);
	

   for (unsigned int i = 0; i < bufrequest.count; ++i) {
		bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		bufferinfo.memory = V4L2_MEMORY_MMAP;
		bufferinfo.index = i;
		
		if(ioctl(camfd, VIDIOC_QUERYBUF, &bufferinfo) < 0){
			perror("VIDIOC_QUERYBUF");
			return false;
		}

		buffers[i] = mmap(
			NULL,
			bufferinfo.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			camfd,
			bufferinfo.m.offset
		);
		if(buffers[i] == MAP_FAILED){
			perror("mmap");
			return false;
		}
		memset(buffers[i], 0, bufferinfo.length);
	}
	
	int type = bufferinfo.type;
	if(ioctl(camfd, VIDIOC_STREAMON, &type) < 0){
		perror("VIDIOC_STREAMON");
		return false;
	}

	for (unsigned int i = 0; i < bufrequest.count; ++i) {
		memset(&bufferinfo, 0, sizeof(bufferinfo));
		bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		bufferinfo.memory = V4L2_MEMORY_MMAP;
		bufferinfo.index = i;

		if(ioctl(camfd, VIDIOC_QBUF, &bufferinfo) < 0){
			std::cerr << "Queueing buffer " << i << ": ";
			perror("VIDIOC_QBUF");
		}
	}
	return true;
}
void VideoReader::openReader() {
	while (!tryOpenReader()) {
		std::cerr << "Failed to open " << deviceFile << "! Retrying in 3 seconds..." << std::endl;
		closeReader();
		sleep(3);
	}
}

void VideoReader::closeReader() {
	int type = bufferinfo.type;
	if (ioctl(camfd, VIDIOC_STREAMOFF, &type) < 0) perror("VIDEOC_STREAMOFF"); //Send the off ioctl.

	// unmap the frame buffers
	for (unsigned int i = 0; i < bufrequest.count; ++i) {

		bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		bufferinfo.memory = V4L2_MEMORY_MMAP;
		bufferinfo.index = i;
		
		if(ioctl(camfd, VIDIOC_QUERYBUF, &bufferinfo) < 0){
			perror("Unable to deallocate buffer. VIDIOC_QUERYBUF");
			continue;
		}
		munmap(buffers[i], bufferinfo.length);
	}
	// Deallocate the buffers from the driver 
	bufrequest.count = 0;
	if(ioctl(camfd, VIDIOC_REQBUFS, &bufrequest) < 0){
		perror("Deallocating buffers: VIDIOC_REQBUFS");
	}

	if (close(camfd) < 0) perror("close"); //Close the camera fd.

	hasFirstFrame = false;
}

VideoReader::~VideoReader(){
	//Destructor
	closeReader();
}

bool VideoReader::grabFrame() {
	
	memset(&bufferinfo, 0, sizeof(bufferinfo));
	bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufferinfo.memory = V4L2_MEMORY_MMAP;
	// The buffer's waiting in the outgoing queue.
	int ret = ioctl(camfd, VIDIOC_DQBUF, &bufferinfo);
	if(ret < 0) {
		perror("VIDIOC_DQBUF");
		 return false;
	}

	currentBuffer = buffers[bufferinfo.index];
	//std::cout << "buffer index: " << bufferinfo.index << " addr: " << currentBuffer << std::endl;
	assert((signed) bufferinfo.length == width*height*2);

	// put the old buffer back into the queue
	if(hasFirstFrame && ioctl(camfd, VIDIOC_QBUF, &bufferinfo) < 0){
		perror("VIDIOC_QBUF");
		return false;
	}

	hasFirstFrame = true;
	return true;
}
/* Get and cache the list of acceptable resolution pair values for the used format. */
void VideoReader::queryResolutions(){
	if(hasResolutions) return;
	hasResolutions=true;
	v4l2_frmsizeenum capability;
	capability.index=1;
	capability.pixel_format=V4L2_PIX_FMT_YUYV;
	while(ioctl(camfd,VIDIOC_ENUM_FRAMESIZES,&capability)==0){
		if(capability.type==V4L2_FRMSIZE_TYPE_DISCRETE) resolutions.push_back(resolution{.type=V4L2_FRMSIZE_TYPE_DISCRETE,.discrete=capability.discrete});
		/*else{ if(capability.type==V4L2_FRMSIZE_TYPE_STEPWISE) resolutions.push_back(resolution{.type=V4L2_FRMSIZE_TYPE_STEPWISE,.stepwise=capability.stepwise});*/
		else{ std::cout << "Non-discrete type for vl42 resolution. It's (currently) not worth our time to use this." << std::endl;}
		capability.index++; //Increment the thing.
		// ^ the most unhelpful comment ever
	}
	for(auto &i : resolutions){
		if(i.type==V4L2_FRMSIZE_TYPE_DISCRETE){
			std::cout << "Resolution: " << i.discrete.width << " : " << i.discrete.height << std::endl;
		}
	}
}

cv::Mat VideoReader::getMat() {
	if (hasFirstFrame) return cv::Mat(height, width, CV_8UC2, currentBuffer);
	else {
		std::cerr << "Frame was requested from uninitialized camera " << deviceFile << "!" << std::endl;
		throw NotInitializedException();
	}
}   
void VideoReader::reset(){
	closeReader();
	sleep(2); //Can this be lowered? I've changed it from 4 to 2, here's hoping it doesn't make anything explode...
	openReader();
}
int VideoReader::getWidth(){
	return (int) this->width; // I want to know the story behind this cast
}
int VideoReader::getHeight(){
	return this->height;
}

void VideoReader::setExposureVals(bool isAuto, int exposure) {
	
	struct v4l2_ext_controls controls;
	memset(&controls, 0, sizeof(controls));
	struct v4l2_ext_control ctrlArray[2];
	memset(&ctrlArray, 0, sizeof(ctrlArray));

	controls.controls = ctrlArray;
	// if exposure is auto, ignore exposure value
	controls.count = isAuto ? 1 : 2;
	controls.which = V4L2_CTRL_WHICH_CUR_VAL;
	controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;

	ctrlArray[0].id = V4L2_CID_EXPOSURE_AUTO;
	ctrlArray[1].id = V4L2_CID_EXPOSURE_ABSOLUTE;

	// V4L2_EXPOSURE_AUTO does not work
	ctrlArray[0].value = (isAuto ? V4L2_EXPOSURE_APERTURE_PRIORITY : V4L2_EXPOSURE_MANUAL);
	ctrlArray[1].value = exposure;

	if (ioctl(camfd, VIDIOC_S_EXT_CTRLS, &controls) < 0) {
		perror("VIDIOC_S_EXT_CTRLS");
	}
}


bool ThreadedVideoReader::grabFrame() {
	resetLock.lock(); resetLock.unlock(); // If resetting, wait until done
	bool goodGrab = VideoReader::grabFrame();
	if (goodGrab) { // We've successfully grabbed a frame. Record frame time and reset the timeout.
		auto now = timeout_clock.now();

		++frameTimeIdx;
		if (frameTimeIdx >= frameTimeCount) frameTimeIdx = 0;
		frameTimes[frameTimeIdx] = now;
		last_update = now; 
	}
	return goodGrab;
}
ThreadedVideoReader::ThreadedVideoReader(int width, int height, const char* file, std::function<void(void)> newFrameCallback)
: VideoReader(width, height, file) {
	this->newFrameCallback=newFrameCallback;
	timeout_clock=std::chrono::steady_clock();
	last_update = timeout_clock.now();

	mainLoopThread = std::thread([this]() {
		openReader();
		
		resetTimeoutThread = std::thread(&ThreadedVideoReader::resetterMonitor,this); //Start monitoring thread.

		while (true) {
			if (grabFrame()) {
				resetLock.lock(); resetLock.unlock(); // If resetting, wait until done
				this->newFrameCallback();
			}
		}
	});
}

double ThreadedVideoReader::getMeanFrameInterval() {
	auto now = timeout_clock.now();
	// A binary search would be more efficient, but whatever
	double count = 0;
	int i = frameTimeIdx;
	while (now - frameTimes[i] < std::chrono::milliseconds(1000)) {
		--i;
		++count;
		if (i < 0) i = frameTimeCount - 1;
	}
	return 1.0/count;
}

void ThreadedVideoReader::resetterMonitor(){ // Seperate thread that resets the camera buffers if it hangs.
	while (true) {
		if((timeout_clock.now()-last_update) > ioctl_timeout){
			std::cerr << "Camera " << deviceFile << " not responding. Resetting..." << std::endl;
			reset();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Because I'm a janky spinlock
	}
}
void ThreadedVideoReader::reset(){
	std::cout << "Camera " << deviceFile << " resetting..." << std::endl;
	resetLock.lock();
	VideoReader::reset();
	last_update = timeout_clock.now();
	resetLock.unlock();
	std::cout << "Camera " << deviceFile << " reset." << std::endl;

}
const std::chrono::steady_clock::time_point ThreadedVideoReader::getLastUpdate(){
	return last_update;
}
/* int ThreadedVideoReader::setResolution(int width, int height)
** This function attempts to set the resolution of the camera stream to the given values, 
**  resetting the feed in the process.
** It returns 0 upon success, 1 if given invalid resolution dimensions for the camera, 
**  and 2 if some other error occurs.
** It is guaranteed to lock the camera until it is done.
*/
int ThreadedVideoReader::setResolution(unsigned int width,unsigned int height){
	bool foundValidResolution=false;
	for(auto& res : resolutions){ //Yes, this is less efficient than it could be, but we're talking about with a list with <50 items here.
		if(res.discrete.width==width && res.discrete.height==height){
			foundValidResolution=true; 
			break;
		}
	}
	if(!foundValidResolution) return 1; //Given resolution is invalid.
	this->width=width; this->height=height;
	std::cout << "Changing resolution to " << width << ":" << height << " ..." << std::endl;
	reset();
	std::cout << "Succesfully reset resolution" << std::endl;
	return 0;
}


// https://gist.github.com/thearchitect/96ab846a2dae98329d1617e538fbca3c
void VideoWriter::openWriter(int width, int height, const char* file) {		
	v4l2lo = open(file, O_WRONLY|O_CLOEXEC);
	if(v4l2lo < 0) {
		std::cout << "Error opening v4l2l device: " << strerror(errno);
		exit(-2);
	}
	struct v4l2_format v;
	int t;
	v.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	t = ioctl(v4l2lo, VIDIOC_G_FMT, &v);
	if( t < 0 ) {
		exit(t);
	}
	v.fmt.pix.width = width;
	v.fmt.pix.height = height;
	v.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	vidsendsiz = width * height * 2;
	v.fmt.pix.sizeimage = vidsendsiz;
	t = ioctl(v4l2lo, VIDIOC_S_FMT, &v);
	if( t < 0 ) {
		exit(t);
	}
}

void VideoWriter::writeFrame(cv::Mat& frame) {
	assert(frame.total() * frame.elemSize() == vidsendsiz);
	
	if (write(v4l2lo, frame.data, vidsendsiz) == -1) {
		perror("writing frame");
	}
}
