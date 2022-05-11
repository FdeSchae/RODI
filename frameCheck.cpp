//libraries 
#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <ctime>
#include <assert.h>
#include <time.h>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <string>

//#include <boost/filesystem.hpp>
#include <cstdio>
#include <cstring>
#include <cerrno>


//define namespaces
using namespace std::chrono;
using namespace Spinnaker;
using namespace Spinnaker::GenApi;
using namespace Spinnaker::GenICam;
using namespace std;

// Initialize config parameters, will be updated by config file
std::string TMPpath;
std::string HDDpath;
std::string triggerCam = "20025419"; // serial number of primary camera
double exposureTime = 5000.0; // exposure time in microseconds (i.e., 1/ max FPS)
double dGain = 17.0;
double FPS = 200.0; // frames per second in Hz
double compression = 1.0; // compression factor, this crops the image
int widthToSet;
int heightToSet;
double NewFrameRate;
int numBuffers = 200; // depending on RAM
int numImages = 100; //needs to be at least 100
int totalfiles = 2;
int NumBlocks = 3;
int startfnr;

// Initialize placeholders
vector<ofstream> cameraFiles;
ofstream csvFile;
ofstream metadataFile;
string metadataFilename;
int cameraCnt;
string serialNumber;



/*
=================
The function AcquireImages will retrieve images from the camera and write them into the temporary file
=================
*/
int AcquireImages(CameraPtr pCam, INodeMap& nodeMap, INodeMap& nodeMapTLDevice)
{
	int result = 0;

	cout << endl << endl << "*** IMAGE ACQUISITION ***" << endl << endl;
	try
	{
		// Begin acquiring images
		pCam->BeginAcquisition();

		cout << "Acquiring images..." << endl;

		// Initialize empty parameters outside of locked case
		ImagePtr pResultImage;
		char* imageData;
		int firstFrame = 1;
		int stopwait = 0;
		int fnr = 0;
		cout << endl;
		CreateTMP(serialNumber, cameraCnt, fnr);
		for (unsigned int fnr = 0; fnr < totalfiles; fnr++)
		{
			// Retrieve, convert, and save images
			const unsigned int k_numImages = numImages;

			for (unsigned int imageCnt = 0; imageCnt < k_numImages; imageCnt++)
			{
				try
				{
					// For first loop in while ...
					if (firstFrame == 1)
					{
						// first image is descarted
						ImagePtr pResultImage = pCam->GetNextImage();
						char* imageData = static_cast<char*>(pResultImage->GetData());
						pResultImage->Release();

						// anounce start recording
						cout << "Camera [" << serialNumber << "] " << "Started recording with ID [" << cameraCnt << " ]..." << endl;
					}
					firstFrame = 0;

					// Retrieve image and ensure image completion
					try
					{
						pResultImage = pCam->GetNextImage(1000); // waiting time for NextImage in miliseconds
					}
					catch (Spinnaker::Exception& e)
					{
						// Break recording loop after waiting 1000ms without trigger
						stopwait = 1;
					}

					// Break recording loop when stopwait is activated
					if (stopwait == 1)
					{
						cout << "Trigger signal disconnected. Stop recording for camera [" << serialNumber << "]" << endl;
						// End acquisition
						pCam->EndAcquisition();
						cameraFiles[fnr].close();
						csvFile.close();
						// Deinitialize camera
						pCam->DeInit();
						break;
					}

					// Acquire the image buffer to write to a file
					imageData = static_cast<char*>(pResultImage->GetData());

					// Do the writing to assigned cameraFile
					cameraFiles[fnr].write(imageData, pResultImage->GetImageSize());

					csvFile << pResultImage->GetFrameID() << "," << pResultImage->GetTimeStamp() << "," << serialNumber << "," << fnr << endl;

					// Check if the writing is successful
					if (!cameraFiles[fnr].good())
					{
						cout << "Error writing to file for camera " << cameraCnt << " !" << endl;
						return -1;
					}
					int tf = fnr + 1;
					int mkf = numImages - 50;
					if ((imageCnt == mkf) && (tf < totalfiles))
					{
						fnr++;
						CreateTMP(serialNumber, cameraCnt, fnr);
						fnr--;
					}

					// Release image
					pResultImage->Release();
				}

				catch (Spinnaker::Exception& e)
				{
					cout << "Error: " << e.what() << endl;
					result = -1;
				}

			}

		}
		pCam->EndAcquisition(); //Ending acquisition appropriately helps ensure that devices clean up properly and do not need to be power-cycled to maintain integrity.
		cameraFiles[fnr].close();
		csvFile.close();
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Error: " << e.what() << endl;
		return -1;
	}

	return result;
}


bool frameCheck(Mat frame, Mat background) {

    Mat imS, background_imS;
    resize(frame, imS, Size(747, 467), INTER_LINEAR);
    resize(background, background_imS, Size(747, 467), INTER_LINEAR);

    // Convert to grayscale
    Mat gray, background_gray;
    cvtColor(imS, gray, COLOR_BGR2GRAY);
    cvtColor(background_imS, background_gray, COLOR_BGR2GRAY);

    // Calc the difference image
    Mat diff;
    absdiff(background_gray, gray, diff);
    //imshow("difference", diff);

    Mat binary;
    threshold(diff, binary, 70, 255, THRESH_BINARY);
    //imshow("Binary", binary);

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

    int areaThresh = 1000;

    if (maxArea > areaThresh) {
        return true;
    }
    else {
        return false;
    }
}


Mat ConvertToCVmat(ImagePtr pImage)
{
	//int result = 0;
	ImagePtr convertedImage = pImage->Convert(PixelFormat_BGR8, NEAREST_NEIGHBOR);

	unsigned int XPadding = convertedImage->GetXPadding();
	unsigned int YPadding = convertedImage->GetYPadding();
	unsigned int rowsize = convertedImage->GetWidth();
	unsigned int colsize = convertedImage->GetHeight();

	//image data contains padding. When allocating Mat container size, you need to account for the X,Y image data padding. 
	Mat cvimg = Mat(colsize + YPadding, rowsize + XPadding, CV_8UC3, convertedImage->GetData(), convertedImage->GetStride());
	namedWindow("current Image", CV_WINDOW_AUTOSIZE);
	imshow("current Image", cvimg);
	resizeWindow("current Image", rowsize / 2, colsize / 2);
	waitKey(1);//otherwise the image will not display...

	//return result;
	return cvimg;
}


/*
=================
This is the main entry point for this program
=================
*/
int main(int /*argc*/, char** /*argv*/)
{
	// Since this application saves images in the current folder we must ensure that we have permission to write to this folder. If we do not have permission, fail right away.
	FILE* tempFile = fopen("test.txt", "w+");
	if (tempFile == nullptr)
	{
		cout << "Failed to create file in current folder.  Please check "
			"permissions."
			<< endl;
		cout << "Press Enter to exit..." << endl;
		getchar();
		return -1;
	}
	fclose(tempFile);
	remove("test.txt");

	// Print application build information
	cout << "Application build date: " << __DATE__ << " " << __TIME__ << endl << endl;

	// Retrieve singleton reference to system object
	SystemPtr system = System::GetInstance();

	// Print out current library version
	const LibraryVersion spinnakerLibraryVersion = system->GetLibraryVersion();
	cout << "Spinnaker library version: " << spinnakerLibraryVersion.major << "." << spinnakerLibraryVersion.minor
		<< "." << spinnakerLibraryVersion.type << "." << spinnakerLibraryVersion.build << endl
		<< endl;

	// Retrieve list of cameras from the system
	CameraList camList = system->GetCameras();

	const unsigned int numCameras = camList.GetSize();

	cout << "Number of cameras detected: " << numCameras << endl << endl;

	// Finish if there are no cameras
	if (numCameras == 0)
	{
		// Clear camera list before releasing system
		camList.Clear();

		// Release system
		system->ReleaseInstance();

		cout << "Not enough cameras!" << endl;
		cout << "Done! Press Enter to exit..." << endl;
		getchar();

		return -1;
	}

	// Read config file and update parameters
	readconfig();

	// Create shared pointer to camera
	CameraPtr pCam = nullptr;

	int result = 0;

	// Run example on each camera
	for (unsigned int i = 0; i < numCameras; i++)
	{
		// Select camera
		pCam = camList.GetByIndex(i);
		cout << endl << "Running example for camera " << i << "..." << endl;

		result = result | RunSingleCamera(pCam);

		cout << "Camera " << i << " example complete..." << endl << endl;
	}

	// Release reference to the camera
	pCam = nullptr;

	// Clear camera list before releasing system
	camList.Clear();

	// Release system
	system->ReleaseInstance();

	cout << endl << "Done! Press Enter to exit..." << endl;
	getchar();

	return result;
}
