#pragma once

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>

// Angles are in radians, distances are in inches.
struct VisionData {
	// distance along the floor to the a point directly below the targets.
	double distance;
	
	// angle of tapes from directly facing robot.
	// Positive when tapes are turned counter-clockwise relative to robot.
	// Unused in 2020.
	double tapeAngle;
	
	// robotAngle is the angle on the floor; i.e. the angle that the robot would have to turn to directly face the target. It is determined using the known value of the height between the camera and the targets. It is positive if the robot would have to turn counterclockwise to face the targets, i.e. if the tapes are on the left side of the camera's image.
	double robotAngle;
};

// TODO: make this an actual struct this year
struct VisionDrawPoints {
	// First, 8 points on corners of vision targets.
	// Then those points, reprojected using the target location.
	// the next two points are a line of color 1
	// the next four points form two squares of color 2
	// the remaining point pairs are lines of color 2
	
	cv::Point2f contour[4];
};

// Draw an overlay representing the vision targets
void drawVisionPoints(VisionDrawPoints& toDraw, cv::Mat& image);
void DrawPoints(std::vector<cv::Point>& toDraw, cv::Mat& drawOn);

struct VisionTarget {
	VisionData calcs;
	VisionDrawPoints drawPoints;
};

// The main vision processing function, which processes a single frame.
VisionTarget doVision(cv::Mat image);
//std::vector<cv::Point> doVision(cv::Mat image);

extern bool isImageTesting;
extern bool verboseMode;

namespace calib {
	extern cv::Mat cameraMatrix, distCoeffs;
	extern int width, height;
}
