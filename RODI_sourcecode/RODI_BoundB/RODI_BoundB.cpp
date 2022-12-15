/*
//========================================================================================================================================
// RODI - Riverine Organism Drift Imager (Bounding Box script)
// This script was developped by Frédéric de Schaetzen and Patryk Nienaltowski, ETH Zürich for the following publication:
//
//
//
// This script is an adaptation from examples provided by FLIR systems (these are included in their SPINNAKER SDK)
// and SyncFlir developped by Guillermo Hidalgo Gadea (MIT License Copyright (c) 2021 GuillermoHidalgoGadea.com -
// Sourcecode: https://gitlab.ruhr-uni-bochum.de/ikn/syncflir).
//
// script parameters are updated using the metadata.txt file.
//
//
//
//
//
// FLIR MAKES NO REPRESENTATIONS OR WARRANTIES ABOUT THE SUITABILITY OF THE
// SOFTWARE, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE, OR NON-INFRINGEMENT. FLIR SHALL NOT BE LIABLE FOR ANY DAMAGES
// SUFFERED BY LICENSEE AS A RESULT OF USING, MODIFYING OR DISTRIBUTING
// THIS SOFTWARE OR ITS DERIVATIVES.
//========================================================================================================================================
*/

#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>
#include <assert.h>
#include <time.h>
#include "SpinVideo.h"
#include <algorithm>
#include <dirent.h>
#include <boost/filesystem.hpp>
#include <stdio.h>
#include <conio.h>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace Spinnaker;
using namespace Spinnaker::GenApi;
using namespace Spinnaker::GenICam;
using namespace Spinnaker::Video;
using namespace std;
using namespace cv;
namespace fs = boost::filesystem;

// Initialize Config parameters with standards, will be updated by metadata config file, some of these are redundant and not used in this script
string inpath;
string outpath;
std::vector<std::string> filenames;
string backgroundpath;
int imageHeight = 1200;
int imageWidth = 1920;

/*
========================================================================================================================================
inpaint creates an extended background using the opencv inpaint function
========================================================================================================================================
*/
Mat extended_frame(Mat frame, Mat extended_background)
{
	Mat extended_background_clone = extended_background.clone();
	Mat roi_frame;
	try
	{
		roi_frame = extended_background_clone(Rect(150, 150, frame.cols, frame.rows));
	}
	catch (...)
	{
		std::cerr << "Trying to create roi out of image boundaries" << std::endl;
	}
	frame.copyTo(roi_frame);
	return extended_background_clone;
}
/*
========================================================================================================================================
inpaint creates an extended background using the opencv inpaint function
========================================================================================================================================
*/
tuple<bool, vector<Rect>> frameCheck(Mat frame, Mat extended_background)
{
	useOptimized();
	//copy the curren frame into the extended background
	Mat frame_final = extended_frame(frame, extended_background);

	//Convert images to grayscale
	Mat gray, background_gray;
	cvtColor(frame_final, gray, COLOR_BGR2GRAY);
	cvtColor(extended_background, background_gray, COLOR_BGR2GRAY);

	//Background subtraction
	Mat diff;
	absdiff(background_gray, gray, diff);

	// gaussian blur
	Mat diffblur;
	blur(diff, diffblur, Size(3, 3));

	// Binarization
	Mat binary;
	threshold(diffblur, binary, 20, 255, THRESH_BINARY);

	// Find contours
	vector<vector<Point>> contours;
	vector<Vec4i> hierarchy;
	vector<Rect> boundRect;
	findContours(binary, contours, hierarchy, RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));
	//cout << contours.size() << " contours have been found in this image" << endl;
	if (contours.size() == 0)
	{
		Point p(0, 0);
		return make_tuple(false, boundRect);
	}
	else
	{
		int areaTresh = 500;
		vector<vector<Point> > contoursFilter;
		vector<float> areas(contours.size());
		vector<vector<Point>> contours_poly;
		int c1 = 0;
		for (int n = 0; n < (int)contours.size(); n++) {
			areas[n] = contourArea(contours[n], false);
			//drawContours(frame, contours, n, Scalar(0, 255, 50), 1.5, LINE_8, hierarchy);
			//cout << areas[n] << endl;
			if (areas[n] > areaTresh)
			{
				contoursFilter.push_back(contours[n]);
				Mat poly;
				approxPolyDP(Mat(contours[n]), poly, 3, true);
				contours_poly.push_back(poly);
				boundRect.push_back(boundingRect(poly));
				int l_edge = max((int)(boundRect[c1].width), (int)(boundRect[c1].height)) * 1.5;
				boundRect[c1].width = (int)l_edge;
				boundRect[c1].height = (int)l_edge;
				Moments m = moments(contours[n], false);
				Point p(m.m10 / m.m00, m.m01 / m.m00);
				boundRect[c1].x = (int)(p.x - (l_edge / 2));
				boundRect[c1].y = (int)(p.y - (l_edge / 2));
				c1++;
			}
		}
		return make_tuple(true, boundRect);
	}
}

/*
========================================================================================================================================
inpaint creates an extended background using the opencv inpaint function
========================================================================================================================================
*/
Mat inpaint(string backgroundpath)
{
	cout << "--- Creating extended background frame. ---";
	Mat background = imread(backgroundpath, 1);
	Mat extension(background.rows + 300, background.cols + 300, CV_8UC3, Scalar(0, 0, 0)); // create a frame that is 300 pixels extended in the x and y direction based on the background frame.
	Mat roi; // create an empty Mat object
	try
	{
		roi = extension(Rect(150, 150, background.cols, background.rows)); // define the 
	}
	catch (...)
	{
		std::cerr << "Trying to create roi out of extension image boundaries" << std::endl;
	}
	background.copyTo(roi); // copies the background frame to the roi of the extension frame
							//Create an 8 bit binary mask
	Mat extension_mask(background.rows + 300, background.cols + 300, CV_8UC1, Scalar(255)); // define a mask frame (255 = white) that is 300 pixels extened in the x and y direction based on the background frame.
	Mat background_mask(background.rows, background.cols, CV_8UC1, Scalar(0)); // create a mask frame (0 = black) that had has the same dimensions as the background frame.
	Mat roi_mask; // create an empty Mat object
	try
	{
		roi_mask = extension_mask(Rect(150, 150, background_mask.cols, background_mask.rows)); // define the roi_mask as 
	}
	catch (...)
	{
		std::cerr << "Trying to create roi out of image boundaries" << std::endl;
	}
	background_mask.copyTo(roi_mask); // copies the black background mask to the white extension mask.
	Mat extended_background; // create a empty Mat object
	inpaint(extension, extension_mask, extended_background, 4, cv::INPAINT_TELEA); // create an extended background based on the extension and extension mask frames. This will extend the background nased 
	cout << "	Complete!" << endl << endl;
	return extended_background;
}
/*
========================================================================================================================================
BoundingBoxAnalysis loops over each frame within each .tmp file and extracts bounding boxes of drifting objects
========================================================================================================================================
*/
int BoundingBoxAnalysis(vector<string>& filenames, int numFiles, Mat extended_background)
{
	int result = 0;
	int frameSize = imageHeight * imageWidth;
	try
	{
		for (int fileCnt = 0; fileCnt < numFiles; fileCnt++) // Open each .tmp file and extract images of size = frameSize
		{
			string FilePath = filenames.at(fileCnt);
			cout << endl << "--- Retrieving frames from: " << FilePath.c_str() << " ---" << endl;
			ifstream rawFile(filenames.at(fileCnt).c_str(), ios_base::in | ios_base::binary);
			if (!rawFile)
			{
				cout << endl << "Could not open file! " << filenames.at(fileCnt).c_str() << "Press enter to exit." << endl;
				getchar();
				return -1;
			}
			// Frame retrieval from .tmp files. Binary data chunks (size = frameSize) are taken and imported into the frames-vector.
			std::vector<cv::Mat> frames; // Create an empty frames vector of Mat objects
			int frameCnt = 0; // set frameCnt to zero
			cout << "Object detected in frames: ";
			while (rawFile.good() && frameCnt < 1000)
			{
				// Reading frames from .tmp file
				char* frameBuffer = new char[frameSize];
				rawFile.read(frameBuffer, frameSize); //Extracts frameSize characters from the .tmp file and assigns it to frameBuffer
				ImagePtr pImage = Image::Create(imageWidth, imageHeight, 0, 0, PixelFormat_BayerRG8, frameBuffer); // create an BayerRG8 Image Pointer 
																												   // Transform binary image into an OpenCV Mat
				ImagePtr convertedImage = pImage->Convert(PixelFormat_BGR8, HQ_LINEAR);
				unsigned int XPadding = convertedImage->GetXPadding(); // image data contains padding. When allocating Mat container size, you need to account for the X,Y image data padding. 
				unsigned int YPadding = convertedImage->GetYPadding();
				unsigned int rowsize = convertedImage->GetWidth();
				unsigned int colsize = convertedImage->GetHeight();
				Mat frame = cv::Mat(colsize + YPadding, rowsize + XPadding, CV_8UC3, convertedImage->GetData(), convertedImage->GetStride());
				Mat frame_extended = extended_frame(frame, extended_background);
				// Analyze frame for bounding boxes
				bool answer;
				vector<Rect> boundingBox; // create an empty boundingBox vector of Rect Objects
				tie(answer, boundingBox) = frameCheck(frame, extended_background);
				if (answer == 1 && (int)boundingBox.size() > 0)
				{
					auto image_rect = Rect({}, frame_extended.size());
					cout << (int)frameCnt << ", ";
					for (int k = 0; k < (int)boundingBox.size(); k++)
					{
						auto roi = boundingBox[k];
						auto intersection = image_rect & roi;
						auto intersection_roi = intersection - roi.tl();
						Mat crop = cv::Mat::zeros(roi.size(), frame_extended.type());
						frame_extended(intersection).copyTo(crop(intersection_roi));
						string boxFilename = outpath + "\\" + FilePath.substr(inpath.length() + 1, FilePath.length() - (inpath.length() + 5)) + "_frame" += to_string(frameCnt) + "_box" += to_string(k) + ".tif";
					cv:imwrite(boxFilename, crop);
					}
					delete[] frameBuffer; // delete frameBuffer
				}
				else
				{
					delete[] frameBuffer; // delete frameBuffer
				}
				frameCnt++; // update frame counter
			}
			cout << endl << endl;
			rawFile.close(); // Close .tmp file
		}
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Error: " << e.what() << endl;
		cout << "Press enter to exit." << endl;
		getchar();
		return -1;
	}
	return result;
}


/*
========================================================================================================================================
Main function of the script. In here input and output folders are defined and the bounding box analysis started.
========================================================================================================================================
*/
int main(int /*argc*/, char** /*argv*/)
{
	int result = 0;
	// Print application build information
	cout << "*************************************************************" << endl;
	cout << "Application build date: " << __DATE__ << " " << __TIME__ << endl;
	cout << "*************************************************************" << endl;
	// ASCII logo: http://patorjk.com/software/taag/#p=display&h=3&v=2&f=Slant%20Relief&t=RODI_conv
	std::cout << R"(  

____/\\\\\\\\\___________/\\\\\_______/\\\\\\\\\\\\_____/\\\\\\\\\\\_        
 __/\\\///////\\\_______/\\\///\\\____\/\\\////////\\\__\/////\\\///__       
  _\/\\\_____\/\\\_____/\\\/__\///\\\__\/\\\______\//\\\_____\/\\\_____      
   _\/\\\\\\\\\\\/_____/\\\______\//\\\_\/\\\_______\/\\\_____\/\\\_____     
    _\/\\\//////\\\____\/\\\_______\/\\\_\/\\\_______\/\\\_____\/\\\_____    
     _\/\\\____\//\\\___\//\\\______/\\\__\/\\\_______\/\\\_____\/\\\_____   
      _\/\\\_____\//\\\___\///\\\__/\\\____\/\\\_______/\\\______\/\\\_____  
       _\/\\\______\//\\\____\///\\\\\/_____\/\\\\\\\\\\\\/____/\\\\\\\\\\\_ 
        _\///________\///_______\/////_______\////////////_____\///////////__
                           
)";
	// Automatic input of filenames based on input folder (inpath) to be converted by boost filesystem.
	cout << endl << "Specifiy input folder containing the binary files (path format example: C:\\RODI) and press enter: " << endl;
	getline(cin, inpath); // read entire line
	cout << endl << "--- Collecting pathnames from folder: " << inpath << " ---";
	fs::path p(inpath);
	for (auto i = fs::directory_iterator(p); i != fs::directory_iterator(); i++)
	{
		if (!is_directory(i->path()))
		{
			filenames.push_back(i->path().string());
		}
		else
		{
			continue;
		}
	}
	cout << "	Complete!" << endl << endl;
	// Specify an output folder (outpath), and test its writing permissions, in which converted files will be saved.
	cout << endl << "Specifiy an output folder (path format example: C:\\RODI) and press enter:" << endl;
	getline(cin, outpath); // read entire line
						   // Check the writing permissions of the specified output folder
	cout << endl << "--- Checking writing permissions. ---";
	string testpath = outpath + "/test.txt";
	const char* testfile = testpath.c_str();
	FILE* tempFile = fopen(testfile, "w+");
	if (tempFile == nullptr)
	{
		cout << "Failure to create test-file in Output-folder. Please check folder permissions." << endl;
		cout << "Press enter to exit." << endl;
		getchar();
		return -1;
	}
	fclose(tempFile);
	remove(testfile);
	cout << "	Complete!" << endl << endl;
	// Specify the background image path
	cout << endl << "Specifiy a background image in tiff format (path format example C:\\RODI\\background.tif) and press enter:" << endl;
	getline(cin, backgroundpath); // read entire line
								  // Create enlarged background image
	Mat extended_background = inpaint(backgroundpath);
	// Print the filenames that will be analyzed
	int numFiles = filenames.size();
	string num = to_string(numFiles);
	cout << endl << "--- Analyzing " + num + " files for bounding boxes. ---" << endl;
	for (int files = 0; files < numFiles; files++)
	{
		cout << "	+" << filenames[files] << endl;
	}
	cout << endl << "Press enter to analyze files." << endl << endl;
	getchar();
	// Generate bounding boxes from .tmp file
	result = BoundingBoxAnalysis(filenames, numFiles, extended_background);
	// Testing ascii logo print
	// ASCII logo: http://patorjk.com/software/taag/#p=display&h=3&v=2&f=Slant%20Relief&t=RODI_conv
	std::cout << R"(  

____/\\\\\\\\\___________/\\\\\_______/\\\\\\\\\\\\_____/\\\\\\\\\\\_        
 __/\\\///////\\\_______/\\\///\\\____\/\\\////////\\\__\/////\\\///__       
  _\/\\\_____\/\\\_____/\\\/__\///\\\__\/\\\______\//\\\_____\/\\\_____      
   _\/\\\\\\\\\\\/_____/\\\______\//\\\_\/\\\_______\/\\\_____\/\\\_____     
    _\/\\\//////\\\____\/\\\_______\/\\\_\/\\\_______\/\\\_____\/\\\_____    
     _\/\\\____\//\\\___\//\\\______/\\\__\/\\\_______\/\\\_____\/\\\_____   
      _\/\\\_____\//\\\___\///\\\__/\\\____\/\\\_______/\\\______\/\\\_____  
       _\/\\\______\//\\\____\///\\\\\/_____\/\\\\\\\\\\\\/____/\\\\\\\\\\\_ 
        _\///________\///_______\/////_______\////////////_____\///////////__
                           
)";
	// Print application build information
	cout << "*************************************************************" << endl;
	cout << "Application build date: " << __DATE__ << " " << __TIME__ << endl;
	cout << "*************************************************************" << endl;
	cout << endl << "--- All files were succesfully analyzed for bounding boxes! Press enter to exit. ---" << endl;
	getchar();
	return result;
}
