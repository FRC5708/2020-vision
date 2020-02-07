#include "vision.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <chrono>
#include <memory>
#include <cmath>

#include "GripHexFinder.hpp"

#define PI 3.14159265

// this function from opencv/samples/cpp/tutorial_code/calib3d/camera_calibration/camera_calibration.cpp
using std::vector;
using std::cout; using std::endl;

bool isImageTesting = false;
bool verboseMode = false;

namespace calib {
	cv::Mat cameraMatrix, distCoeffs;
	int width, height;
}


bool isNanOrInf(double in) {
	return isnan(in) || isinf(in);
}

// All the constants

constexpr double inchTapesHeightAboveGround = 6*12 + 9 + 1/4;
constexpr double inchTapesWidthTop = 3*12 + 3 + 1/4;
constexpr double inchSideTapesLength = 1*12 + 7 + 5/8;
constexpr double inchTapesHeight = 1*12 + 5;

const double inchTapesWidthBottom = inchTapesWidthTop
 - 2*sqrt(pow(inchSideTapesLength, 2) - pow(inchTapesHeight, 2));
 
 constexpr double inchCameraHeightAboveGround = 5*12; // TODO: Set me to my actual value!

struct ContourCorners {
	cv::Point topleft, topright, bottomright, bottomleft;
	bool valid;
	ContourCorners() : topleft(0, 0),
	topright(INT_MAX, 0),
	bottomright(INT_MAX, INT_MAX),
	bottomleft(0, INT_MAX),
	valid(true) {}
};
void printContourCorners(ContourCorners corners) {
	std::cout << "tl: " << corners.topleft << " tr: " << corners.topright << " bl: " << corners.bottomleft << " br: " << corners.bottomright;
}

ContourCorners getContourCorners(std::vector<cv::Point>& contour) {
	//std::chrono::steady_clock clock;
	//auto startTime = clock.now();

	//https://github.com/opencv/opencv/blob/master/samples/cpp/squares.cpp
	ContourCorners result;
	std::vector<cv::Point> approx;
	double fittingError=.01;
	double fittingStep = 0.001;
	char last_change=0;
	double arcLength = cv::arcLength(contour,true);
	constexpr unsigned char REFINE_FAIL_COUNT=6; //Make this number higher to improve accuracy, decrease it to improve speed.
	std::chrono::steady_clock::time_point begin;
	std::chrono::steady_clock::time_point end;
	if(verboseMode){
		begin = std::chrono::steady_clock::now();
	}
	while (true) {
    	approxPolyDP(contour,approx,arcLength*fittingError,true);
 		//std::cout << approx.size() << " " << "points generated by approxPolyDP with fittingError: " << fittingError <<std::endl;
		if (approx.size() == 4 ) break;
		if(fittingStep < 0.0000001){
			//Failure case; 
			std::cout << "approxPolyDP was unable to find a valid quadrilateral. Be forewarned." << std::endl;
			result.valid = false;
			return result;
		}

		if(((unsigned short) approx.size()) - 4 > 0){
			//We have too many sides, aka too low of a fitting error
			if(last_change==1){
				//We're jumping back and forth over the optimum
				fittingStep /= 2.0;
			}
			fittingError+=fittingStep;
			last_change=-1;
		}else{
			//We have too few sides, we need a less lenient fitting error
			if(last_change==-1){
				//We're jumping back and forth over the optimum
				fittingStep /= 2.0;
			}
			fittingError-=fittingStep;
			last_change=1;
		}
	}
	if(verboseMode){
	end = std::chrono::steady_clock::now();
	std::cout << "DEBUG: ApproxPolyDP initial solve time difference (ms): " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() <<std::endl;
	begin=end;
	std::cout << "DEBUG: Initial fittingError: " << fittingError << std::endl;
	}
	for(int i=0;i<REFINE_FAIL_COUNT;){
		//Attempts to refine the solution, to obtain the lowest fitting error that produces a quadrilateral.
		//After REFINE_FAIL_COUNT fails, it exits.
		fittingError-=fittingStep;
		approxPolyDP(contour,approx,arcLength*fittingError,true);
		if(approx.size()!=4){
			i+=1; //Increment our failure counter
			fittingError+=fittingStep; //Undo our last jump
			fittingStep /= 2.0; //Decrease the step size.
		}
	}
	if(verboseMode){
	end = std::chrono::steady_clock::now();
	std::cout << "DEBUG: ApproxPolyDP refinement solve time difference (ms): " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() <<std::endl;
	std::cout << "DEBUG: Final fittingError: " << fittingError << std::endl;
	}

	approxPolyDP(contour,approx,arcLength*fittingError,true); //One last run, so that we have our best result.
	
	if (verboseMode) {
		std::cout << "points:";
		for (auto i : approx) {
			std::cout << " [" << i.x << "," << i.y << "]";
		}
		std::cout << std::endl;
	}

	for(auto i: approx) {
		
		short t_s=0,r_s=0;
		for(auto j: approx){
			t_s+=(short) i.y>=j.y;
			r_s+=(short) i.x>=j.x;
		}
		if(t_s>=3){
			if(r_s>=3){
				result.bottomright=i;
			}else{
				result.bottomleft=i;
			}
		}else{
			if(r_s>=3){
				result.topright=i;
			}else{
				result.topleft=i;
			}
		}
	}

	//std::cout << "getContourCorners time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
	//	clock.now() - startTime).count() << std::endl;

	return result;
}

bool matContainsNan(cv::Mat& in) {
	for (unsigned int i = 0; i < in.total(); ++i) {
		if (in.type() == CV_32F && isnan(in.at<float>(i))) return true;
		else if (in.type() == CV_64F && isnan(in.at<double>(i))) return true;
	}
	return false;
}

void drawVisionPoints(VisionDrawPoints& toDraw, cv::Mat& image) {
	/*cv::Scalar mainColor(255, 0, 0);
	cv::Scalar rodColor(0, 0, 255);
	cv::Scalar rawPointColor(0, 255, 0);
	cv::Scalar dotColor(0, 255, 255);*/

	cv::Scalar mainColor(0, 0);
	cv::Scalar rodColor(170, 200);
	cv::Scalar rawPointColor(128, 0);
	cv::Scalar dotColor(255, 255);
	
	
    for(auto p : toDraw.contour){
        cv::circle(image, p, 1, rawPointColor);
    }

	/*
	for (int i = 0; i < 8; ++i) {
		cv::circle(image, toDraw.points[i], 1, rawPointColor, 2);
	}
	for (int i = 8; i < 16; ++i) {
		cv::circle(image, toDraw.points[i], 1, rodColor, 2);
	}
	for (int i = 18; i < 22; ++i) {
		int oppPoint = i + 1;
		if (oppPoint == 22) oppPoint = 18;
		cv::line(image, toDraw.points[i], toDraw.points[oppPoint], mainColor, 1);
	}
	for (int i = 22; i < 26; ++i) {
		int oppPoint = i + 1;
		if (oppPoint == 26) oppPoint = 22;
		cv::line(image, toDraw.points[i], toDraw.points[oppPoint], mainColor, 1);
	}
	for (int i = 26; i < (int) (sizeof(VisionDrawPoints) / sizeof(cv::Point2f)); i += 2) {
		cv::line(image, toDraw.points[i], toDraw.points[i + 1], mainColor, 2);
	}
	cv::line(image, toDraw.points[16], toDraw.points[17], rodColor, 2);
	cv::circle(image, toDraw.points[16], 2, dotColor, 4);*/
	
}

cv::Mat* debugDrawImage;
void showDebugPoints(VisionDrawPoints& toDraw) {
	if (!isImageTesting) return;
	cv::Mat drawOn = debugDrawImage->clone();

	drawVisionPoints(toDraw, drawOn);
	
    /*
    cv::namedWindow("projection");
	imshow("projection", drawOn);
	cv::waitKey(0);
	*/

    //save the jpg image
    static int imgNum = 0;
	++imgNum;
	cv::imwrite("./debugimg_" + std::to_string(imgNum) + ".jpg", drawOn);

}

void DrawPoints(std::vector<cv::Point>& toDraw, cv::Mat& drawOn){
    //if(!isImageTesting) return;

	cv::Scalar rawPointColor(128, 0);
    
    for(auto p : toDraw){
        cv::circle(drawOn, p, 1, rawPointColor); 
    }

    /*static int imgNum = 0;
	++imgNum;
	cv::imwrite("./pdebugimg_" + std::to_string(imgNum) + ".jpg", drawOn);
    */
}

void invertPose(cv::Mat& rotation_vector, cv::Mat& translation_vector, cv::Mat& cameraRotationVector, cv::Mat& cameraTranslationVector) {
	cv::Mat R;
	cv::Rodrigues(rotation_vector, R);
	cv::Rodrigues(R.t(),cameraRotationVector);
	cameraTranslationVector = -R.t()*translation_vector;
}

// Gets useful values from the output of solvePnP. 
// https://docs.opencv.org/2.4/modules/calib3d/doc/camera_calibration_and_3d_reconstruction.html (old documentation) and https://docs.opencv.org/4.2.0/d9/d0c/group__calib3d.html#ga549c2075fac14829ff4a58bc931c033d (newer, but slightly worse documentation) go into some (insufficent) detail about solvePnP. 
struct SolvePnpResult {
	// rvec and tvec are returned from solvePnP. (Go read the above documents and stare at the diagram if you haven't already.) They represent a transform between two coordinate spaces; camera space, and world space. Camera space's origin is at the camera, its x-axis is left, its y-axis is down, and z-axis is outwards. World space is whatever we define it to be in the worldPoints parameter passed to solvePnP.
	// tvec is a vector in camera space that represents the translation from the origin of camera space to the origin of world space. From there, rvec somehow tells us what direction the axes of world coordinate space points.
	cv::Mat rvec, tvec;
	
	double pixError;
	
	// X, Y, and height are the robot's position in a coordinate system oriented so that the X axis is on the plane of the targets and the Y axis points out of the targets. X is right postive, Y is forwards positive, and height is up positive. the breakdown into X, Y, and height is often inaccurate. TotalDist is usually accurate.
	double inchTotalDist, inchHeight, inchRobotX, inchRobotY;
	
	VisionData output;
	bool valid;

	SolvePnpResult(cv::Mat rvec, cv::Mat tvec, double reprojError,
	std::vector<cv::Point3f> worldPoints, std::vector<cv::Point2f> imagePoints) :
	rvec(rvec), tvec(tvec), pixError(reprojError) {
		assert(tvec.type() == CV_64F && rvec.type() == CV_64F);

		if (matContainsNan(rvec) || matContainsNan (tvec)) {
			std::cout << "solvePnP returned NaN!\n";
			valid = false;
			return;
		} 
		
		// Get the translation in the world coordinate space.
		cv::Mat rotation, translation;
		invertPose(rvec, tvec, rotation, translation);
		
		inchRobotX = translation.at<double>(0);
		inchRobotY = translation.at<double>(2);
		inchHeight = -translation.at<double>(1);
		
		inchTotalDist = sqrt(pow(tvec.at<double>(0), 2) + pow(tvec.at<double>(1), 2) + pow(tvec.at<double>(2), 2));
		
		output.distance = sqrt(pow(inchTotalDist, 2) - pow(inchTapesHeightAboveGround - inchCameraHeightAboveGround, 2));
		
		output.tapeAngle = -atan2(inchRobotX, inchRobotY);
		
		// I'm 90% sure that this works
		output.robotAngle = -asin(tvec.at<double>(0) / output.distance);	
		
		valid = true;	
	}

	SolvePnpResult() {
		valid = false;
	}
};

struct ProcessPointsResult {
	bool success;
	double pixError;
	VisionTarget t;
};
ProcessPointsResult processPoints(ContourCorners trapezoid,
 int pixImageWidth, int pixImageHeight) {

	// There might be a bug in openCV that would require the focal length to be multiplied by 2.
	// Test this.

	// world coords: (0, 0, 0) at bottom center of tapes
	// up and right are positive. y-axis is vertical.
	std::vector<cv::Point3f> worldPoints = {
		cv::Point3f(-inchTapesWidthTop/2, inchTapesHeight, 0),
		cv::Point3f(inchTapesWidthTop/2, inchTapesHeight, 0),
		cv::Point3f(-inchTapesWidthBottom/2, 0, 0),
		cv::Point3f(inchTapesWidthBottom/2, 0, 0),
	};
	std::vector<cv::Point2f> imagePoints = {
		trapezoid.topleft, trapezoid.topright, trapezoid.bottomleft, trapezoid.bottomright
	};

	std::vector<double> reprojErrors;
	std::vector<cv::Mat> rvecs, tvecs;
	cv::solvePnPGeneric(worldPoints, imagePoints, calib::cameraMatrix, calib::distCoeffs,
	 rvecs, tvecs, false, cv::SOLVEPNP_IPPE, cv::noArray(), cv::noArray(), reprojErrors);
	 
	// SOLVEPNP_IPPE returns up to 2 results.
	SolvePnpResult result1(rvecs[0], tvecs[0], reprojErrors.at(0), worldPoints, imagePoints);
	SolvePnpResult result2(rvecs[1], tvecs[1], reprojErrors.at(1), worldPoints, imagePoints);

	// solvePnpGeneric sorts results by reprojection error
	assert(result1.pixError <= result2.pixError);
	
	double pixMaxError = std::max(3.0, (trapezoid.topright.x - trapezoid.topleft.x + trapezoid.bottomright.y - trapezoid.topleft.y) / 2.0 / 12.0);

	SolvePnpResult* resultUsing = nullptr;
	
	std::cout << "result1: err:" << result1.pixError << " x:" << result1.tvec.at<double>(0) << " y:" << result1.tvec.at<double>(1) << " z:" << result1.tvec.at<double>(2) << "\n"
		 << "result2: err:" << result1.pixError << " x:" << result2.tvec.at<double>(0) << " y:" << result2.tvec.at<double>(1) << " z:" << result2.tvec.at<double>(2) << "\n"
		  << "maxError:" << pixMaxError << std::endl;

	if (result1.pixError > pixMaxError) return { false, {}};
	else if (result2.pixError > pixMaxError) resultUsing = &result1;
	else {

		// TODO: guess which solution is correct.
		// This isn't ambiguity isn't terribly important this year, so this doesn't have to be done.
		
		// Temporary solution:
		resultUsing = &result1;
	}
	
	std::cout << "  Using:" << ((resultUsing == &result1) ? "result1" : "result2") << std::endl;


	auto rsize = resultUsing->rvec.size();
	auto tsize = resultUsing->tvec.size();
	if (!(rsize == cv::Size(1, 3) || rsize == cv::Size(3, 1)) && (tsize == cv::Size(1, 3) || tsize == cv::Size(3, 1))) {
		std::cout << "SolvePnP returned stuff with wrong sizes" << std::endl;
		return { false, {}};
	}

/*
	VisionData result;
	result.distance = sqrt(pow(resultUsing->inchRobotX, 2) + pow(resultUsing->inchRobotY, 2));
	result.tapeAngle = -atan2(resultUsing->inchRobotX, resultUsing->inchRobotY);
	result.robotAngle = -asin(resultUsing->tvec.at<double>(0) / result.distance);
	*/

	if (isNanOrInf(resultUsing->output.distance) || isNanOrInf(resultUsing->output.robotAngle) || isNanOrInf(resultUsing->output.tapeAngle)) {
		std::cout << "encountered NaN or Infinity" << std::endl;
		return { false, {} };
	}
	
	//double radReferencePitch = fmod((radPitch + 2*M_PI), M_PI); // make positive
	//if (radReferencePitch > M_PI_2) radReferencePitch = M_PI - radReferencePitch;
	
	VisionDrawPoints draw;
	std::copy(imagePoints.begin(), imagePoints.end(), draw.contour);
	/*
	constexpr float CROSSHAIR_LENGTH = 4,
	 FLOOROUT_LENGTH = 33,
	 FLOOROUT_WIDTH = 27.5,
	 FLOOROUT_HEIGHT = 14;

	worldPoints.insert(worldPoints.end(), {
		cv::Point3f(inchTapeBottomsApart/2, 0, 0),

		cv::Point3f(0, 0, CROSSHAIR_LENGTH), cv::Point3f(0, 0, -CROSSHAIR_LENGTH),
		
		cv::Point3f(CROSSHAIR_LENGTH, CROSSHAIR_LENGTH, 0), cv::Point3f(-CROSSHAIR_LENGTH, CROSSHAIR_LENGTH, 0),
		cv::Point3f(-CROSSHAIR_LENGTH, -CROSSHAIR_LENGTH, 0), cv::Point3f(CROSSHAIR_LENGTH, -CROSSHAIR_LENGTH, 0),

		cv::Point3f(-FLOOROUT_WIDTH/2, -FLOOROUT_HEIGHT, 0), cv::Point3f(FLOOROUT_WIDTH/2, -FLOOROUT_HEIGHT, 0),
		cv::Point3f(FLOOROUT_WIDTH/2, -FLOOROUT_HEIGHT, FLOOROUT_LENGTH),
		cv::Point3f(-FLOOROUT_WIDTH/2, -FLOOROUT_HEIGHT, FLOOROUT_LENGTH), 
		
		cv::Point3f(-CROSSHAIR_LENGTH, 0, 0), cv::Point3f(CROSSHAIR_LENGTH, 0, 0),
		cv::Point3f(0, -CROSSHAIR_LENGTH, 0), cv::Point3f(0, CROSSHAIR_LENGTH, 0)
	});
	
	cv::Mat projPoints;
	cv::projectPoints(worldPoints, resultUsing->rvec, resultUsing->tvec, calib::cameraMatrix, calib::distCoeffs, projPoints);
	assert(projPoints.type() == CV_32FC2);
	std::copy(projPoints.begin<cv::Point2f>(), projPoints.end<cv::Point2f>(), draw.points + 8);
	
	if (isImageTesting) showDebugPoints(draw);*/
	
	return { true, resultUsing->pixError, resultUsing->output, draw };
}

VisionTarget doVision(cv::Mat image) {
	if (isImageTesting) debugDrawImage = &image;

    grip::GripHexFinder finder;
    finder.Process(image);

    //convert lines to contours
    std::vector<std::vector<cv::Point> > conts=*(finder.GetConvexHullsOutput());
    
    cout << "Found " << conts.size() << " contours" << std::endl; 
    for(auto c : conts){
       //filter out contours that don't make sense

        //ensure contour area is at least a certain percent of the image
        double imageArea = image.rows*image.cols;
        double contArea = cv::contourArea(c);
        double contPerc = contArea/imageArea;
        if(contPerc > 0.01 && c.size() > 4){
            ContourCorners bestCont = getContourCorners(c);
            std::vector<cv::Point2f> largestCont;
            largestCont.push_back(bestCont.topleft); largestCont.push_back(bestCont.topright);
            largestCont.push_back(bestCont.bottomleft);largestCont.push_back(bestCont.bottomright);
            sort(largestCont.begin(), largestCont.end(), 
                [](const cv::Point& a, const cv::Point& b) -> bool{
                    return a.y > b.y;
                });
            
            double topAng = abs(atan((largestCont[0].y - largestCont[1].y)/(largestCont[0].x - largestCont[1].x))) * 180.0 / PI;
            double botAng = abs(atan((largestCont[2].y - largestCont[3].y)/(largestCont[2].x - largestCont[3].x))) * 180.0 / PI;
            if(topAng < 15.0 && botAng < 15.0){
                //if(){
                    //the top and bottoms are relatively aligned (within 10 pixels)
                    std::vector<ProcessPointsResult> targets;        
                    try { 
                        auto result = processPoints(bestCont, image.cols, image.rows);
                        if(result.success){
                            targets.push_back(result);
                        }
                    } catch(const cv::Exception& e){
                        std::cout << "a cv::Exception was thrown" << std::endl;
                        continue;
                    }
                
                    //return largestCont;
                    for(auto target : targets){
                        std::cout << "distance: " << target.t.calcs.distance << " robotAngle: " << target.t.calcs.robotAngle << std::endl;
                    }

                    // TODO: In the rare case that there's more than one result, choose which one to return
                    if (targets.size() > 0) return targets[0].t;
                //}
            }
        }
    }
	return {};
}
