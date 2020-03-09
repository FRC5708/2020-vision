#include "Displays.h"
void overlayPOV(cv::Mat& frame,pov_state state){
    //Draws an overlay depending on POV state. This is to let the driver know which cameras are currently "forward".
    if(state!=pov_state::neither){
        cv::putText(
            frame,
            (state==pov_state::front ? "-Front-" : "-Rear"),
            {50,50},
            1,
            2,
            {0,128},
            1,
            8
        );
    }
}
void VisionCamera::annotateFrame(cv::Mat& frame){
    annotateVisionFrame(frame);

    // draw thing to see if camera is updating
	{
		static std::chrono::steady_clock::time_point beginTime = timing_clock.now();

		// one revolution per second
		double angle = 2*M_PI * 
		std::chrono::duration_cast<std::chrono::duration<double>>(timing_clock.now() - beginTime).count();
		cv::line(frame, 
		{ frame.cols/2, frame.rows/2 }, 
		{ (int) round(frame.cols/2 * (1 - sin(angle))), (int) round(frame.rows/2 * (1 - cos(angle))) },
		{ 0, 0 });
	}
} 
void IntakeCamera::annotateFrame(cv::Mat& frame){} 
void ForwardCamera::annotateFrame(cv::Mat& frame){} 
void BackwardCamera::annotateFrame(cv::Mat& frame){} 