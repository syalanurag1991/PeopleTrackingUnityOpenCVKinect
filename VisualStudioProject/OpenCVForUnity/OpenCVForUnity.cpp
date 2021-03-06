// OpenCVForUnity.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "OpenCVForUnity.h"

// C++ variables
uchar rgbData[rgbFrameWidth * rgbFrameHeight * 4];							// BGRA array containing the texture data
uchar depthData[depthFrameWidth * depthFrameHeight];						// Array containing depth texture data
uchar blobsBasedDepthData[depthFrameWidth * depthFrameHeight];				// Array containing binary depth texture data (0 or 255)
uchar depthDataForVisualization[depthFrameWidth * depthFrameHeight * 4];	// Array containing depth texture data for visualization

																			// OpenCV variables
Mat ocvRGBFrameData;														// RGB data from Depth Sensor (BGRA, 32 bit)
Mat ocvDepthFrameData;														// Depth data from Depth Sensor (GRAY, 8 bit)
Mat ocvDepthFrameFGMaskCleaned_MOG2;										// Noise-free data after background-subtraction (MOG2)
Mat ocvDepthFrameDataForVisualization;										// Process visualization feed

																			// Denoising and Blob-formation Algorithm variables
bool dataAvailable = false;
bool showVisualization = false;
bool foregroundMaskIsAvailable = false;

int totalFramesPassed = 0;
int frameFetchAttempt = 0;
int maxNumberOfFrameFetchAttempts = 10;
int waitForFramesToPass = 1000;
int framesPassedAfterLastUpdate = 0;
int stopUpdatingBackgroundAfterFrames = 50;

// Blob-tracking variables
bool processingNow = false;
Ptr<SimpleBlobDetector> blobDetector;
vector<KeyPoint> keypoints;
list<MyBlob> collectionOfBlobs;

Ptr< BackgroundSubtractor> pointerToSubtractorObject_MOG2;
Mat foregroundMask_MOG2;
Mat updatedForegroundMask;

Mat morphingKernel_size1 = getStructuringElement(MORPH_RECT, Size(1, 1));
Mat morphingKernel_size3 = getStructuringElement(MORPH_RECT, Size(3, 3));
Mat morphingKernel_size5 = getStructuringElement(MORPH_RECT, Size(5, 5));
Mat morphingKernel_size7 = getStructuringElement(MORPH_RECT, Size(7, 7));

namespace OpenCV {
	float Functions::Add(float a, float b) {
		float result = a + b;
		return result;
	}

	float Functions::Multiply(float a, float b) {
		float result = a * b;
		return result;
	}

	float Functions::Foopluginmethod() {
		Mat img(10, 10, CV_8UC1); // use some OpenCV objects
		return img.rows * 1.0f;     // should return 10.0f
	}

	char* Functions::getByteArray() {
		//Create your array(Allocate memory)
		char * arrayTest = new char[2];

		//Do something to the Array
		arrayTest[0] = 3;
		arrayTest[1] = 5;

		//Return it
		return arrayTest;
	}

	int Functions::freeMem(char* arrayPtr) {
		delete[] arrayPtr;
		return 0;
	}

	// Use this to Convert RGB data from Depth Sensor to OpenCV format.
	int Functions::ConvertRGBDataToOpenCVFormat(uchar * rgbDataFromSensor) {
		if (&rgbDataFromSensor[0] == nullptr)
			return -2;
		memcpy(rgbData, rgbDataFromSensor, rgbFrameWidth * rgbFrameHeight * 4);
		int width = ocvRGBFrameData.cols;
		int height = ocvRGBFrameData.rows;
		if (width > 0 && height > 0) {
			return width * height;
		}
		else {
			return -1;
		}
	}

	// Use this to Convert Depth data from Depth Sensor to OpenCV format
	int Functions::ConvertDepthDataToOpenCVFormat(uchar * depthDataFromSensor) {
		if (&depthDataFromSensor[0] == nullptr)
			return -2;
		memcpy(depthData, depthDataFromSensor, depthFrameWidth * depthFrameHeight);
		int width = ocvDepthFrameData.cols;
		int height = ocvDepthFrameData.rows;
		if (width > 0 && height > 0) {
			CleanDepthData();
			return width * height;
		}
		else {
			return -1;
		}
	}

	// Use this to Convert Depth data for visualization from Depth Sensor to OpenCV format
	int Functions::ConvertVisualizationDepthDataToOpenCVFormat(uchar * visualizationDepthDataFromSensor) {
		if (&depthDataForVisualization[0] == nullptr)
			return -2;
		memcpy(depthDataForVisualization, visualizationDepthDataFromSensor, 4 * depthFrameWidth * depthFrameHeight);
		int width = ocvDepthFrameDataForVisualization.cols;
		int height = ocvDepthFrameDataForVisualization.rows;

		if (showVisualization) {
			for (list<MyBlob>::iterator blobIterator = collectionOfBlobs.begin(); blobIterator != collectionOfBlobs.end(); blobIterator++) {
				MyFilledCircle(&ocvDepthFrameDataForVisualization, Point2f((*blobIterator).xActual, (*blobIterator).yActual));
			}
		}

		if (width > 0 && height > 0) {
			return width * height;
		}
		else {
			return -1;
		}
	}

	// Initializes background subtraction object using MOG2 method (an OpenCV function)
	void Functions::InitializeForegroundMasking() {
		pointerToSubtractorObject_MOG2 = createBackgroundSubtractorMOG2(10, 16, false);
	}

	// Updates data input for background-subtraction.
	// Waits for certain number of frames to pass before updating.
	// A '0'-wait will mean that data input is updated every frame.
	// First few frames in background-subtraction are used for determining the background.
	// In general, you don't want to update input for background-subtraction every frame.
	// Ideally you would want background to be updated only a few times before it is fixed. 
	void Functions::UpdateForegroundMask() {
		// Initialize foreground mask
		if (updatedForegroundMask.rows == 0) {
			if (ocvDepthFrameData.rows != 0) {
				updatedForegroundMask = ocvDepthFrameData;
			}
		}

		framesPassedAfterLastUpdate++;
		//cout << "Total frames passed: " << totalFramesPassed << "\n";
		if (framesPassedAfterLastUpdate == waitForFramesToPass) {
			//cout << "Foreground mask #" << framesPassedAfterLastUpdate << " updated!";
			framesPassedAfterLastUpdate = 0;
			updatedForegroundMask = ocvDepthFrameData;
		}
		if (updatedForegroundMask.rows != 0)
			foregroundMaskIsAvailable = true;
	}

	// Performs background-subtraction and produces a masked-foreground
	void Functions::ApplyForegroundMasking() {
		totalFramesPassed++;
		if (totalFramesPassed < stopUpdatingBackgroundAfterFrames) {
			pointerToSubtractorObject_MOG2->apply(ocvDepthFrameData, foregroundMask_MOG2, 0.01);
		}
		else {
			pointerToSubtractorObject_MOG2->apply(ocvDepthFrameData, foregroundMask_MOG2, 0);
		}

		if (totalFramesPassed == 10000)
			totalFramesPassed = 0;
	}

	// Function performs denoising of background-subtracted depth frame
	// 'Hole-filling' is performed before return to fill gaps in blobs
	void Functions::CleanForegroundMask() {
		Mat *toBeCleanedFrame, *cleanedFrame;
		toBeCleanedFrame = &foregroundMask_MOG2;
		cleanedFrame = &ocvDepthFrameFGMaskCleaned_MOG2;

		// De-noising
		medianBlur(*toBeCleanedFrame, *cleanedFrame, 7);
		medianBlur(*cleanedFrame, *cleanedFrame, 3);
		GaussianBlur(*cleanedFrame, *cleanedFrame, Size(3, 3), 3, 3);
		threshold(*cleanedFrame, *cleanedFrame, 200.0, 255.0, THRESH_BINARY);
		erode(*cleanedFrame, *cleanedFrame, morphingKernel_size3, Point(-1, -1), 3);

		//Hole-filling
		dilate(*cleanedFrame, *cleanedFrame, morphingKernel_size3, Point(-1, -1), 3);
		Mat foregroundComplement = (*cleanedFrame).clone();
		floodFill(foregroundComplement, Point(0, 0), Scalar(255));
		bitwise_not(foregroundComplement, foregroundComplement);
		*cleanedFrame = (*cleanedFrame | foregroundComplement);
		return;
	}

	// Function performs denoising of depth data 
	// 'Hole-filling' is performed before return to fill gaps in blobs
	deque<Mat> bufferDepthFrames;
	deque<Mat> bufferDepthFrameMasks;
	int bufferSize = 30;
	float newFrameWeight = 0.1;
	float maskResizeFactor = 0.5;
	uchar binarizingThreshold = 254;
	const unsigned char noDepth = 0;
	int Functions::GetBufferSize() {
		return bufferDepthFrameMasks.size();
	}
	void Functions::SetDepthCleaningParameters(int numberOfFrames, float currentFrameWeight, float resizeFactor, uchar binarizingCutoff) {

		bufferSize = numberOfFrames;
		newFrameWeight = currentFrameWeight;
		maskResizeFactor = resizeFactor;
		binarizingThreshold = binarizingCutoff;

		if (maskResizeFactor > 1)
			maskResizeFactor = 1;
		else if (maskResizeFactor < 0)
			maskResizeFactor = 0;

		if (newFrameWeight > 1)
			newFrameWeight = 1;
		else if (newFrameWeight < 0)
			newFrameWeight = 0;

		return;
	}

	void Functions::CleanDepthData() {
		// Inpainting
		// 1 step - downsize for performance, use a smaller version of depth image
		//Mat depthDataHalfResolution;
		//resize(*cleanedFrame, depthDataHalfResolution, Size(), 0.2, 0.2);
		// 2 step - inpaint only the masked "unknown" pixels
		//Mat inpaintResultHalfResolution, inpaintResult;
		//inpaint(depthDataHalfResolution, (depthDataHalfResolution == noDepth), inpaintResultHalfResolution, 5.0, INPAINT_TELEA);
		// 3 step - upscale to original size and replace inpainted regions in original depth image
		//resize(inpaintResultHalfResolution, inpaintResult, (*cleanedFrame).size());
		//inpaintResult.copyTo(inpaintResult, (*cleanedFrame == noDepth));  // Prepare mask
		//inpaintResult.copyTo(*cleanedFrame, mask);  // copy original signal
		Mat mask = ocvDepthFrameData.clone();
		resize(mask, mask, Size(), maskResizeFactor, maskResizeFactor);
		medianBlur(mask, mask, 3);
		medianBlur(mask, mask, 3);
		threshold(mask, mask, 10, 255, THRESH_BINARY);
		bufferDepthFrameMasks.push_back(mask);
		if (bufferDepthFrameMasks.size() > bufferSize) {
			bufferDepthFrameMasks.pop_front();
			Mat averageDepthFrameMask = Mat(mask.size(), CV_32FC1, Scalar());
			for (int i = 0; i < bufferSize-1; i++) {
				Mat tempDepthFrameMask = bufferDepthFrameMasks[i];
				tempDepthFrameMask.convertTo(tempDepthFrameMask, CV_32FC1);
				averageDepthFrameMask += tempDepthFrameMask;
			}
			averageDepthFrameMask /=  (float)(bufferSize-1);
			averageDepthFrameMask.convertTo(averageDepthFrameMask, CV_8UC1);
			threshold(averageDepthFrameMask, averageDepthFrameMask, binarizingThreshold, 255, THRESH_BINARY);
			averageDepthFrameMask.convertTo(averageDepthFrameMask, CV_32FC1);
			Mat tempDepthFrameMask = bufferDepthFrameMasks[bufferSize - 1];
			tempDepthFrameMask.convertTo(tempDepthFrameMask, CV_32FC1);
			averageDepthFrameMask = (1 - newFrameWeight)*averageDepthFrameMask + newFrameWeight*tempDepthFrameMask;
			resize(averageDepthFrameMask, averageDepthFrameMask, Size(), 1 / maskResizeFactor, 1 / maskResizeFactor);
			averageDepthFrameMask.convertTo(averageDepthFrameMask, CV_8UC1);
			threshold(averageDepthFrameMask, averageDepthFrameMask, binarizingThreshold, 255, THRESH_BINARY);
			dilate(averageDepthFrameMask, averageDepthFrameMask, morphingKernel_size3, Point(-1, -1), 5);
			medianBlur(averageDepthFrameMask, averageDepthFrameMask, 3);
			medianBlur(averageDepthFrameMask, averageDepthFrameMask, 3);
			medianBlur(averageDepthFrameMask, averageDepthFrameMask, 3);
			ocvDepthFrameData &= averageDepthFrameMask;
		}
		return;
	}

	// Use this function to initialize blob-tracker
	// Parameters are fixed
	void Functions::InitializeBlobTracker() {
		SimpleBlobDetector::Params params;
		params.minDistBetweenBlobs = 10;

		// Filter by color
		params.filterByColor = true;
		params.blobColor = 255;

		// Change thresholds
		params.minThreshold = 0;
		params.maxThreshold = 1000;

		// Filter by Area
		params.filterByArea = true;
		params.minArea = 100;
		params.maxArea = 1000000;

		// Filter by Circularity
		params.filterByCircularity = false;
		params.minCircularity = 0.1;

		// Filter by Convexity
		params.filterByConvexity = false;
		params.minConvexity = 0.87;

		// Filter by Inertia
		params.filterByInertia = false;
		params.minInertiaRatio = 0.01;

#if CV_MAJOR_VERSION < 3   // If you are using OpenCV 2

		// Set up detector with params
		SimpleBlobDetector detector(params);

		// You can use the detector this way
		// detector.detect( im, keypoints);

#else

		// Set up detector with params
		// SimpleBlobDetector::create creates a smart pointer. 
		// So you need to use arrow ( ->) instead of dot ( . )
		// detector->detect( im, keypoints);
		blobDetector = SimpleBlobDetector::create(params);
#endif
	}

	// Use function to create a circular visualizer for blob-tracking points
	void Functions::MyFilledCircle(Mat* img, Point2f center) {
		int w = 200;
		circle(*img,
			center,
			w / 32,
			CV_RGB(255, 255, 255),
			FILLED,
			LINE_8);
	}

	// Function will return a byte-array containing data from latest RGB data
	uchar* Functions::GetRGBData() {
		if (rgbData != nullptr) {
			uchar *rgbDataArray = rgbData;
			return rgbDataArray;
		}
		else {
			for (int i = 0; i < rgbFrameHeight*rgbFrameWidth; i++) {
				rgbData[4 * i] = 255;
				rgbData[4 * i + 1] = 255;
				rgbData[4 * i + 2] = 0;
				rgbData[4 * i + 3] = 255;
			}
			uchar * rgbDataArray = rgbData;
			return rgbDataArray;
		}
	}

	// Function will return a byte-array containing raw depth data from latest Depth frame
	uchar* Functions::GetDepthData() {
		if (depthData != nullptr) {
			uchar * depthDataArray = depthData;
			return depthDataArray;
		}
		else {
			for (int i = 0; i < depthFrameHeight*depthFrameWidth; i++) {
				depthData[i] = 128;
			}
			uchar * depthDataArray = depthData;
			return depthDataArray;
		}
	}

	// Function will return a byte-array containing depth data for only blob regions
	uchar* Functions::GetBlobsBasedDepthData() {
		if (ocvDepthFrameFGMaskCleaned_MOG2.total() > 0 && ocvDepthFrameFGMaskCleaned_MOG2.elemSize() > 0) {
			int size = ocvDepthFrameFGMaskCleaned_MOG2.total() * ocvDepthFrameFGMaskCleaned_MOG2.elemSize();
			memcpy(blobsBasedDepthData, ocvDepthFrameFGMaskCleaned_MOG2.data, size * sizeof(uchar));
			uchar * blobBasedDepthDataArray = blobsBasedDepthData;
			return blobBasedDepthDataArray;
		}
		else {
			for (int i = 0; i < depthFrameHeight*depthFrameWidth; i++) {
				blobsBasedDepthData[i] = 128;
			}
			uchar * blobBasedDepthDataArray = blobsBasedDepthData;
			return blobBasedDepthDataArray;
		}
	}

	// Function will return a byte-array containing depth data for visualization
	uchar* Functions::GetDepthDataForVisualization() {
		if (ocvDepthFrameDataForVisualization.total() > 0 && ocvDepthFrameDataForVisualization.elemSize() > 0) {
			int size = ocvDepthFrameDataForVisualization.total() * ocvDepthFrameDataForVisualization.elemSize();
			memcpy(depthDataForVisualization, ocvDepthFrameDataForVisualization.data, size * sizeof(uchar));
			uchar * depthDataForVisualizationArray = depthDataForVisualization;
			return depthDataForVisualizationArray;
		}
		else {
			for (int i = 0; i < depthFrameHeight*depthFrameWidth; i++) {
				depthDataForVisualization[4 * i] = 255;
				depthDataForVisualization[4 * i + 1] = 255;
				depthDataForVisualization[4 * i + 2] = 0;
				depthDataForVisualization[4 * i + 3] = 255;
			}
			uchar * depthDataForVisualizationArray = depthDataForVisualization;
			return depthDataForVisualization;
		}
	}

	// Function initializes depth sensor as well as OpenCV modules.
	// Initiates receiving of RGB and Depth streams from sensor as well.
	// Returns 'TRUE'/'FALSE'. Accepts min and max observable distances.
	// Function activates processing for visualizing tracking feed.
	// Visualization slows down tracking as new feed is generated.
	// Hence, default value for activateVisualization is FALSE.
	bool Functions::InitializeTracking(bool activateVisualization) {
		showVisualization = activateVisualization;
		InitializeForegroundMasking();
		InitializeBlobTracker();
		ocvRGBFrameData = Mat(rgbFrameHeight, rgbFrameWidth, CV_8UC4, rgbData);
		ocvDepthFrameData = Mat(depthFrameHeight, depthFrameWidth, CV_8UC1, depthData);
		if (showVisualization)
			ocvDepthFrameDataForVisualization = Mat(depthFrameHeight, depthFrameWidth, CV_8UC4, depthDataForVisualization);
		return true;
	}

	// Function returns status of blob-tracking thread
	// Recommended to check status before calling 'TrackInFrame' function
	bool Functions::IsBlobTrackingThreadRunning() {
		return processingNow;
	}

	// Function will track blobs for only current frame
	bool Functions::TrackInFrame() {
		processingNow = true;
		UpdateForegroundMask();
		if (foregroundMaskIsAvailable) {
			ApplyForegroundMasking();
			CleanForegroundMask();
		}
		blobDetector->detect(ocvDepthFrameFGMaskCleaned_MOG2, keypoints);
		TrackBlobsPersistently(); \
			processingNow = false;
		return true;
	}

	// Perform persistent blob-tracking
	void Functions::TrackBlobsPersistently() {
		int numberOfBlobsThisFrame = keypoints.size();
		int numberOfBlobsInCollection = collectionOfBlobs.size();
		if (numberOfBlobsInCollection == 0) {									// CASE #1 Blobs collection is empty
			for (int i = 0; i < numberOfBlobsThisFrame; i++) {
				collectionOfBlobs.push_back(CreateNewBlobForCollection(
					i,
					keypoints.at(i).pt.x,
					keypoints.at(i).pt.y
				));
			}
		}
		else if (numberOfBlobsThisFrame >= numberOfBlobsInCollection) {											// CASE #2 More/Equal blobs detected than present
			bool *foundMatchInExistingBlobsCollection = (bool*)malloc(numberOfBlobsThisFrame * sizeof(bool));	// Match new blobs with existing blobs
			memset(foundMatchInExistingBlobsCollection, false, numberOfBlobsThisFrame);
			for (list<MyBlob>::iterator blobIterator = collectionOfBlobs.begin(); blobIterator != collectionOfBlobs.end(); blobIterator++) {
				int index = -1;
				float distanceThreshold = 50000.0;
				for (int i = 0; i < numberOfBlobsThisFrame; i++) {
					float distanceFromCurrentBlob = GetDistance(
						NormalizeX(keypoints.at(i).pt.x),
						NormalizeY(keypoints.at(i).pt.y),
						(*blobIterator).xNormalized,
						(*blobIterator).yNormalized
					);
					if (distanceFromCurrentBlob < distanceThreshold && !foundMatchInExistingBlobsCollection[i]) {
						distanceThreshold = distanceFromCurrentBlob;
						index = i;
					}
				}
				foundMatchInExistingBlobsCollection[index] = true;
				UpdateExistingBlobInCollection(&*blobIterator, keypoints.at(index).pt.x, keypoints.at(index).pt.y);
			}
			for (int i = 0; i < numberOfBlobsThisFrame; i++) {					// Add new blobs to collection
				if (!foundMatchInExistingBlobsCollection[i]) {
					collectionOfBlobs.push_back(
						CreateNewBlobForCollection(numberOfBlobsInCollection, keypoints.at(i).pt.x, keypoints.at(i).pt.y));
					numberOfBlobsInCollection++;
				}
			}
		}
		else {																						// CASE #3 Fewer blobs detected than present
			bool *toBeRetainedInCollection = (bool*)malloc(numberOfBlobsInCollection * sizeof(bool)); // Match new blobs with existing blobs
			memset(toBeRetainedInCollection, false, numberOfBlobsInCollection);
			for (int i = 0; i < numberOfBlobsThisFrame; i++) {
				int index = -1;
				float distanceThreshold = 50000.0;
				int blobCoordinateX = keypoints.at(i).pt.x;
				int blobCoordinateY = keypoints.at(i).pt.y;
				list<MyBlob>::iterator blobIterator = collectionOfBlobs.begin();
				for (int j = 0; j < numberOfBlobsInCollection; j++) {
					advance(blobIterator, j);
					float distanceFromCurrentBlob = GetDistance(
						NormalizeX(blobCoordinateX),
						NormalizeY(blobCoordinateY),
						(*blobIterator).xNormalized,
						(*blobIterator).yNormalized
					);
					if (distanceFromCurrentBlob < distanceThreshold && !toBeRetainedInCollection[j]) {
						distanceThreshold = distanceFromCurrentBlob;
						index = j;
					}
				}
				blobIterator = collectionOfBlobs.begin();
				advance(blobIterator, index);
				toBeRetainedInCollection[index] = true;
				UpdateExistingBlobInCollection(&*blobIterator, blobCoordinateX, blobCoordinateY);
			}
			for (int i = 0, tempBlobIndex = 0; i < numberOfBlobsInCollection; i++) {// Delete old blobs from collection
				MyBlob tempBlob = collectionOfBlobs.front();
				collectionOfBlobs.pop_front();
				if (toBeRetainedInCollection[i]) {
					tempBlob.index = tempBlobIndex;
					collectionOfBlobs.push_back(tempBlob);
					tempBlobIndex++;
				}
			}
		}
	}

	// Function to create new blob, specifically for CollectionOfBlobs list
	MyBlob Functions::CreateNewBlobForCollection(int index, float blobCoordinateX, float blobCoordinateY) {
		MyBlob newBlob;
		newBlob.index = index;
		newBlob.xActual = blobCoordinateX;
		newBlob.yActual = blobCoordinateY;
		newBlob.xNormalized = NormalizeX(blobCoordinateX);
		newBlob.yNormalized = NormalizeY(blobCoordinateY);
		newBlob.depthValue = NormalizeDepth(blobCoordinateX, blobCoordinateY);
		return newBlob;
	}

	// Function to update a blob in CollectionOfBlobs list
	void Functions::UpdateExistingBlobInCollection(MyBlob * addressOfBlob, float blobCoordinateX, float blobCoordinateY) {
		(*addressOfBlob).xActual = blobCoordinateX;
		(*addressOfBlob).yActual = blobCoordinateY;
		(*addressOfBlob).xNormalized = NormalizeX(blobCoordinateX);
		(*addressOfBlob).yNormalized = NormalizeY(blobCoordinateY);
		(*addressOfBlob).depthValue = NormalizeDepth((int)blobCoordinateX, (int)blobCoordinateY);
	}

	// Functions to normalize (x, y) coordinates
	float Functions::NormalizeX(float blobCoordinateX) {
		return (float)((blobCoordinateX - depthFrameHalfWidth) / (float)depthFrameHalfWidth);
	}

	float Functions::NormalizeY(float blobCoordinateY) {
		return (float)((blobCoordinateY - depthFrameHalfHeight) / (float)depthFrameHalfHeight);
	}

	float Functions::NormalizeDepth(int blobCoordinateX, int blobCoordinateY) {
		return (float)depthData[depthFrameWidth * blobCoordinateY + blobCoordinateX];
	}

	// Function to calculate distance between two blobs
	float Functions::GetDistance(float x1, float y1, float x2, float y2) {
		float xDiff = x1 - x2;
		float yDiff = y1 - y2;
		return (xDiff * xDiff) + (yDiff * yDiff);
	}

	// Function returns number of blobs detected
	int Functions::GetNumberOfBlobs() {
		if (!collectionOfBlobs.empty()) {
			return collectionOfBlobs.size();
		}
		else {
			return 0;
		}
	}

	// Get number of blobs detected before using this
	// Each blob has (x, y) coordinates pair and depth value of its center
	// [0] -> x-Coordinate, [1] -> y-Coordinate, [2] -> depth value at center
	float* Functions::GetBlobsData() {
		int numberOfBlobs = collectionOfBlobs.size();
		float *blobsData = new float[numberOfBlobs * 4];
		int iteration = 0;
		for (list<MyBlob>::iterator blobIterator = collectionOfBlobs.begin(); blobIterator != collectionOfBlobs.end(); blobIterator++) {
			blobsData[4 * iteration] = (float)(*blobIterator).index;
			blobsData[4 * iteration + 1] = (*blobIterator).xNormalized;
			blobsData[4 * iteration + 2] = (*blobIterator).yNormalized;
			//blobsData[4 * iteration + 1] = (*blobIterator).xActual;
			//blobsData[4 * iteration + 2] = (*blobIterator).yActual;
			blobsData[4 * iteration + 3] = (*blobIterator).depthValue;
			iteration++;
		}
		return blobsData;
	}

	// Use this function to delete blobs data and frees memory
	int Functions::DeleteBlobsData(float* blobsData) {
		delete[] blobsData;
		return 0;
	}

	// Function closes sensor and frees memory
	bool Functions::Close() {
		if (blobDetector)
			blobDetector->clear();

		if (keypoints.size() > 0)
			keypoints.clear();

		if (collectionOfBlobs.size() > 0)
			collectionOfBlobs.clear();

		if (pointerToSubtractorObject_MOG2)
			pointerToSubtractorObject_MOG2->clear();

		return true;
	}
}