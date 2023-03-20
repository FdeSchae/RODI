//========================================================================================================================================
// RODI - Riverine Organism Drift Imager (Recording script) 
// This script was developped by Frédéric de Schaetzen, ETH Zürich for the following publication:
//
//
//
// This script was written based on examples provided by FLIR systems (these are included in their SPINNAKER SDK) 
// and SyncFlir developped by Guillermo Hidalgo Gadea (MIT License Copyright (c) 2021 GuillermoHidalgoGadea.com - 
// Sourcecode: https://gitlab.ruhr-uni-bochum.de/ikn/syncflir).
//
// script parameters are updated using the myconfig.txt file. 
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

//define namespaces
using namespace std::chrono;
using namespace Spinnaker;
using namespace Spinnaker::GenApi;
using namespace Spinnaker::GenICam;
using namespace std;

// Initialize config parameters, will be updated by config file
std::string outpath;
std::string myconfig;
double exposureTime; // exposure time in microseconds (i.e., 1/ max FPS)
double dGain;
double FPS; // frames per second in Hz
int numBuffers; // depending on RAM
int numFrames; //needs to be at least 100
int totalfiles;

// Initialize placeholders
vector<ofstream> cameraFiles;
ofstream csvFile;
string serialNumber;

/*
========================================================================================================================================
readconfig() opens the myconfig.txt file and updates script parameters
========================================================================================================================================
*/
int readconfig(string myconfig)
{
	int result = 0;
	std::ifstream cFile(myconfig);
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
			//Parameter retrieval from myconfig.txt file
			if (name == "FPS") FPS = std::stod(value);
			else if (name == "exposureTime") exposureTime = std::stod(value);
			else if (name == "dGain") dGain = std::stod(value);
			else if (name == "numBuffers") numBuffers = std::stoi(value);
			else if (name == "numFrames") numFrames = std::stoi(value); 
			else if (name == "totalfiles") totalfiles = std::stoi(value); 
		}
	}
	else
	{
		std::cerr << "Failure: myconfig.txt could not be opened. Press enter to exit.";
		getchar();
		return -1;
	}
	cout << endl << "FPS=" << FPS << " fps" << endl;
	cout << "exposureTime=" << exposureTime << " microseconds" << endl;
	cout << "dGain=" << dGain << " dB" << endl;
	cout << "numBuffers=" << numBuffers << endl;
	cout << "numFrames=" << numFrames << endl;
	cout << "totalfiles=" << totalfiles << endl;
	return result,  FPS, exposureTime, dGain, numBuffers, numFrames, totalfiles;
}

/*
========================================================================================================================================
Helper functions: DateTime, removeSpaces, TimeStamp, CreateTMP and CreateCSV.
========================================================================================================================================
*/
string removeSpaces(string word) // removes spaces in string
{
	string newWord;
	for (int i = 0; i < word.length(); i++) {
		if (word[i] != ' ') {
			newWord += word[i];
		}
	}
	return newWord;
}

string DateTime() //retrieves date and time in the following format: DDMMYYYY_HHMMSS
{
	stringstream DateTime;
	// current date and time based on system
	time_t ttNow = time(0);
	tm* ptmNow;
	ptmNow = localtime(&ttNow);
	// days
	if (ptmNow->tm_mday < 10) //Fill in the leading 0 if less than 10
		DateTime << "0" << ptmNow->tm_mday << " ";
	else
		DateTime << ptmNow->tm_mday << " ";
	// months
	if (ptmNow->tm_mon < 10) //Fill in the leading 0 if less than 10
		DateTime << "0" << 1 + ptmNow->tm_mon;
	else
		DateTime << (1 + ptmNow->tm_mon);
	// years
	DateTime << 1900 + ptmNow->tm_year;
	DateTime << "_"; // underscore spacer
	//hours
	if (ptmNow->tm_hour < 10) //Fill in the leading 0 if less than 10
		DateTime << "0" << ptmNow->tm_hour;
	else
		DateTime << ptmNow->tm_hour;
	// minutes
	if (ptmNow->tm_min < 10) //Fill in the leading 0 if less than 10
		DateTime << "0" << ptmNow->tm_min;
	else
		DateTime << ptmNow->tm_min;
	// seconds
	if (ptmNow->tm_sec < 10) //Fill in the leading 0 if less than 10
		DateTime << "0" << ptmNow->tm_sec;
	else
		DateTime << ptmNow->tm_sec;
	string dt = DateTime.str();
	dt = removeSpaces(dt);
	return dt;
}

string FileNr(int fnr) // generates filenr, adding zero to numbers smaller than a certain threshold
{
	stringstream fnrss;
	if (totalfiles < 10)
		fnrss << fnr;
	else if (totalfiles < 99)
		if (fnr < 10) fnrss << "0" << fnr;
		else fnrss << fnr;
	else if (totalfiles < 999)
		if (fnr < 10) fnrss << "00" << fnr;
		else if (fnr < 100) fnrss << "0" << fnr;
		else fnrss << fnr;
	else if (totalfiles < 9999)
		if (fnr < 10) fnrss << "000" << fnr;
		else if (fnr < 100) fnrss << "00" << fnr;
		else if (fnr < 1000) fnrss << "00" << fnr;
		else fnrss << fnr;
		string nr = fnrss.str();
		return nr;
}

auto TimeStamp() // retrieves timestamp of image in nanoseconds
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


int CreateTMP(string serialNumber, int fnr) // creates a .tmp file to store frames in binary format
{
	int result = 0;
	stringstream sstream_tmpFilename;
	string tmpFilename;
	ofstream filename; // TODO: still needed?
	// Create temporary file from serialnr and filenumber
	sstream_tmpFilename << outpath << "/" << serialNumber << "_file" << FileNr(fnr) << ".tmp";
	sstream_tmpFilename >> tmpFilename;
	//cout << "File " << tmpFilename << " initialized" << endl;
	cameraFiles.push_back(std::move(filename)); // TODO: still needed?
	cameraFiles[fnr].open(tmpFilename.c_str(), ios_base::out | ios_base::binary);
	return result;
}

int CreateCSV(string SerialNumber) // creates a .csv log file that keeps track of all frames
{
	int result = 0;
	stringstream sstream_csvFile;
	string csvFilename;
	// Create .csv logfile
	sstream_csvFile << outpath << "/" << serialNumber << "logfile_" << DateTime() << ".csv";
	sstream_csvFile >> csvFilename;
	csvFile.open(csvFilename);
	csvFile << "FrameID" << "," << "Timestamp" << "," << "SerialNumber" << "," << "FileNumber" << "," << "SystemTimeInNanoseconds" << endl;
	return result;
}

/*
========================================================================================================================================
ConfigureFramerate sets the desired Framerate for the camera.
========================================================================================================================================
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
			// Set Frame Rate Auto to Off 
			CEnumerationPtr ptrFrameRateAuto = pCam->GetNodeMap().GetNode("AcquisitionFrameRateAuto");
			if (!IsAvailable(ptrFrameRateAuto) || !IsWritable(ptrFrameRateAuto))
			{
				cout << "Failure: Unable to set FrameRateAuto." << endl;
				cout << "Press enter to exit." << endl << endl;
				getchar();
				return -1;
			}
			// Retrieve entry node from enumeration node
			CEnumEntryPtr ptrFrameRateAutoOff = ptrFrameRateAuto->GetEntryByName("Off");
			if (!IsAvailable(ptrFrameRateAutoOff) || !IsReadable(ptrFrameRateAutoOff))
			{
				cout << "Failure: Unable to set Frame Rate to Off." << endl;
				cout << "Press enter to exit." << endl << endl;
				getchar();
				return -1;
			}
			int64_t framerateAutoOff = ptrFrameRateAutoOff->GetValue();

			ptrFrameRateAuto->SetIntValue(framerateAutoOff);
			// Chameleon3 node is called Acquisition Frame Rate Control Enabled
			CBooleanPtr ptrAcquisitionFrameRateControlEnabled = pCam->GetNodeMap().GetNode("AcquisitionFrameRateEnabled");
			if (!IsAvailable(ptrAcquisitionFrameRateControlEnabled) || !IsWritable(ptrAcquisitionFrameRateControlEnabled))
			{
				cout << "Failure: Unable to set AcquisitionFrameRateControlEnabled." << endl;
				cout << "Press enter to exit." << endl << endl;
				getchar();
				return -1;
			}
			ptrAcquisitionFrameRateControlEnabled->SetValue(true);
		}
		else
		{
			ptrAcquisitionFrameRateEnable->SetValue(true);
		}

		// Set Maximum Acquisition FrameRate
		CFloatPtr ptrAcquisitionFrameRate = pCam->GetNodeMap().GetNode("AcquisitionFrameRate");
		if (!IsAvailable(ptrAcquisitionFrameRate) || !IsReadable(ptrAcquisitionFrameRate))
		{
			cout << "Failure: Unable to get node AcquisitionFrameRate." << endl;
			cout << "Press enter to exit." << endl << endl;
			getchar();
			return -1;
		}
		ptrAcquisitionFrameRate->SetValue(ptrAcquisitionFrameRate->GetMax());

		// Desired frame rate from config 
		double FPSToSet = FPS;
		// Maximum Acquisition Frame Rate
		double testAcqFrameRate = ptrAcquisitionFrameRate->GetValue();
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
		cout << "Failure: " << e.what() << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	return result;
}

/*
========================================================================================================================================
BufferHandlingSettings sets manual buffer handling mode to numBuffers set above.
========================================================================================================================================
*/
int BufferHandlingSettings(CameraPtr pCam)
{
	int result = 0;
	// Retrieve Stream Parameters device nodemap
	Spinnaker::GenApi::INodeMap& sNodeMap = pCam->GetTLStreamNodeMap();
	// Retrieve Buffer Handling Mode Information
	CEnumerationPtr ptrHandlingMode = sNodeMap.GetNode("StreamBufferHandlingMode");
	if (!IsAvailable(ptrHandlingMode) || !IsWritable(ptrHandlingMode))
	{
		cout << "Failure: Unable to set Buffer Handling mode (node retrieval)." << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	CEnumEntryPtr ptrHandlingModeEntry = ptrHandlingMode->GetCurrentEntry();
	if (!IsAvailable(ptrHandlingModeEntry) || !IsReadable(ptrHandlingModeEntry))
	{
		cout << "Failure: Unable to set Buffer Handling mode (Entry retrieval)." << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	// Set stream buffer Count Mode to manual
	CEnumerationPtr ptrStreamBufferCountMode = sNodeMap.GetNode("StreamBufferCountMode");
	if (!IsAvailable(ptrStreamBufferCountMode) || !IsWritable(ptrStreamBufferCountMode))
	{
		cout << "Failure: Unable to set Buffer Count Mode (node retrieval)." << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
		CEnumEntryPtr ptrStreamBufferCountModeManual = ptrStreamBufferCountMode->GetEntryByName("Manual");
	if (!IsAvailable(ptrStreamBufferCountModeManual) || !IsReadable(ptrStreamBufferCountModeManual))
	{
		cout << "Failure: Unable to set Buffer Count Mode entry (Entry retrieval)." << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	ptrStreamBufferCountMode->SetIntValue(ptrStreamBufferCountModeManual->GetValue());
	// Retrieve and modify Stream Buffer Count
	CIntegerPtr ptrBufferCount = sNodeMap.GetNode("StreamBufferCountManual");
	if (!IsAvailable(ptrBufferCount) || !IsWritable(ptrBufferCount))
	{
		cout << "Failure: Unable to set Buffer Count (Integer node retrieval)." << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}

	// Display Buffer Info
	if (ptrBufferCount->GetMax() < numBuffers)
	{
		ptrBufferCount->SetValue(ptrBufferCount->GetMax());
	}
	else
	{
		ptrBufferCount->SetValue(numBuffers);
	}
	ptrHandlingModeEntry = ptrHandlingMode->GetEntryByName("OldestFirst");
	ptrHandlingMode->SetIntValue(ptrHandlingModeEntry->GetValue());
	return result;
}

/*
========================================================================================================================================
ConfigureExposure sets the Exposure setting for the cameras.
========================================================================================================================================
*/
int ConfigureExposure(CameraPtr pCam)
{
	int result = 0;
	try
	{
		// Turn off automatic exposure mode
		CEnumerationPtr ptrExposureAuto = pCam->GetNodeMap().GetNode("ExposureAuto");
		if (!IsAvailable(ptrExposureAuto) || !IsWritable(ptrExposureAuto))
		{
			cout << "Failure: Unable to disable automatic exposure." << endl;
			cout << "Press enter to exit." << endl << endl;
			getchar();
			return -1;
		}
		CEnumEntryPtr ptrExposureAutoOff = ptrExposureAuto->GetEntryByName("Off");
		if (!IsAvailable(ptrExposureAutoOff) || !IsReadable(ptrExposureAutoOff))
		{
			cout << "Failure: Unable to disable automatic exposure." << endl;
			cout << "Press enter to exit." << endl << endl;
			getchar();
			return -1;
		}
		ptrExposureAuto->SetIntValue(ptrExposureAutoOff->GetValue());

		// Set exposure time manually; exposure time recorded in microseconds
		CFloatPtr ptrExposureTime = pCam->GetNodeMap().GetNode("ExposureTime");
		if (!IsAvailable(ptrExposureTime) || !IsWritable(ptrExposureTime))
		{
			cout << "Failure: Unable to set exposure time." << endl;
			cout << "Press enter to exit." << endl << endl;
			getchar();
			return -1;
		}
		// Desired exposure from config
		double exposureTimeToSet = exposureTime; // Exposure time will limit FPS by 1000000/exposure
		// Ensure desired exposure time does not exceed the maximum or minimum
		const double exposureTimeMax = ptrExposureTime->GetMax();
		const double exposureTimeMin = ptrExposureTime->GetMin();

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
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Failure: " << e.what() << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	return result;
}

/*
========================================================================================================================================
ConfigureGain sets the digital gain setting for the cameras. 
========================================================================================================================================
*/
int ConfigureGain(CameraPtr pCam)
{
	int result = 0;
	try
	{
		// Turn off automatic exposure mode
		CEnumerationPtr ptrGainAuto = pCam->GetNodeMap().GetNode("GainAuto");
		if (!IsAvailable(ptrGainAuto) || !IsWritable(ptrGainAuto))
		{
			cout << "Failure: Unable to disable automatic exposure." << endl;
			cout << "Press enter to exit." << endl << endl;
			getchar();
			return -1;
		}
		CEnumEntryPtr ptrGainAutoOff = ptrGainAuto->GetEntryByName("Off");
		if (!IsAvailable(ptrGainAutoOff) || !IsReadable(ptrGainAutoOff))
		{
			cout << "Failure: Unable to disable automatic gain." << endl;
			cout << "Press enter to exit." << endl << endl;
			getchar();
			return -1;
		}
		ptrGainAuto->SetIntValue(ptrGainAutoOff->GetValue());
		// Set exposure time manually; exposure time recorded in microseconds
		CFloatPtr ptrGain = pCam->GetNodeMap().GetNode("Gain");
		if (!IsAvailable(ptrGain) || !IsWritable(ptrGain))
		{
			cout << "Failure: Unable to set Gain." << endl;
			cout << "Press enter to exit." << endl << endl;
			getchar();
			return -1;
		}
		// Desired exposure from config
		double GainToSet = dGain;
		ptrGain->SetValue(GainToSet);
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Failure: " << e.what() << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	return result;
}

/*
========================================================================================================================================
CleanBuffer runs on camera prior to image acquisition.
========================================================================================================================================
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
		cout << "Failure: " << e.what() << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	pCam->EndAcquisition();
	return result;
}
/*
========================================================================================================================================
AcquireImages will retrieve images from the camera and write them into the temporary file.
========================================================================================================================================
*/
int AcquireImages(CameraPtr pCam, INodeMap& nodeMap, INodeMap& nodeMapTLDevice)
{
	int result = 0;
	cout << endl << "--- Acquiring images ---" << endl << endl;
	try
	{
		// Create the .csv log file
		CreateCSV(serialNumber); 
		// Begin acquiring images
		pCam->BeginAcquisition();
		// first image is discarded
		ImagePtr pResultImage = pCam->GetNextImage();
		char* imageData = static_cast<char*>(pResultImage->GetData());
		pResultImage->Release();
		int stopwait = 0;
		for (unsigned int fnr = 0; fnr < totalfiles; fnr++)
		{
			CreateTMP(serialNumber, fnr);
			cout << "	++ saving " << numFrames << " frames to file " << fnr << "/" << totalfiles << " ++" << endl;
			const unsigned int k_numFrames = numFrames;
			for (unsigned int FrameCnt = 0; FrameCnt < k_numFrames; FrameCnt++)
			{
				try
				{
					// Retrieve image and ensure image completion
					try
					{
						pResultImage = pCam->GetNextImage(1000); // timeout for NextImage in miliseconds
					}
					catch (Spinnaker::Exception& e)
					{
						cout << "Failure: " << e.what() << endl;
						cout << "Press enter to exit." << endl << endl;
						getchar();
						return -1;
					}
					// Retrieve imageData
					imageData = static_cast<char*>(pResultImage->GetData());
					// write frame to respective cameraFile
					cameraFiles[fnr].write(imageData, pResultImage->GetImageSize());
					csvFile << pResultImage->GetFrameID() << "," << pResultImage->GetTimeStamp() << "," << serialNumber << "," << FileNr(fnr) << endl;
					// Check if the writing is successful
					if (!cameraFiles[fnr].good())
					{
						cout << "Error writing to file for camera!" << endl;
						cout << "Press enter to exit." << endl << endl;
						getchar();
						return -1;
					}
					pResultImage->Release();
				}
				catch (Spinnaker::Exception& e)
				{
					cout << "Failure: " << e.what() << endl;
					cout << "Press enter to exit." << endl << endl;
					getchar();
					return -1;
				}
			}
			cameraFiles[fnr].close();

	}
	pCam->EndAcquisition(); //Ending acquisition appropriately helps ensure that devices clean up properly and do not need to be power-cycled to maintain integrity.
	csvFile.close();
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Failure: " << e.what() << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	return result;
}


/*
========================================================================================================================================
InitializeCamera will setup will change camera parameters based on the myconfig.txt prior to image acquisition.
========================================================================================================================================
*/
int InitializeCamera(CameraPtr pCam, string serialNumber)
{
	int result = 0;
	try
	{
		BufferHandlingSettings(pCam); // Set Buffer
		ConfigureFramerate(pCam); // Set Framerate
		ConfigureExposure(pCam); // Set Exposure
		ConfigureGain(pCam); // SetGain
		CleanBuffer(pCam); // Clean buffer
	}
	catch (Spinnaker::Exception& e)
	{
		cout << "Failure: " << e.what() << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		return -1;
	}
	cout << endl << "--- Camera parameters have been set! ---" << endl;
	return result;
}

int RunCamera(CameraPtr pCam)
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
		}
		// Set DeviceUserID to loop counter to assign camera order to cameraFiles in parallel threads
		CStringPtr ptrDeviceUserId = nodeMap.GetNode("DeviceUserID");
		if (!IsAvailable(ptrDeviceUserId) || !IsWritable(ptrDeviceUserId))
		{
			cout << "Failure: Unable to get node ptrDeviceUserId. Press enter to exit." << endl << endl;
			getchar();
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
		cout << "Failure: " << e.what() << endl;
		cout << "Press enter to exit." << endl << endl;
		getchar();
		result = -1;
	}
	return result;
}

/*
========================================================================================================================================
Main function of the script. In here the output folder is defined, myconfig.txt is loaded and the camera started for image acquisition.
========================================================================================================================================
*/
int main(int /*argc*/, char** /*argv*/)
{
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
	//Specify an output folder (outpath), and test its writing permissions, in which recorded files will be saved.
	cout << endl << "Specifiy folder to which you would like to save recorded files C:\\RODI): " << endl;
	getline(cin, outpath);
	// check the writing permissions of the specified output folder
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
	// Retrieve singleton reference to system object
	SystemPtr system = System::GetInstance();
	// Print out current library version
	const LibraryVersion spinnakerLibraryVersion = system->GetLibraryVersion();
	cout << "Spinnaker library version: " << spinnakerLibraryVersion.major << "." << spinnakerLibraryVersion.minor
		<< "." << spinnakerLibraryVersion.type << "." << spinnakerLibraryVersion.build << endl
		<< endl;
	// Checl the system for attached cameras
	CameraList camList = system->GetCameras();
	const unsigned int numCameras = camList.GetSize();
	cout << "--- Number of cameras detected: " << numCameras << " ---" << endl << endl;
	// Exit if no camera's are present
	if (numCameras == 0)
	{
		// Clear camera list before releasing system
		camList.Clear();
		// Release system
		system->ReleaseInstance();
		cout << "No camera's were found!" << endl;
		cout << "Press enter to exit." << endl;
		getchar();
		return -1;
	}
	// Read config file and update parameters
	cout << "Provide the path to the myconfig.txt file (path format example: C:\\RODI\\myconfig.txt) " << endl;
	getline(cin, myconfig);
	cout << endl << "--- Importing parameters from " + myconfig + " ---";
	readconfig(myconfig);
	cout << "	Complete!" << endl << endl;
	// Create shared pointer to camera
	CameraPtr pCam = nullptr;
	int result = 0;
	// Run the script for each camera
	for (unsigned int i = 0; i < numCameras; i++)
	{
		// Select camera
		pCam = camList.GetByIndex(i);
		cout << endl << "--- Starting camera " << i << " ---" << endl;
		result = result | RunCamera(pCam);
		cout << endl << "--- Recording complete! ---" << endl << endl;
	}
	// Release reference to the camera
	pCam = nullptr;
	// Clear camera list before releasing system
	camList.Clear();
	// Release system
	system->ReleaseInstance();
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
	cout << endl << "All recordings are complete! Press enter to exit." << endl;
	getchar();
	return result;
}
