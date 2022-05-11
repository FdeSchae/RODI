#include <opencv2/opencv.hpp>
#include "opencv2/core/cuda.hpp"
#include <iostream>
#include <string>
#include <fstream>
#include <stdio.h>
#include <conio.h>

using namespace cv;
using namespace std;


tuple<bool, Point> frameCheck(Mat frame, Mat background) {

    useOptimized();

    //Convert images to grayscale
    Mat gray, background_gray;
    cvtColor(frame, gray, COLOR_BGR2GRAY);
    cvtColor(background, background_gray, COLOR_BGR2GRAY);
  
    //Background subtraction
    Mat diff;
    absdiff(background_gray, gray, diff);

    // Binarization
    Mat binary;
    threshold(diff, binary, 70, 255, THRESH_BINARY);

    // Find contours
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(binary, contours, hierarchy, RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));

    // max contour
    double maxArea = 0;
    int maxAreaContourId = -1;
    for (int j = 0; j < contours.size(); j++) {
        double newArea = contourArea(contours.at(j));
        if (newArea > maxArea) {
            maxArea = newArea;
            maxAreaContourId = j;
        }
    }
    
    // Contour moments
    Moments m = moments(contours[maxAreaContourId], false);

    int areaThresh = 1000;

    if (maxArea > areaThresh){
        //center of the contour
        Point p(m.m10 / m.m00, m.m01 / m.m00);
        return make_tuple(true, p);
    }
    else {
        Point p(0, 0);
        return make_tuple(false, p);
    }
}

tuple<bool, Point> frameCheck(Mat frame) {

    useOptimized();

    //Convert images to grayscale
    Mat gray;
    cvtColor(frame, gray, COLOR_BGR2GRAY);

    // Binarization
    Mat binary;
    threshold(gray, binary, 70, 255, THRESH_BINARY);

    // Find contours
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(binary, contours, hierarchy, RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));

    // max contour
    double maxArea = 0;
    int maxAreaContourId = -1;
    for (int j = 0; j < contours.size(); j++) {
        double newArea = cv::contourArea(contours.at(j));
        if (newArea > maxArea) {
            maxArea = newArea;
            maxAreaContourId = j;
        }
    }

    // Contour moments
    Moments m = moments(contours[maxAreaContourId], false);

    int areaThresh = 1000;

    if (maxArea > areaThresh) {
        //center of the contour
        Point p(m.m10 / m.m00, m.m01 / m.m00);
        return make_tuple(true, p);
    }
    else {
        Point p(0, 0);
        return make_tuple(false, p);
    }
}



int main()
{

    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    // Read the image file
    Mat background = imread("C:/Users/patry/Desktop/RODI/back.tiff");
    Mat frame = imread("C:/Users/patry/Desktop/RODI/fish2.tiff");
    
    resize(frame, frame, Size(747, 467), INTER_LINEAR);
    resize(background, background, Size(747, 467), INTER_LINEAR);

    // Check for failure
    if (frame.empty())
    {
        cout << "Could not open or find the image" << endl;
        cin.get(); //wait for any key press
        return -1;
    }

    bool answer;
    Point center;

    tie(answer, center) = frameCheck(frame, background);
    chrono::steady_clock::time_point end =chrono::steady_clock::now();
    cout << "----------------------------------------------------------------" << endl;
    cout << "Time difference = " << chrono::duration_cast<chrono::milliseconds>(end - begin).count() << "[ms]" << std::endl;
    cout << "----------------------------------------------------------------" << endl;


    //system("cls");
    cout << "----------------------------------------------------------------" << endl;
    cout << "Is the object in the frame?: " << answer << endl;
    cout << "x= " << center.x << "\t";
    cout << "y= " << center.y << "\n";
    cout << "----------------------------------------------------------------" << endl;

    //pseudo frames selector
    int imageCenter = int(frame.size().width / 2);
    if (center.x < imageCenter) {
        //take n previous frames and m next frames
        int n = 3;
        int m = 7;
        cout << "----------------------------------------------------------------" << endl;
        cout << "Taking " << n << " previous frames and " << m << " next frames" << endl;
        cout << "----------------------------------------------------------------" << endl;
    }
    else {
        //take m previous frames and n next frames
        int n = 7;
        int m = 3;
        cout << "----------------------------------------------------------------" << endl;
        cout <<"Taking "<< n << " previous frames and " << m << " next frames" << endl;
        cout << "----------------------------------------------------------------" << endl;
    }
    
    drawMarker(frame, center, Scalar(0, 255, 0), MARKER_CROSS, 20, 4);
    line(frame, Point(imageCenter, 0), Point(imageCenter, frame.size().height), Scalar(255, 0, 0), 2);
    //drawing = circle(dis, center, 3, (255, 0, 0), 2);
    imshow("Input", frame);
    waitKey(0); // Wait for any keystroke in the window
    destroyAllWindows();
 
}