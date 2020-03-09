#include "VideoHandler.hpp"
#include <iostream>
#include <chrono>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

/* class Display(ThreadedVideoReader* videoReader)
** Virtual Wrapper class to associate certain cameras with displays and state
*/
enum struct pov_state{front,rear,neither}; //Whether the camera view is currently in the "front" or "back" of the robot. Neither is for cameras that aren't associated with either.

class Display{
protected:
	pov_state pov=pov_state::neither;
	ThreadedVideoReader* videoReader;
	virtual void annotateFrame(cv::Mat& frame) = 0; //Draw an overlay over the frame before sending it to streamer.
	bool flipped=false;
public:
	cv::Mat getMat(){
		cv::Mat frame=videoReader->getMat();
		if(flipped){
			cv::Mat flipped;
			cv::flip(frame, flipped, -1);
			for (int x = 0; x < flipped.cols; x += 2) for (int y = 0; y < flipped.rows; ++y) {
				std::swap(flipped.at<cv::Vec2b>(y, x)[1], flipped.at<cv::Vec2b>(y, x+1)[1]);
			}
			frame=flipped;
		}
		try{
			annotateFrame(frame);
		}catch(std::exception& e){
			std::cerr << "annotateFrame threw " << e.what() << std::endl;
		}
		return frame;
	};
	void setPOV(pov_state reference){
		//Set pov to reference. If pov is neither, do nothing.
		if(pov!=pov_state::neither) pov=reference;
	}
	string getName(){
		return videoReader->getName();
	}
};
class VisionCamera : public Display{
    void annotateFrame(cv::Mat& frame) override;
	std::function<void(cv::Mat& frame)> annotateVisionFrame;
	std::chrono::steady_clock timing_clock;
    public:
        VisionCamera(ThreadedVideoReader* reader, std::function<void(cv::Mat& drawOn)> annotateVisionFrame){
			pov=pov_state::front;
			this->videoReader=reader;
			this->annotateVisionFrame=annotateVisionFrame;
		}
};
class IntakeCamera : public Display{
    void annotateFrame(cv::Mat& frame) override;
    public:
        IntakeCamera(ThreadedVideoReader* reader){
			pov=pov_state::rear;
			this->videoReader=reader;
			flipped=true;
		}
};
class ForwardCamera : public Display{
    void annotateFrame(cv::Mat& frame) override;
    public:
        ForwardCamera(ThreadedVideoReader* reader){
			pov=pov_state::front;
			this->videoReader=reader;
		}
};
class BackwardCamera : public Display{
    void annotateFrame(cv::Mat& frame) override;
    public:
        BackwardCamera(ThreadedVideoReader* reader){
			pov=pov_state::rear;
			this->videoReader=reader;
		}
};
class UnknownCamera : public Display{
    void annotateFrame(cv::Mat&){} //Do nothing.
    public:
        UnknownCamera(ThreadedVideoReader* reader){this->videoReader=reader;}
};