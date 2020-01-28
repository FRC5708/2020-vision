#pragma once
#include <opencv2/objdetect/objdetect.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/features2d.hpp>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <vector>
#include <string>
#include <math.h>

namespace grip {

/**
* GripHexFinder class.
* 
* An OpenCV pipeline generated by GRIP.
*/
class GripHexFinder {
	private:
		cv::Mat hslThresholdOutput;
		std::vector<std::vector<cv::Point> > findContoursOutput;
		std::vector<std::vector<cv::Point> > filterContoursOutput;
		std::vector<std::vector<cv::Point> > convexHullsOutput;
		void hslThreshold(cv::Mat &, double [], double [], double [], cv::Mat &);
		void findContours(cv::Mat &, bool , std::vector<std::vector<cv::Point> > &);
		void filterContours(std::vector<std::vector<cv::Point> > &, double , double , double , double , double , double , double [], double , double , double , double , std::vector<std::vector<cv::Point> > &);
		void convexHulls(std::vector<std::vector<cv::Point> > &, std::vector<std::vector<cv::Point> > &);

	public:
		GripHexFinder();
		void Process(cv::Mat& source0);
		cv::Mat* GetHslThresholdOutput();
		std::vector<std::vector<cv::Point> >* GetFindContoursOutput();
		std::vector<std::vector<cv::Point> >* GetFilterContoursOutput();
		std::vector<std::vector<cv::Point> >* GetConvexHullsOutput();
};


} // end namespace grip


