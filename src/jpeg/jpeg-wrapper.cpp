#include "jpeg-wrapper.hpp"
#include "jpegfixer.hpp"

#include <iostream>
#include <thread>
#include <algorithm>


// They can't be methods because they have to be passed to pure C code
size_t wrapper_get_data(void* buf, size_t outputSize, userptr_t* user) {
	MJpegDecoder* wrapper = (MJpegDecoder *) user;
	
	std::unique_lock<std::mutex> ul(wrapper->waitMutex);
	while (wrapper->bufPos >= wrapper->encodedBuf.size()) {
		//std::cerr << "waiting for new frame..." << std::endl;
		wrapper->condVar.wait(ul);
	}

	assert(wrapper->encodedBuf.size() >= wrapper->bufPos);
	size_t copyLen = std::min(wrapper->encodedBuf.size() - wrapper->bufPos, outputSize);
	assert(copyLen > 0);
	memcpy(buf, wrapper->encodedBuf.data(), copyLen);
	wrapper->bufPos += copyLen;

	//std::cerr << "sending " << copyLen << " bytes to decoder. Input len:" << wrapper->bufLen << " Output len:" << outputSize << std::endl;
	return copyLen;
}
void wrapper_decoded_buffer(void* buf, size_t length, userptr_t* user) {
	MJpegDecoder* wrapper = (MJpegDecoder *) user;

	// TODO: convert color space
	
}

void MJpegDecoder::addFrame(void* buf, size_t len, std::function<void(cv::Mat)> callback) {
	std::cerr << "starting decode of buffer of length " << len << std::endl;

	std::unique_lock<std::mutex> ul(waitMutex);

	//encodedBuf = fixJpeg(buf, len);
	encodedBuf.clear();
	encodedBuf.insert(encodedBuf.end(), (uint8_t*) buf, ((uint8_t*) buf)+len);

	/*FILE* debugOutput = fopen("/home/pi/frame.jpeg", "w");
	std::cerr << "writing to file " << encodedBuf.size() << " bytes and exiting" << std::endl;
	fwrite(encodedBuf.data(), encodedBuf.size(), 1, debugOutput);
	fclose(debugOutput);
	exit(0);*/

	//encodedBuf = buf; 
	//bufLen = len;
	bufPos = 0;
	currentCallback = callback;

	condVar.notify_all();
}

MJpegDecoder::MJpegDecoder() {

	std::thread([this]() {

		decode_jpeg_video((userptr_t*) this, &wrapper_get_data, &wrapper_decoded_buffer);
	}).detach();
}