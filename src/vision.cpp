#include "vision.hpp"
#include "RedContourGrip.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

#include "GripHexFinder.hpp"

// this function from opencv/samples/cpp/tutorial_code/calib3d/camera_calibration/camera_calibration.cpp
using std::vector;
using std::cout; using std::endl;
namespace cv {
	static double computeReprojectionErrors( const vector<Point3f>& objectPoints,
											const vector<Point2f>& imagePoints,
											const Mat& rvec, const Mat& tvec,
											const Mat& cameraMatrix , const Mat& distCoeffs) {
		vector<Point2f> imagePoints2;
		size_t totalPoints = 0;
		double totalErr = 0, err;

		projectPoints(objectPoints, rvec, tvec, cameraMatrix, distCoeffs, imagePoints2);
		
		err = norm(imagePoints, imagePoints2, NORM_L2);

		size_t n = objectPoints.size();
		totalErr        += err*err;
		totalPoints     += n;

		return std::sqrt(totalErr/totalPoints);
	}
}

bool isImageTesting = false;
bool verboseMode = false;

namespace calib {
	cv::Mat cameraMatrix, distCoeffs;
	int width, height;
}


bool isNanOrInf(double in) {
	return isnan(in) || isinf(in);
}

// from http://answers.opencv.org/question/16796/computing-attituderoll-pitch-yaw-from-solvepnp/?answer=52913#post-id-52913
cv::Vec3d getEulerAngles(cv::Mat &rotation) {
	cv::Mat rotCamerMatrix;
	cv::Rodrigues(rotation, rotCamerMatrix);

    cv::Mat cameraMatrix,rotMatrix,transVect,rotMatrixX,rotMatrixY,rotMatrixZ;
	cv::Vec3d eulerAngles;
    double* _r = rotCamerMatrix.ptr<double>();
    double projMatrix[12] = {_r[0],_r[1],_r[2],0,
                          _r[3],_r[4],_r[5],0,
                          _r[6],_r[7],_r[8],0};

    decomposeProjectionMatrix(cv::Mat(3,4,CV_64FC1,projMatrix),
                               cameraMatrix,
                               rotMatrix,
                               transVect,
                               rotMatrixX,
                               rotMatrixY,
                               rotMatrixZ,
                               eulerAngles);
	return eulerAngles;
}

struct ProcessPointsResult {
	bool success;
	VisionData calcs;
	VisionDrawPoints drawPoints;
};

// all the constants
constexpr double radTapeOrientation = 14.5/180*M_PI;
constexpr double inchTapesWidth = 2;
constexpr double inchTapesLength = 5.5;
constexpr double inchInnerTapesApart = 8;
const double inchTapesHeight = inchTapesLength*cos(radTapeOrientation) + inchTapesWidth*sin(radTapeOrientation); // from bottom to top of tapes
const double inchTapeTopsApart = inchInnerTapesApart + 2*inchTapesWidth*cos(radTapeOrientation);
const double inchTapeBottomsApart = inchInnerTapesApart + 2*inchTapesLength*sin(radTapeOrientation);
const double inchOuterTapesApart = inchTapeTopsApart + 2*inchTapesLength*sin(radTapeOrientation); // from outermost edge
const double inchHatchTapesAboveGround = 2*12+7.5 - inchTapesHeight;
const double inchPortTapesAboveGround = 3*12+3.125 - inchTapesHeight;

// For reference: old cameras' FOV is 69°, new camera is 78°


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
	cv::circle(image, toDraw.points[16], 2, dotColor, 4);
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

struct SolvePnpResult {
	cv::Mat rvec, tvec;
	double pixError, inchHeight, inchRobotX, inchRobotY;
	bool valid;

	SolvePnpResult(cv::Mat rvec, cv::Mat tvec, 
	std::vector<cv::Point3f> worldPoints, std::vector<cv::Point2f> imagePoints) :
	rvec(rvec), tvec(tvec) {
		assert(tvec.type() == CV_64F && rvec.type() == CV_64F);

		cv::Mat rotation, translation;
		invertPose(rvec, tvec, rotation, translation);

		if (matContainsNan(rvec) || matContainsNan (tvec)) {
			std::cout << "solvePnP returned NaN!\n";
			valid = false;
			return;
		} 

		pixError = cv::computeReprojectionErrors(worldPoints, imagePoints, rvec, tvec, calib::cameraMatrix, calib::distCoeffs);

		cv::Vec3d angles = getEulerAngles(rvec);
		
		// x is right postive, y is forwards positive
		inchRobotX = translation.at<double>(0);
		inchRobotY = translation.at<double>(2);
		inchHeight = -translation.at<double>(1);	

		valid = true;	
	}

	bool withinHeight() {
		return inchHeight > 3 && inchHeight < 8;
	}

	SolvePnpResult() {
		valid = false;
	}
};
SolvePnpResult prevResult;


ProcessPointsResult processResult(SolvePnpResult* resultUsing, 
std::vector<cv::Point3f>& worldPoints, std::vector<cv::Point2f>& imagePoints, cv::Point2f lastImagePoint) {

	auto rsize = resultUsing->rvec.size();
	auto tsize = resultUsing->tvec.size();
	if (!(rsize == cv::Size(1, 3) || rsize == cv::Size(3, 1)) && (tsize == cv::Size(1, 3) || tsize == cv::Size(3, 1))) {
		std::cout << "SolvePnP returned stuff with wrong sizes" << std::endl;
		return { false, {}};
	}

	VisionData result;
	result.distance = sqrt(pow(resultUsing->inchRobotX, 2) + pow(resultUsing->inchRobotY, 2));

	result.tapeAngle = -atan2(resultUsing->inchRobotX, resultUsing->inchRobotY);
	result.robotAngle = -asin(resultUsing->tvec.at<double>(0) / result.distance);

	if (isNanOrInf(result.distance) || isNanOrInf(result.robotAngle) || isNanOrInf(result.tapeAngle)) {
		std::cout << "encountered NaN or Infinity" << std::endl;
		return { false, {} };
	}

	
	//double radReferencePitch = fmod((radPitch + 2*M_PI), M_PI); // make positive
	//if (radReferencePitch > M_PI_2) radReferencePitch = M_PI - radReferencePitch;

	VisionDrawPoints draw;
	std::copy(imagePoints.begin(), imagePoints.end(), draw.points);
	draw.points[7] = lastImagePoint;

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
	
	if (isImageTesting) showDebugPoints(draw);
	
	prevResult = *resultUsing;
	return { true, result, draw };
}

ProcessPointsResult processPoints(ContourCorners left, ContourCorners right,
 int pixImageWidth, int pixImageHeight) {
	
	if (verboseMode) {
		std::cout << "left: ";
		printContourCorners(left);
		std::cout << "\nright: ";
		printContourCorners(right);
		std::cout << std::endl;
	}

	// There might be a bug in openCV that would require the focal length to be multiplied by 2.
	// Test this.

	// world coords: (0, 0, 0) at bottom center of tapes
	// up and right are positive. y-axis is vertical.
	std::vector<cv::Point3f> worldPoints = {
		cv::Point3f(-inchOuterTapesApart/2, inchTapesWidth*sin(radTapeOrientation), 0),
		cv::Point3f(inchOuterTapesApart/2, inchTapesWidth*sin(radTapeOrientation), 0),
		cv::Point3f(-inchTapeTopsApart/2, inchTapesHeight, 0),
		cv::Point3f(inchTapeTopsApart/2, inchTapesHeight, 0),
		cv::Point3f(-inchInnerTapesApart/2, inchTapesLength*cos(radTapeOrientation), 0),
		cv::Point3f(inchInnerTapesApart/2, inchTapesLength*cos(radTapeOrientation), 0),
		cv::Point3f(-inchTapeBottomsApart/2, 0, 0),
		//cv::Point3f(inchTapeBottomsApart/2, 0, 0)
	};
	std::vector<cv::Point2f> imagePoints = {
		left.bottomleft, right.bottomright, left.topleft, right.topright,
		left.topright, right.topleft, left.bottomright//, right.bottom
	};

	cv::Mat rvec, tvec;
	bool retval = cv::solvePnP(worldPoints, imagePoints, calib::cameraMatrix, calib::distCoeffs, rvec, tvec, false, cv::SOLVEPNP_ITERATIVE);
	//std::cout << "solvePnP withoutprev retval: " << retval << std::endl;
	SolvePnpResult resultWithoutPrevious(rvec, tvec, worldPoints, imagePoints);

	SolvePnpResult* resultUsing = nullptr;
	SolvePnpResult resultWithPrevious;

	if (prevResult.valid) {
		rvec = prevResult.rvec; tvec = prevResult.tvec;
		try {
			cv::solvePnP(worldPoints, imagePoints, calib::cameraMatrix, calib::distCoeffs, rvec, tvec, true, cv::SOLVEPNP_ITERATIVE);
		}
		catch(const cv::Exception e) {
			std::cerr << "cv::solvePnP useExtrinsicGuess threw a cv::Exception: " << e.msg << std::endl;
			resultUsing = &resultWithoutPrevious;
			return processResult(resultUsing, worldPoints, imagePoints, right.bottomleft);
		}

		resultWithPrevious = SolvePnpResult(rvec, tvec, worldPoints, imagePoints);

		std::cout << "without prev: err:" << resultWithoutPrevious.pixError << " height:" << resultWithoutPrevious.inchHeight
		<< "with prev: err:" << resultWithPrevious.pixError << " height:" << resultWithPrevious.inchHeight << std::endl;

		double pixMaxError = std::max(3, 
				((left.bottomright.y - left.topleft.y) + (right.bottomleft.y - right.topright.y))/2 / 6);


		if (resultWithoutPrevious.pixError > pixMaxError) resultUsing = &resultWithPrevious;
		if (resultWithPrevious.pixError > pixMaxError) resultUsing = &resultWithoutPrevious;
		if (resultUsing != nullptr && resultUsing->pixError > pixMaxError) return { false, {}};

		if (resultUsing == nullptr) {
			if (resultWithoutPrevious.withinHeight() && !resultWithPrevious.withinHeight()) resultUsing = &resultWithoutPrevious;
			else if (resultWithPrevious.withinHeight() && !resultWithoutPrevious.withinHeight()) resultUsing = &resultWithPrevious;
			else resultUsing = (resultWithoutPrevious.pixError < resultWithPrevious.pixError) 
			? &resultWithoutPrevious : &resultWithPrevious;
		}	 
	}
	else resultUsing = &resultWithoutPrevious;

	if (resultUsing == &resultWithPrevious) std::cout << "Using previous result with useExtrinsicGuess" << std::endl;

	return processResult(resultUsing, worldPoints, imagePoints, right.bottomleft);
}

std::vector<VisionTarget> processContours(
	std::vector< std::vector<cv::Point> >* contours, int imgWidth, int imgHeight) {
    std::vector<cv::Rect> rects;
	std::vector<ContourCorners> contourCorners;

	const float minRectWidth = 10; //pixels 
	const float minRectHeight= 10;
    cout << "num contours: " << contours->size() << endl;
	for (auto i : *contours) {
		cv::Rect rect = cv::boundingRect(i);
		if (rect.width >= minRectWidth && rect.height >= minRectHeight) {
			rects.push_back(rect);
			/*if (verboseMode) {
				std::cout << "contour: " <<
			}*/
			ContourCorners corners = getContourCorners(i);
			if (corners.valid){ 
                contourCorners.push_back(corners);
		        cout << "TL: " << corners.topleft.x << " " << corners.topleft.y <<endl;
		        cout << "TR: " << corners.topright.x << " " << corners.topright.y <<endl;
		        cout << "BL: " << corners.bottomleft.x<<" "<< corners.bottomleft.y<<endl;
		        cout << "BR: " << corners.bottomright.x<<" "<<corners.bottomright.y<<endl;
                cout << endl;
            }
        }
	}

	const float rectSizeDifferenceTolerance = 0.5; // fraction of width/height
	const float rectYDifferenceTolerance = 0.5;
	const float rectDistanceTolerance = 10; // multiplier of the width of one rectangle, that the whole vision target can be

	if (verboseMode) for (auto rect : rects) {
		std::cout << "found rect: x:" << rect.x << ",y:" << rect.y << ",w:" << rect.width << ",h:" << rect.height << std::endl;
	}

	std::vector<VisionTarget> results;
	
	// find rects that are close enough in size and distance
	for (unsigned int i = 0; i < rects.size(); ++i) {
		for (unsigned int j = 0; j < rects.size(); ++j) {
			cv::Rect& left = rects[i];
			cv::Rect& right = rects[j];

			if (left != right &&
				left.br().x < right.tl().x &&
				left.tl().x + (left.width + right.width) / 2 * rectDistanceTolerance > right.br().x &&
			    abs(left.width - right.width) < rectSizeDifferenceTolerance * (left.width + right.width) / 2 &&
				abs(left.height - right.height) < rectSizeDifferenceTolerance * (left.width + right.width) / 2 &&
				abs(left.br().y - right.br().y) < rectYDifferenceTolerance * (left.height + right.height) / 2) {
				// keep around old output for debugging
				//if (verboseMode) processRects(left, right, imgWidth, imgHeight);
				try {
					ProcessPointsResult result = processPoints(
						contourCorners[i], contourCorners[j], imgWidth, imgHeight);

					if (result.success) {
						results.push_back({ result.calcs, result.drawPoints, left, right });
					}
				}
				catch (const cv::Exception e) {
					if (isImageTesting) throw;
					std::cerr << "ProcessPoints threw a cv::Exception: " << e.msg << std::endl;
				}
			}
		}
	}
	// lowest distance first
	std::sort(results.begin(), results.end(), [](VisionTarget a, VisionTarget b) -> bool {
		return a.calcs.distance < b.calcs.distance;
	});
	
	return results;
}

std::vector<cv::Point> doVision(cv::Mat image) {
	if (isImageTesting) debugDrawImage = &image;

	//grip::RedContourGrip finder;
	//finder.Process(image);
    grip::GripHexFinder finder;
    finder.Process(image);

    std::vector<cv::Point> results;
    //convert lines to contours
    std::vector<std::vector<cv::Point> > conts=*(finder.GetConvexHullsOutput());
    cout << "Found " << conts.size() << " contours" << std::endl; 
    int i = 0;
    for(auto c : conts){
        //filter out contours that don't make sense


        cout << "Contour " << i << " with " << c.size() << " points" << endl;
        i++;
        ContourCorners simpleCont = getContourCorners(c);
        printContourCorners(simpleCont);
        results.push_back(simpleCont.topleft);
        results.push_back(simpleCont.topright);
        results.push_back(simpleCont.bottomleft);
        results.push_back(simpleCont.bottomright);
        /*DrawPoints(sCont);*/
        /*for(auto p : simplifiedCont){
            cout << "   ";
            cout << p.x << " " << p.y << endl; 
        }*/
    }

	//auto results1 = processContours(finder.GetBrightContours(), image.cols, image.rows);
	//auto results2 = processContours(finder.GetRedContours(), image.cols, image.rows);

	//results1.insert(results1.begin(), results2.begin(), results2.end());
	//return results1;
    return results;
}


