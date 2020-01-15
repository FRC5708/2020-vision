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

void VideoReader::openReader(int width, int height, const char* file) {
    this->width = width; this->height = height;

    // http://jwhsmith.net/2014/12/capturing-a-webcam-stream-using-v4l2/
    // https://jayrambhia.com/blog/capture-v4l2

    camfd = open(file, O_RDWR);
    if (camfd == -1) {
        perror("open");
        exit(1);
    }

    struct v4l2_capability cap;
    if(ioctl(camfd, VIDIOC_QUERYCAP, &cap) < 0){
        perror("VIDIOC_QUERYCAP");
        exit(1);
    }
    if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)){
        fprintf(stderr, "The device does not handle single-planar video capture.\n");
        exit(1);
    }

    struct v4l2_format format;
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if(ioctl(camfd, VIDIOC_S_FMT, &format) < 0){
        perror("VIDIOC_S_FMT");
        exit(1);
    }

    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufrequest.memory = V4L2_MEMORY_MMAP;
    bufrequest.count = 4;

    timeout_clock=std::chrono::steady_clock();
    last_update = timeout_clock.now();
    std::thread threadObj(resetTimeout); //Start monitoring thread.

    if(ioctl(camfd, VIDIOC_REQBUFS, &bufrequest) < 0){
        perror("VIDIOC_REQBUFS");
        exit(1);
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
            exit(1);
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
            exit(1);
        }
        memset(buffers[i], 0, bufferinfo.length);
    }

    // get framerate
    struct v4l2_frmivalenum frameinterval;
    frameinterval.index = 0;
    frameinterval.width = width;
    frameinterval.height = height;
    frameinterval.pixel_format = V4L2_PIX_FMT_YUYV;
    ioctl(camfd, VIDIOC_ENUM_FRAMEINTERVALS, &frameinterval);
    std::cout << "frame interval: " << frameinterval.discrete.numerator
        << "/" << frameinterval.discrete.denominator << std::endl;

    // Activate streaming
    int type = bufferinfo.type;
    if(ioctl(camfd, VIDIOC_STREAMON, &type) < 0){
        perror("VIDIOC_STREAMON");
        exit(1);
    }

    for (unsigned int i = 0; i < bufrequest.count; ++i) {
        memset(&bufferinfo, 0, sizeof(bufferinfo));
        bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        bufferinfo.memory = V4L2_MEMORY_MMAP;
        bufferinfo.index = i;

        if(ioctl(camfd, VIDIOC_QBUF, &bufferinfo) < 0){
            perror("VIDIOC_QBUF");
            exit(1);
        }
    }
    grabFrame(true);
}

void VideoReader::resetTimeout(){
    //Seperate thread that resets the camera buffers if it hangs.
    while(1){
        if((timeout_clock.now()-last_update) > ioctl_timeout){
            ioctl(camfd, VIDIOC_STREAMOFF, &bufrequest); // Reset the pipeline
            ioctl(camfd, VIDIOC_STREAMON, &bufrequest);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); //Don't waste CPU cycles
    }
}

void VideoReader::grabFrame(bool firstTime) {
    //cv::Mat otherBuffer;
    
    memset(&bufferinfo, 0, sizeof(bufferinfo));
    bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufferinfo.memory = V4L2_MEMORY_MMAP;
    // The buffer's waiting in the outgoing queue.
    int ret = ioctl(camfd, VIDIOC_DQBUF, &bufferinfo);
    if(ret < 0){
       if(ret == EAGAIN){
            perror("VIDIOC_DQBUF -- EAGAIN") //Ioctl timed out.
            return; //We're already borked; abort this cycle.
       }
       else{ 
            perror("VIDIOC_DQBUF"); //Some other horrifying error.
            exit(1);
       }
  }

    
    currentBuffer = buffers[bufferinfo.index];
    //std::cout << "buffer index: " << bufferinfo.index << " addr: " << currentBuffer << std::endl;
    assert((signed) bufferinfo.length == width*height*2);

    // put the old buffer back into the queue
    if(!firstTime && ioctl(camfd, VIDIOC_QBUF, &bufferinfo) < 0){
        perror("VIDIOC_QBUF");
        exit(1);
    }
    last_update = timeout_clock.now(); //We succesfully grabbed a frame. Reset the timeout.
}
cv::Mat VideoReader::getMat() {
    return cv::Mat(height, width, CV_8UC2, currentBuffer);
}   
/*void VideoReader::setExposure(int value) {
    struct v4l2_ext_controls controls;
    memset(&controls, 0, sizeof(controls));
    if (ioctl(camfd, VIDIOC_G_EXT_CTRLS, &controls) < 0) {
        perror ("setExposure: VIDIOC_G_EXT_CTRLS");
        return;
    }

    for (unsigned i = 0; i < controls.count; ++i) {
        switch (controls.controls[i].id) {
        case V4L2_CID_EXPOSURE_AUTO:
            controls.controls[i].value = V4L2_EXPOSURE_MANUAL;

         case V4L2_CID_EXPOSURE_ABSOLUTE: 
            controls.controls[i].value = value;
        } 
    }

    if (ioctl(camfd, VIDIOC_S_EXT_CTRLS, &controls) < 0) {
        perror("setExposure: VIDIOC_S_EXT_CTRLS");
    }
}*/
void VideoReader::setExposureVals(bool isAuto, int exposure) {
    /*struct v4l2_ext_controls controls;
    memset(&controls, 0, sizeof(controls));
    struct v4l2_ext_control ctrlArray[30];
    memset(&ctrlArray, 0, sizeof(ctrlArray));

    controls.controls = ctrlArray;
    controls.count = sizeof(ctrlArray) / sizeof(v4l2_ext_control);
    controls.which = V4L2_CTRL_WHICH_CUR_VAL;

    if (ioctl(camfd, VIDIOC_G_EXT_CTRLS, &controls) < 0) {
        perror ("resetExposure: VIDIOC_G_EXT_CTRLS");
        return;
    }
    std::cout << "controls count: " << controls.count << std::endl;

    for (unsigned i = 0; i < controls.count; ++i) {
        switch (controls.controls[i].id == V4L2_CID_EXPOSURE_AUTO) {
            controls.controls[i].value = V4L2_EXPOSURE_AUTO;
        }
    }
    if (ioctl(camfd, VIDIOC_S_EXT_CTRLS, &controls) < 0) {
        perror("resetExposure: VIDIOC_S_EXT_CTRLS");
    }*/
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


// https://gist.github.com/thearchitect/96ab846a2dae98329d1617e538fbca3c
void VideoWriter::openWriter(int width, int height, const char* file) {		
    v4l2lo = open(file, O_WRONLY);
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
