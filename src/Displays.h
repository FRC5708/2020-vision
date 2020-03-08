#include "VideoHandler.hpp"
#include <iostream>

/* class Display(ThreadedVideoReader* videoReader)
** Virtual Wrapper class to associate certain cameras with displays and state
*/
class Display{
protected:
	enum struct pov_state{front,rear}; //Whether the camera view is currently in the "front" or "back" of the robot.
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
		annotateFrame(frame);
		return frame;
	};
};
class VisionCamera : public Display{
    void annotateFrame(cv::Mat& frame) override;
	std::function<void(cv::Mat& frame)> annotateVisionFrame;
    public:
        VisionCamera(ThreadedVideoReader* reader, std::function<void(cv::Mat& drawOn)> annotateVisionFrame){
			this->videoReader=reader;
			this->annotateVisionFrame=annotateVisionFrame;
			}
};
class IntakeCamera : public Display{
    void annotateFrame(cv::Mat& frame) override;
    public:
        IntakeCamera(ThreadedVideoReader* reader){this->videoReader=reader;}
};
class ForwardCamera : public Display{
    void annotateFrame(cv::Mat& frame) override;
    public:
        ForwardCamera(ThreadedVideoReader* reader){this->videoReader=reader;}
};
class BackwardCamera : public Display{
    void annotateFrame(cv::Mat& frame) override;
    public:
        BackwardCamera(ThreadedVideoReader* reader){this->videoReader=reader;}
};
class UnknownCamera : public Display{
    void annotateFrame(cv::Mat&){} //Do nothing.
    public:
        UnknownCamera(ThreadedVideoReader* reader){this->videoReader=reader;}
};