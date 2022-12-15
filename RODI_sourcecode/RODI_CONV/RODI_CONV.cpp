/*
//========================================================================================================================================
// RODI - Riverine Organism Drift Imager (Conversion script)
// This script was developped by Frédéric de Schaetzen, ETH Zürich for the following publication:
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


// Initialize placeholder variables, these will be updated with by the readconfig() function or provided by the user upon starting the script.
string metadata;
string inpath;
string outpath;
std::vector<std::string> filenames;
double FPS;
int imageHeight;
int imageWidth;
std::string chosenVideoType;
int h264bitrate; // 1000000 - 16000000
int mjpgquality; //1-100
int maxVideoSize; // max file size in GB, 0 indicates no limit (not recommended).
int maxRAM; // max RAM in GB to store frames in working memory
/*
========================================================================================================================================
readconfig() opens the metadata.txt file and updates script parameters
========================================================================================================================================
*/
auto readconfig(string metadata)
{
	int result = 0;
	std::ifstream cFile(metadata);
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
			//Parameter retrieval from metadata.txt file
			if (name == "Framerate") FPS = std::stod(value);
			else if (name == "ImageHeight") imageHeight = std::stod(value);
			else if (name == "ImageWidth") imageWidth = std::stod(value);
			else if (name == "chosenVideoType") chosenVideoType = value;
			else if (name == "h264bitrate") h264bitrate = std::stod(value);
			else if (name == "mjpgquality") mjpgquality = std::stod(value);
			else if (name == "maxVideoSize") maxVideoSize = std::stod(value);
			else if (name == "maxRAM") maxRAM = std::stod(value);
		}
	}
	else
	{
		std::cerr << "Failure to open config-file. Press enter to exit.";
		getchar();
		return -1;
	}
	cout << endl << "Framerate=" << FPS << " fps" << endl;
	cout << "ImageHeight=" << imageHeight << " px" << endl;
	cout << "ImageWidth=" << imageWidth << " px" << endl;
	cout << "chosenVideoType=" << chosenVideoType << endl;
	cout << "h264bitrate=" << h264bitrate << " bps" << endl;
	cout << "mjpgquality=" << mjpgquality << " %" << endl;
	cout << "maxVideoSize=" << maxVideoSize << " GB" << endl;
	cout << "maxRAM=" << maxRAM << " GB" << endl;
	return result, FPS, imageHeight, imageWidth, chosenVideoType, h264bitrate, mjpgquality, maxVideoSize, maxRAM;
}

/*
========================================================================================================================================
Save2Video coverts the frames-vector to an .avi file.
========================================================================================================================================
*/
int Save2Video(string tempFilename, vector<ImagePtr>& frames)
{
	int result = 0;
	cout << "--- Converting to .AVI ";
	try
	{
		// creata a new filename
		string videoFilename = outpath + "\\" + tempFilename.substr(inpath.length() + 1, tempFilename.length() - (inpath.length() + 5));
		SpinVideo video; // Start and open Spinvideo file
		// Set maximum video file size in MiB (MebiBytes). A new video file is generated when limit is reached. Setting maximum file size to 0 indicates no limit.
		const unsigned int k_videoFileSize = maxVideoSize * 3814; //Conversion decimal GigaByte (GB) to binary MebiByte (MiB)
		video.SetMaximumFileSize(k_videoFileSize);
		// set the desired compression format (MJPG, H264, UNCOMPRESSED) and open videofile in that format.	
		if (chosenVideoType == "MJPG")
		{
			Video::MJPGOption option;
			option.frameRate = FPS;
			option.quality = mjpgquality;
			video.Open(videoFilename.c_str(), option);
			cout << "MJPG ";
		}
		else if (chosenVideoType == "H264")
		{
			Video::H264Option option;
			option.frameRate = FPS;
			option.bitrate = h264bitrate;
			option.height = static_cast<unsigned int>(frames[0]->GetHeight());
			option.width = static_cast<unsigned int>(frames[0]->GetWidth());
			video.Open(videoFilename.c_str(), option);
			cout << "H264 ";
		}
		else // UNCOMPRESSED
		{
			Video::AVIOption option;
			option.frameRate = FPS;
			video.Open(videoFilename.c_str(), option);
			cout << "UNCOMPRESSED ";
		}
		// Build and save video-file.
		cout << "--- Building video-file " << videoFilename <<" ---";
		for (int frameCnt = 0; frameCnt < frames.size(); frameCnt++)
		{
			video.Append(frames[frameCnt]);
		}
		video.Close(); // Close video file
		cout << "	Complete!" << endl;
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
FrameRetrieval takes binary data chunks (size = frameSize) from the .tmp file, imports it into the frames-vector in either BayerRG8 or Mono8 format and loads the Save2Video function.
========================================================================================================================================
*/
int FrameRetrieval(vector<string>& filenames, int numFiles)
{
	int result = 0;
	int frameSize = imageHeight * imageWidth;
	try 
	{
		for (int fileCnt = 0; fileCnt < numFiles; fileCnt++) // for-loop going over each .tmp file
		{
			string FilePath = filenames.at(fileCnt);
			cout << endl << "--- Retrieving frames from: " << FilePath.c_str() << " ---" << endl;
			ifstream rawFile(filenames.at(fileCnt).c_str(), ios_base::in | ios_base::binary);
			if (!rawFile) // check-file
			{
				cout << endl << "Could not open file! " << filenames.at(fileCnt).c_str() << "Press enter to exit." << endl;
				getchar();
				return -1;
			}
			// Frame retrieval from .tmp files. Binary data chunks (size = frameSize) are taken and imported into the frames-vector.
			vector<ImagePtr> frames; // Create an empty frames vector of ImagePointer objects
			int frameCnt = 0; // set frameCnt to zero
			while (rawFile.good() && frameCnt < 1000)
			{
				// Reading frames from .tmp file
				char* frameBuffer = new char[frameSize];
				rawFile.read(frameBuffer, frameSize); //Extracts frameSize characters from the .tmp file and assigns it to frameBuffer
				ImagePtr pImage = Image::Create(imageWidth, imageHeight, 0, 0, PixelFormat_BayerRG8, frameBuffer);
				frames.push_back(pImage->Convert(PixelFormat_BayerRG8, HQ_LINEAR));
				delete[] frameBuffer; // delete frameBuffer
				frameCnt++; // update frame counter
			}
			result = Save2Video(FilePath, frames); //converting frames-vector into .avi
			rawFile.close(); // Close .tmp file
		}
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
Main function of the script. In here input and output folders are defined, metadata.txt file loaded and the conversion process started.
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

	// Ask for metadata first to update config parameters
	cout << endl << "Provide the path to the metadata.txt file (path format example: C:\\RODI\\metadata.txt) " << endl;
	getline(cin, metadata);
	cout << endl << "--- Importing parameters from " + metadata + " ---" << endl;
	readconfig(metadata);
	// Automatic input of filenames based on input folder (inpath) to be converted by boost filesystem.
	cout << endl << "Specifiy input folder containing the binary files (path format example: C:\\RODI): " << endl;
	getline(cin, inpath);
	cout << endl << "--- Checking folder: " << inpath << " ---" << endl;
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
	//Specify an output folder (outpath), and test its writing permissions, in which converted files will be saved.
	cout << endl << "Specifiy an output folder (path format example: C:\\RODI)" << endl;
	getline(cin, outpath); 
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
	//Print the filenames that will be converted
	int numFiles = filenames.size();
	string num = to_string(numFiles);
	cout << endl << "--- Converting " + num + " .tmp files ---" << endl;
	for (int files = 0; files < numFiles; files++)
	{
		cout << "	+" << filenames[files] << endl;
	}
	cout << endl << "Press enter to start conversion" << endl << endl;
	getchar(); 
	// Start conversion process
	result = FrameRetrieval(filenames, numFiles);
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
	cout << endl << "All files were succesfully converted! Press enter to exit." << endl;
	getchar();
	return result;
}
