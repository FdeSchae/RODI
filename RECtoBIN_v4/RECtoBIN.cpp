//=============================================================================
// RODI - Riverine Organism Drift Imager (Image acquisition script) 
//
// This script was written based on examples provided by FLIR systems (these are included in their SPINNAKER SDK) 
// and SyncFlir developped by Guillermo Hidalgo Gadea (MIT License Copyright (c) 2021 GuillermoHidalgoGadea.com - 
// Sourcecode: https://github.com/Guillermo-Hidalgo-Gadea/syncFLIR).
//
// script parameters are updated using the myconfig.txt file. 
//
// TODO:
// - empty image removal
//		+ once a .tmp file is finished analyze every 10th frame for an object, if an object is found, determine the location (coordinates) 
//		  and amke a list of frame ID
// 
// 
// 
//FLIR MAKES NO REPRESENTATIONS OR WARRANTIES ABOUT THE SUITABILITY OF THE
// SOFTWARE, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE, OR NON-INFRINGEMENT. FLIR SHALL NOT BE LIABLE FOR ANY DAMAGES
// SUFFERED BY LICENSEE AS A RESULT OF USING, MODIFYING OR DISTRIBUTING
// THIS SOFTWARE OR ITS DERIVATIVES.
//=============================================================================

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

#ifndef _WIN32
#include <pthread.h>
#endif

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
================
This function reads the config file to update camera parameters such as working directory, serial of primary camera and Framerate
================
*/
int readconfig()
{
	int result = 0;

	cout << "Reading config file... ";
	std::ifstream cFile("myConfig.txt");
	if (cFile.is_open())
	{

		std::string line;
		while (getline(cFile, line))
		{
			line.erase(std::remove_if(line.begin(), line.end(), isspace), line.end());
			if (line[0] == '#' || line.empty()) continue;

			auto delimiterPos = line.find("=");
			auto name = line.substr(0, delimiterPos);
			auto value = line.substr(delimiterPos + 1);

			//Custom coding
			if (name == "triggerCam") triggerCam = value;
			else if (name == "TMPpath") TMPpath = value; //path to the internal laptop harddrive
			else if (name == "HDDpath") HDDpath = value; //path to the external harddrive
			else if (name == "FPS") FPS = std::stod(value);
			else if (name == "exposureTime") exposureTime = std::stod(value);
			else if (name == "dGain") dGain = std::stod(value);
			else if (name == "numBuffers") numBuffers = std::stoi(value);
			else if (name == "numImages") numImages = std::stoi(value); //
			else if (name == "totalfiles") totalfiles = std::stoi(value); //this list can be expanded, just make sure the last line returns an int value
			else if (name == "NumBlocks") NumBlocks = std::stoi(value);
		}
	}
	else
	{
		std::cerr << " Couldn't open config file for reading.";
	}

	cout << " Done!" << endl;
	/*
	cout << "\ntriggerCam=" << triggerCam;
	std::cout << "\nFPS=" << FPS;
	std::cout << "\ncompression=" << compression;
	std::cout << "\nexposureTime=" << exposureTime;
	std::cout << "\nnumBuffers=" << numBuffers;
	std::cout << "\nPath=" << path << endl << endl;
	*/
	return result, triggerCam, TMPpath, HDDpath, FPS, exposureTime, dGain, numBuffers, numImages, totalfiles;
}

/*
=================
These helper functions getCurrentDateTime and removeSpaces get the system time and transform it to readable format. The output string is used as timestamp for new filenames. The function getTimeStamp prints system writing time in nanoseconds to the csv logfile.
=================
*/
string removeSpaces(string word)
{
	string newWord;
	for (int i = 0; i < word.length(); i++) {
		if (word[i] != ' ') {
			newWord += word[i];
		}
	}
	return newWord;
}

string getCurrentDateTime()
{
	stringstream currentDateTime;
	// current date/time based on system
	time_t ttNow = time(0);
	tm* ptmNow;
	// year
	ptmNow = localtime(&ttNow);
	currentDateTime << 1900 + ptmNow->tm_year;
	//month
	if (ptmNow->tm_mon < 10) //Fill in the leading 0 if less than 10
		currentDateTime << "0" << 1 + ptmNow->tm_mon;
	else
		currentDateTime << (1 + ptmNow->tm_mon);
	//day
	if (ptmNow->tm_mday < 10) //Fill in the leading 0 if less than 10
		currentDateTime << "0" << ptmNow->tm_mday << " ";
	else
		currentDateTime << ptmNow->tm_mday << " ";
	//spacer
	currentDateTime << "_";
	//hour
	if (ptmNow->tm_hour < 10) //Fill in the leading 0 if less than 10
		currentDateTime << "0" << ptmNow->tm_hour;
	else
		currentDateTime << ptmNow->tm_hour;
	//min
	if (ptmNow->tm_min < 10) //Fill in the leading 0 if less than 10
		currentDateTime << "0" << ptmNow->tm_min;
	else
		currentDateTime << ptmNow->tm_min;
	//sec
	if (ptmNow->tm_sec < 10) //Fill in the leading 0 if less than 10
		currentDateTime << "0" << ptmNow->tm_sec;
	else
		currentDateTime << ptmNow->tm_sec;
	string test = currentDateTime.str();

	test = removeSpaces(test);

	return test;
}

auto getTimeStamp()
{
	std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();

	typedef std::chrono::duration<int, std::ratio_multiply<std::chrono::hours::period, std::ratio<8>>::type> Days;

	Days days = std::chrono::duration_cast<Days>(duration);
	duration -= days;

	auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);

	auto timestamp = nanoseconds.count();
	return timestamp;
}


int CreateTMP(string serialNumber, int cameraCnt, int fnr)
{
	int result = 0;
	//cout << endl << "CREATING FILES:..." << endl;

	stringstream sstream_tmpFilename;
	string tmpFilename;
	
	ofstream filename; // TODO: still needed?
	const string csDestinationDirectory = TMPpath;

	// Create temporary file from serialnum assigned to cameraCnt
	sstream_tmpFilename << csDestinationDirectory <<  serialNumber << "_file" << fnr << ".tmp";
	sstream_tmpFilename >> tmpFilename;

	//cout << "File " << tmpFilename << " initialized" << endl;

	cameraFiles.push_back(std::move(filename)); // TODO: still needed?
	cameraFiles[fnr].open(tmpFilename.c_str(), ios_base::out | ios_base::binary);

	return result;
}

int CreateCSV(string SerialNumber)
{
	int result = 0;
	stringstream sstream_csvFile;
	stringstream sstream_metadataFile;
	string csvFilename;

	ofstream filename; // TODO: still needed?
	const string csDestinationDirectory = TMPpath;

	// Create .csv logfile and .txt metadata only once for all cameras during first loop
	if (cameraCnt == 0)
	{
		// create csv logfile with headers
		sstream_csvFile << csDestinationDirectory << serialNumber << "logfile_" << getCurrentDateTime() << ".csv";
		sstream_csvFile >> csvFilename;

		//cout << "CSV file: " << csvFilename << " initialized" << endl;

		csvFile.open(csvFilename);
		csvFile << "FrameID" << "," << "Timestamp" << "," << "SerialNumber" << "," << "FileNumber" << "," << "SystemTimeInNanoseconds" << endl;

		// create txt metadata
		sstream_metadataFile << csDestinationDirectory << "metadata_" << getCurrentDateTime() << ".txt";
		sstream_metadataFile >> metadataFilename;

	}
	return result;
}

/*
=================
The function ConfigureFramerate sets the desired Framerate for the trigger camera.
=================
*/
int ConfigureFramerate(CameraPtr pCam)
{
	int result = 0;
	//cout << endl << "CONFIGURING FRAMERATE:..." << endl;

	try
	{
		//Enable Aquisition Frame Rate 
		CBooleanPtr ptrAcquisitionFrameRateEnable = pCam->GetNodeMap().GetNode("AcquisitionFrameRateEnable");
		if (!IsAvailable(ptrAcquisitionFrameRateEnable) || !IsWritable(ptrAcquisitionFrameRateEnable))
		{
			// Trying Chameleon3 settings
			// Set Frame Rate Auto to Off 
			CEnumerationPtr ptrFrameRateAuto = pCam->GetNodeMap().GetNode("AcquisitionFrameRateAuto");
			if (!IsAvailable(ptrFrameRateAuto) || !IsWritable(ptrFrameRateAuto))
			{
				cout << "Unable to set FrameRateAuto. Aborting..." << endl << endl;
				return -1;
			}
			// Retrieve entry node from enumeration node
			CEnumEntryPtr ptrFrameRateAutoOff = ptrFrameRateAuto->GetEntryByName("Off");
			if (!IsAvailable(ptrFrameRateAutoOff) || !IsReadable(ptrFrameRateAutoOff))
			{
				cout << "Unable to set Frame Rate to Off. Aborting..." << endl << endl;
				return -1;
			}
			int64_t framerateAutoOff = ptrFrameRateAutoOff->GetValue();

			ptrFrameRateAuto->SetIntValue(framerateAutoOff);
			//cout << "Frame Rate Auto set to Off..." << endl;

			// Chameleon3 node is called Acquisition Frame Rate Control Enabled
			CBooleanPtr ptrAcquisitionFrameRateControlEnabled = pCam->GetNodeMap().GetNode("AcquisitionFrameRateEnabled");
			if (!IsAvailable(ptrAcquisitionFrameRateControlEnabled) || !IsWritable(ptrAcquisitionFrameRateControlEnabled))
			{
				cout << "Unable to set AcquisitionFrameRateControlEnabled. Aborting..." << endl << endl;
				return -1;
			}
			ptrAcquisitionFrameRateControlEnabled->SetValue(true);
			//cout << "Acquisition Frame Rate Control Enabled" << endl;
		}
		else
		{
			ptrAcquisitionFrameRateEnable->SetValue(true);
			//cout << "Acquisition Frame Rate Enabled" << endl;
		}

		// Set Maximum Acquisition FrameRate
		CFloatPtr ptrAcquisitionFrameRate = pCam->GetNodeMap().GetNode("AcquisitionFrameRate");
		if (!IsAvailable(ptrAcquisitionFrameRate) || !IsReadable(ptrAcquisitionFrameRate))
		{
			cout << "Unable to get node AcquisitionFrameRate. Aborting..." << endl << endl;
			return -1;
		}
		ptrAcquisitionFrameRate->SetValue(ptrAcquisitionFrameRate->GetMax());

		// Desired frame rate from config 
		double FPSToSet = FPS;

		// Maximum Acquisition Frame Rate
		double testAcqFrameRate = ptrAcquisitionFrameRate->GetValue();
		//cout << "Maximum Acquisition Frame Rate is  : " << ptrAcquisitionFrameRate->GetMax() << endl;

		// Maximum Resulting Frame Rate bounded by 1/Exposure
		double testResultingAcqFrameRate = 1000000 / exposureTime; // exposure in microseconds (10^-6)

		// If desired FPS too high, find next best available
		if (FPSToSet > testResultingAcqFrameRate || FPSToSet > testAcqFrameRate)
		{
			// take the lowest bounded frame rate
			if (testResultingAcqFrameRate > testAcqFrameRate)
			{
				FPSToSet = testAcqFrameRate;
			}
			else
			{
				FPSToSet = testResultingAcqFrameRate;
			}
		}
		// if FPS not too high, go ahead
		ptrAcquisitionFrameRate->SetValue(FPSToSet);
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Error: " << e.what() << endl;
		result = -1;
	}
	return result;
}

/*
=================
The function BufferHandlingSettings sets manual buffer handling mode to numBuffers set above.
=================
*/
int BufferHandlingSettings(CameraPtr pCam)
{
	int result = 0;
	//cout << endl << "CONFIGURING BUFFER..." << endl;

	// Retrieve Stream Parameters device nodemap
	Spinnaker::GenApi::INodeMap& sNodeMap = pCam->GetTLStreamNodeMap();

	// Retrieve Buffer Handling Mode Information
	CEnumerationPtr ptrHandlingMode = sNodeMap.GetNode("StreamBufferHandlingMode");
	if (!IsAvailable(ptrHandlingMode) || !IsWritable(ptrHandlingMode))
	{
		cout << "Unable to set Buffer Handling mode (node retrieval). Aborting..." << endl << endl;
		return -1;
	}

	CEnumEntryPtr ptrHandlingModeEntry = ptrHandlingMode->GetCurrentEntry();
	if (!IsAvailable(ptrHandlingModeEntry) || !IsReadable(ptrHandlingModeEntry))
	{
		cout << "Unable to set Buffer Handling mode (Entry retrieval). Aborting..." << endl << endl;
		return -1;
	}

	// Set stream buffer Count Mode to manual
	CEnumerationPtr ptrStreamBufferCountMode = sNodeMap.GetNode("StreamBufferCountMode");
	if (!IsAvailable(ptrStreamBufferCountMode) || !IsWritable(ptrStreamBufferCountMode))
	{
		cout << "Unable to set Buffer Count Mode (node retrieval). Aborting..." << endl << endl;
		return -1;
	}

	CEnumEntryPtr ptrStreamBufferCountModeManual = ptrStreamBufferCountMode->GetEntryByName("Manual");
	if (!IsAvailable(ptrStreamBufferCountModeManual) || !IsReadable(ptrStreamBufferCountModeManual))
	{
		cout << "Unable to set Buffer Count Mode entry (Entry retrieval). Aborting..." << endl << endl;
		return -1;
	}
	ptrStreamBufferCountMode->SetIntValue(ptrStreamBufferCountModeManual->GetValue());

	// Retrieve and modify Stream Buffer Count
	CIntegerPtr ptrBufferCount = sNodeMap.GetNode("StreamBufferCountManual");
	if (!IsAvailable(ptrBufferCount) || !IsWritable(ptrBufferCount))
	{
		cout << "Unable to set Buffer Count (Integer node retrieval). Aborting..." << endl << endl;
		return -1;
	}

	// Display Buffer Info
	//cout << "Stream Buffer Count Mode set to manual" << endl;
	//cout << "Default Buffer Count: " << ptrBufferCount->GetValue() << endl;
	//cout << "Maximum Buffer Count: " << ptrBufferCount->GetMax() << endl;
	if (ptrBufferCount->GetMax() < numBuffers)
	{
		ptrBufferCount->SetValue(ptrBufferCount->GetMax());
	}
	else
	{
		ptrBufferCount->SetValue(numBuffers);
	}

	//cout << "Manual Buffer Count: " << ptrBufferCount->GetValue() << endl;
	ptrHandlingModeEntry = ptrHandlingMode->GetEntryByName("OldestFirst");
	ptrHandlingMode->SetIntValue(ptrHandlingModeEntry->GetValue());
	//cout << "Buffer Handling Mode has been set to " << ptrHandlingModeEntry->GetDisplayName() << endl;

	return result;
}

/*
=================
The function ConfigureExposure sets the Exposure setting for the cameras. The Exposure time will affect the overall brightness of the image, but also the framerate. Note that *exposureTime* is set from outside the function. To reach a Frame Rate of 200fps each frame cannot take longer than 1/200 = 5ms or 5000 microseconds.
=================
*/
int ConfigureExposure(CameraPtr pCam)
{
	int result = 0;
	//cout << endl << "CONFIGURING EXPOSURE:..." << endl;

	try
	{
		// Turn off automatic exposure mode
		CEnumerationPtr ptrExposureAuto = pCam->GetNodeMap().GetNode("ExposureAuto");
		if (!IsAvailable(ptrExposureAuto) || !IsWritable(ptrExposureAuto))
		{
			cout << "Unable to disable automatic exposure. Aborting..." << endl << endl;
			return -1;
		}
		CEnumEntryPtr ptrExposureAutoOff = ptrExposureAuto->GetEntryByName("Off");
		if (!IsAvailable(ptrExposureAutoOff) || !IsReadable(ptrExposureAutoOff))
		{
			cout << "Unable to disable automatic exposure. Aborting..." << endl << endl;
			return -1;
		}
		ptrExposureAuto->SetIntValue(ptrExposureAutoOff->GetValue());

		// Set exposure time manually; exposure time recorded in microseconds
		CFloatPtr ptrExposureTime = pCam->GetNodeMap().GetNode("ExposureTime");
		if (!IsAvailable(ptrExposureTime) || !IsWritable(ptrExposureTime))
		{
			cout << "Unable to set exposure time. Aborting..." << endl << endl;
			return -1;
		}

		// Desired exposure from config
		double exposureTimeToSet = exposureTime; // Exposure time will limit FPS by 1000000/exposure

												 // Ensure desired exposure time does not exceed the maximum or minimum
		const double exposureTimeMax = ptrExposureTime->GetMax();
		const double exposureTimeMin = ptrExposureTime->GetMin();
		//cout << "exposureTimeMax: " << exposureTimeMax << endl;

		if (exposureTimeToSet > exposureTimeMax || exposureTimeToSet < exposureTimeMin)
		{
			if (exposureTimeToSet > exposureTimeMax)
			{
				exposureTimeToSet = exposureTimeMax;
			}
			else
			{
				exposureTimeToSet = exposureTimeMin;
			}
		}
		ptrExposureTime->SetValue(exposureTimeToSet);
		//cout << std::fixed << "Exposure time set to " << exposureTimeToSet << " microseconds, resulting in " << 1000000 / exposureTimeToSet << "Hz" << endl;
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Error: " << e.what() << endl;
		result = -1;
	}
	return result;
}

/*
=================
The function ConfigureGain sets the digital gain setting for the cameras. The digital gain time will affect will increase the pixel values recorded by the sensor with a factor.
=================
*/
int ConfigureGain(CameraPtr pCam)
{
	int result = 0;
	//cout << endl << "CONFIGURING EXPOSURE:..." << endl;

	try
	{
		// Turn off automatic exposure mode
		CEnumerationPtr ptrGainAuto = pCam->GetNodeMap().GetNode("GainAuto");
		if (!IsAvailable(ptrGainAuto) || !IsWritable(ptrGainAuto))
		{
			cout << "Unable to disable automatic exposure. Aborting..." << endl << endl;
			return -1;
		}
		CEnumEntryPtr ptrGainAutoOff = ptrGainAuto->GetEntryByName("Off");
		if (!IsAvailable(ptrGainAutoOff) || !IsReadable(ptrGainAutoOff))
		{
			cout << "Unable to disable automatic gain. Aborting..." << endl << endl;
			return -1;
		}
		ptrGainAuto->SetIntValue(ptrGainAutoOff->GetValue());

		// Set exposure time manually; exposure time recorded in microseconds
		CFloatPtr ptrGain = pCam->GetNodeMap().GetNode("Gain");
		if (!IsAvailable(ptrGain) || !IsWritable(ptrGain))
		{
			cout << "Unable to set Gain. Aborting..." << endl << endl;
			return -1;
		}

		// Desired exposure from config
		double GainToSet = dGain;

		ptrGain->SetValue(GainToSet);
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Error: " << e.what() << endl;
		result = -1;
	}
	return result;
}

/*
=================
The function CleanBuffer runs on all cameras in hardware trigger mode before the actual recording. If the buffer contains images, these are released in the for loop. When the buffer is empty, GetNextImage will get timeout error and end the loop.
=================
*/
int CleanBuffer(CameraPtr pCam)
{
	int result = 0;

	// Clean Buffer acquiring idle images
	pCam->BeginAcquisition();

	try
	{
		for (unsigned int imagesInBuffer = 0; imagesInBuffer < numBuffers; imagesInBuffer++)
		{
			// first numBuffer images are descarted
			ImagePtr pResultImage = pCam->GetNextImage(100);
			char* imageData = static_cast<char*>(pResultImage->GetData());
			pResultImage->Release();
		}
	}
	catch (Spinnaker::Exception& e)
	{
		//cout << "Buffer clean. " << endl;
	}

	pCam->EndAcquisition();

	return result;
}
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
					if ( (imageCnt == mkf) && (tf < totalfiles) )
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


/*
=================
The function InitializeCamera will setup will change camera parameters based on the myconfig.txt prior to image acquisition
=================
*/
int InitializeCamera(CameraPtr pCam, string serialNumber)
{
	int result;

	try
	{
		// Set Buffer
		BufferHandlingSettings(pCam);

		// Set Framerate
		ConfigureFramerate(pCam);

		// Set Exposure
		ConfigureExposure(pCam);

		// SetGain
		ConfigureGain(pCam);

		// Clean buffer
		CleanBuffer(pCam);

		// Create the .csv log file
		CreateCSV(serialNumber);

	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Error: " << e.what() << endl;
		result = -1;
	}

	return result;
}

int RunSingleCamera(CameraPtr pCam)
{
	int result;

	try
	{
		// Initialize camera
		pCam->Init();

		// Retrieve GenICam nodemap
		INodeMap& nodeMap = pCam->GetNodeMap();
		INodeMap& nodeMapTLDevice = pCam->GetTLDeviceNodeMap();
		CStringPtr ptrStringSerial = pCam->GetTLDeviceNodeMap().GetNode("DeviceSerialNumber");

		if (IsAvailable(ptrStringSerial) && IsReadable(ptrStringSerial))
		{
			serialNumber = ptrStringSerial->GetValue();
			//cout << "Device serial number retrieved as " << serialNumber << "..." << endl;
		}

		// Set DeviceUserID to loop counter to assign camera order to cameraFiles in parallel threads
		CStringPtr ptrDeviceUserId = nodeMap.GetNode("DeviceUserID");
		if (!IsAvailable(ptrDeviceUserId) || !IsWritable(ptrDeviceUserId))
		{
			cout << "Unable to get node ptrDeviceUserId. Aborting..." << endl << endl;
			return -1;
		}

		// camera setup based on myconfig.txt parameters
		result = result | InitializeCamera(pCam, serialNumber);

		// Acquire images
		result = result | AcquireImages(pCam, nodeMap, nodeMapTLDevice);

		// Deinitialize camera
		pCam->DeInit();
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Error: " << e.what() << endl;
		result = -1;
	}

	return result;
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
