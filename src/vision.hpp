#pragma once

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>

struct VisionData {
	double height;
	bool isPort;
	double distance;
	
	// Angles: left/down=negative, right/up=positive
	
	// angle of tapes from directly facing robot.
	// Positive when tapes are turned counter-clockwise relative to robot.
	double tapeAngle;
	
	// angle of robot from directly facing tapes.
	// Positive when tapes are on the left side of the camera's image.
	double robotAngle;
};

// TODO: make this an actual struct this year
struct VisionDrawPoints {
	// First, 8 points on corners of vision targets.
	// Then those points, reprojected using the target location.
	// the next two points are a line of color 1
	// the next four points form two squares of color 2
	// the remaining point pairs are lines of color 2
	
	cv::Point2f points[30];
};

// Draw an overlay representing the vision targets
void drawVisionPoints(VisionDrawPoints& toDraw, cv::Mat& image);
void DrawPoints(std::vector<cv::Point>& toDraw, cv::Mat& drawOn);

struct VisionTarget {
	VisionData calcs;
	VisionDrawPoints drawPoints;
};

// The main vision processing function, which processes a single frame.
//std::vector<VisionTarget> doVision(cv::Mat image);
std::vector<cv::Point> doVision(cv::Mat image);

extern bool isImageTesting;
extern bool verboseMode;

namespace calib {
	extern cv::Mat cameraMatrix, distCoeffs;
	extern int width, height;
}
