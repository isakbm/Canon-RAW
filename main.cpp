/* 

Extract raw difference values from Canon raw files .CR2

The console application takes two arguments.

main.a <input> <output>

<input> 	name of the input Canon raw file, e.g image.CR2
<output> 	name of the output file, e.g DIFF_VALUES.dat

The output is a file containing raw difference values from which you can continue to construct an image by simply summing the difference values. 
Resetting at the start of every new row.

In addition to producing a raw difference value image, the application also prints out metadata and headers.
Something that may or may not be useful to you, but that was definitely useful for debugging.

This piece of code was only possible thanks to an invaluable source on the .CR2 format that Canon uses.  : http://lclevy.free.fr/cr2/

A very nice poster on the lossless JPEG encoding can be found here : https://github.com/lclevy/libcraw2/blob/master/docs/cr2_lossless.pdf?raw=true

That source explains how to interpret the Huffman encoding and how to extract the difference values from it, something that turns out to be a very fun exercise in itself.

Note that the output is raw difference values. A lot of extra processing is needed to get a picture. You need to integrate the difference values, and do demosaicing to reconstruct color. 
Demosaicing is necessary since, as you might know, digital cameras use a Bayer filter "[[R,G], [G,B]] * [res_x, res_y]".

If you don't want color, you can simply do accumulation of the difference values to generate a grayscale.

*/

// This is a work in progress, the code needs to be tidied up and split into separate files. 


#include <stdio.h>  // fopen, fclose, fread, fseek
#include <stdlib.h>  // malloc, free
#include <stdint.h> // uint8_t, uint16_t, uint32_t

// ==================================================================================================================================================================================================
// TIFF TAG ENUM
// ==================================================================================================================================================================================================

enum TAG_ID_TYPE {
					   SENSOR_INFO = 224,
					   IMAGE_WIDTH = 256, 
				      IMAGE_LENGTH = 257, 
				   BITS_PER_SAMPLE = 258, 
					   COMPRESSION = 259, 
		PHOTOMETRIC_INTERPRETATION = 262, 
							  MAKE = 271, 
						     MODEL = 272, 
					  STRIP_OFFSET = 273, 
					   ORIENTATION = 274, 
				 SAMPLES_PER_PIXEL = 277, 
					ROWS_PER_STRIP = 278, 
			     STRIP_BYTE_COUNTS = 279, 
					  X_RESOLUTION = 282, 
				      Y_RESOLUTION = 283, 
			  PLANAR_CONFIGURATION = 284, 
				   RESOLUTION_UNIT = 296, 
						 DATE_TIME = 306, 
							ARTIST = 315, 
		   JPEG_INTERCHANGE_FORMAT = 513, 
	JPEG_INTERCHANGE_FORMAT_LENGTH = 514, 
						 COPYRIGHT = 33432,
						      EXIF = 34665,
					   		   GPS = 34853,
						 CR2_SLICE = 50752,
						 SRAW_TYPE = 50885,
					 EXPOSURE_TIME = 33434,
						  F_NUMBER = 33437,
						 MAKERNOTE = 37500,
};

// ==================================================================================================================================================================================================
// TYPEDEFS FOR EASY READ OF FILE HEADERS
// ==================================================================================================================================================================================================
typedef unsigned char uchar;

#pragma pack(push,1)

typedef struct TIFF_HEADER {
    char     id[2];
    uint16_t version; 
    uint32_t offset; 
} TIFF_HEADER; 
 
typedef struct CR2_HEADER {
    char     id[2]; 
    uint8_t  major;
    uint8_t  minor; 
    uint32_t offset; 
} CR2_HEADER; 

typedef struct TIFF_TAG {
	uint16_t ID;
	uint16_t type;
	uint32_t values;
	uint32_t value;
} TIFF_TAG; 

// THE FOLLOWING ARE RAW IFD HEADERS AND THEIR ENDIANNESS IS DIFFERENT
uint16_t swapBytes(uint16_t input) { return (input << 8) | (input >> 8); }

typedef struct DHT_HEADER 
{
	uint16_t marker, length;
	uint8_t  tc_index0;
	uint8_t  huff_data_0[16];
	uint8_t  huff_vals_0[15];
	uint8_t  tc_index1;
	uint8_t  huff_data_1[16];
	uint8_t  huff_vals_1[15];
	void swap() { marker = swapBytes(marker); 
				  length = swapBytes(length); }
} DHT_HEADER;

typedef struct SOF3_HEADER {
    uint16_t marker, length; 
   	uint8_t  sampleP;
    uint16_t num_lines, samp_per_lin;
    uint8_t  comp_per_frame;
    uint8_t  sFactors[12];
    void swap() { marker       = swapBytes(marker);
    			  length       = swapBytes(length);
    			  num_lines    = swapBytes(num_lines);
    			  samp_per_lin = swapBytes(samp_per_lin); }
} SOF3_HEADER;

typedef struct SOS_HEADER {
    uint16_t marker, length; // 
    uint8_t  numComp;
    uint8_t  scanCompSel[8];
    uint8_t  remBytes[3];
    void swap() { marker = swapBytes(marker);
    			  length = swapBytes(length); }
} SOS_HEADER;

#pragma pack(pop)

// ==================================================================================================================================================================================================
// Structs : ByteStream and ImData
// ==================================================================================================================================================================================================

struct ByteStream
{
	uint8_t * bytes;
	long size;
	long byteLoc;

	uint32_t bitBuffer;
	int bitStart;

	// Initialize bitStart to 32 so that the bitBuffer is "initialized" as entirely empty
	ByteStream() : size(0), byteLoc(0), bitStart(32), bitBuffer(0) {};

	void loadBytes(FILE * fp, long s);
	uint16_t readBits(int N);
	void print(int N);
};

struct ImData
{
	FILE * file;

	TIFF_HEADER tiff_header;
	CR2_HEADER  cr2_header;

	uint16_t sensor_width, sensor_height, sensor_left_border, sensor_top_border, sensor_right_border, sensor_bottom_border;
	
	uint16_t cr2_slice [3];

	long raw_offset, raw_size, raw_dht_offset, raw_sof3_offset, raw_sos_offset, raw_scan_offset, raw_scan_size;
	long exif_subdir_offset, makernote_offset;

	uint8_t huffData [16];
	int huffValues [16];
};

// ==================================================================================================================================================================================================
// MISC FUNCTIONS (TIDY UP)
// ==================================================================================================================================================================================================

void toFile(const char * fname, unsigned char * data, int width, int height);

template <typename T>
void getMinMax(T* , T* , T* , int, int);

template <typename T>
void freadVar(T*, FILE*);

void printFatLine();
void printThinLine();
void printTiffTag(TIFF_TAG tiff_tag, FILE * fp);
void printTagInfo(TIFF_TAG tag);
void printTagType(TIFF_TAG tag);
void printPointerDataTag(TIFF_TAG tag, FILE * f);
void printSensorDescriptor(int val);
void huffCodes(uint8_t * huffData, uint16_t * table);
void printBits(uint16_t integer);
void printBits(uint8_t integer);
void getDiffValues(int * diffOut, ImData im);

int elementSizeTag(TIFF_TAG tag);
int dataSizeTag(TIFF_TAG tag);
int getDiffValue(uint16_t code, int len);

unsigned char toUChar(int dVal, int maxAbs);

int main(int argc, char * argv[])
{

	// Check that input is proper
	if (argc != 3) 
	{
		printf("\nThis application takes two arguments:\n\nmain.a <input> <output>\n\n");
		return 0;
	}

	FILE *fp = fopen(argv[1], "rb");
	char* out_fname = argv[2];

	if ( fp == NULL )
	{
		printf("The file \"%s\" cannot be located or successfully opened!", argv[1]);
		return 0;
	}

	
	ImData imageData;
	imageData.file = fp;

	// ==================================================================================================================================================================================================
	// READING FILE HEADERS
	// ==================================================================================================================================================================================================

	// =====================================================================
	// TIFF AND CR2 HEADERS
	// =====================================================================

	printFatLine();
    freadVar(&imageData.tiff_header, fp);
    freadVar(&imageData.cr2_header, fp);
    printFatLine();
	
	// =====================================================================
	// IFD #0
    // =====================================================================
   
    printf("IFD#0 :");
    printFatLine();

    uint16_t num_IFD_entries;
    freadVar(&num_IFD_entries, fp);
   	
   	TIFF_TAG tiff_tag;
    for (int i = 0; i < num_IFD_entries; i++)	
    {
    	printf("Entry number %d ", i);
	    freadVar(&tiff_tag, fp);
	    if (tiff_tag.ID == EXIF)
	    {
	    	imageData.exif_subdir_offset = tiff_tag.value;
	    }
	    printTiffTag(tiff_tag, fp);
    }
    printFatLine();

    // =====================================================================
    // EXIF SUBDIR (of TIFF)
    // =====================================================================

    printf("EXIF SUBDIR:");
    printFatLine();
    fseek(fp, imageData.exif_subdir_offset, SEEK_SET);
    freadVar(&num_IFD_entries, fp);
    for (int i = 0; i < num_IFD_entries; i++)	
    {
        freadVar(&tiff_tag, fp);
	    if (tiff_tag.ID == MAKERNOTE)
	    {
	    	imageData.makernote_offset = tiff_tag.value;
	    }
	    printTiffTag(tiff_tag, fp);
    }
    printf("\n");
    printFatLine();

    // =====================================================================
	// Makernote
    // =====================================================================

   	printf("Makernote: (displaying sensor data only)");
   	printFatLine();
   	fseek(fp, imageData.makernote_offset, SEEK_SET);
   	freadVar(&num_IFD_entries, fp);

   	for (int i = 0; i < num_IFD_entries; i++)
   	{
   		freadVar(&tiff_tag, fp);
   		if (tiff_tag.ID == SENSOR_INFO)
   		{
   			fseek(fp, tiff_tag.value, SEEK_SET);
   			uint16_t numEntries;
   			freadVar(&numEntries, fp);
   			printf("\n");
   			for( int j = 0; j < 8; j++)
   			{
	   			uint16_t entryVal;
   				freadVar(&entryVal, fp);
   				printSensorDescriptor(j);
   				printf(" = %d\n", entryVal);

   				if (j == 0)
   					imageData.sensor_width = entryVal;
   				if (j == 1)
   					imageData.sensor_height = entryVal;
   				if (j == 4)
   					imageData.sensor_left_border = entryVal;
   				if (j == 5)
   					imageData.sensor_top_border = entryVal;
   				if (j == 6)
   					imageData.sensor_right_border = entryVal;
   				if (j == 7)
   					imageData.sensor_bottom_border = entryVal;
   			}
   		}
   	}
   	printFatLine();

    // =====================================================================
	// RAW IFD 
    // =====================================================================
   
    printf("RAW IFD :");
    printFatLine();
    fseek(fp, imageData.cr2_header.offset, SEEK_SET);
    fread(&num_IFD_entries, sizeof(num_IFD_entries), 1, fp);

	uint16_t cr2slice [3];

    for (int i = 0; i < num_IFD_entries; i++)	
    {
    	printf("Entry number %d ", i);
    	printThinLine();
	    freadVar(&tiff_tag, fp);
	    long currentOffset = ftell(fp);

	    if (tiff_tag.ID == STRIP_OFFSET) // Offset of raw
	    {
	    	imageData.raw_offset = tiff_tag.value;
	    }
	    if (tiff_tag.ID == STRIP_BYTE_COUNTS) // Size of raw in bytes
	    {
	    	imageData.raw_size = tiff_tag.value;
	    }
	    if (tiff_tag.ID == CR2_SLICE)
	    {
	    	fseek(fp, tiff_tag.value, SEEK_SET);
	    	fread(&cr2slice, sizeof(uint16_t), tiff_tag.values, fp);
	    	fseek(fp, currentOffset, SEEK_SET);
	    	fseek(fp, tiff_tag.value, SEEK_SET);
	    	fread(&imageData.cr2_slice, sizeof(uint16_t), tiff_tag.values, fp);
	    	fseek(fp, currentOffset, SEEK_SET);
	    }

	    printTiffTag(tiff_tag, fp);
    }
    printFatLine();
	// =====================================================================

	// =====================================================================
    // RAW FILE HEADERS
	// =====================================================================

    fseek(fp, imageData.raw_offset, SEEK_SET);

    uint16_t soi_marker;
    freadVar(&soi_marker, fp);
    printf("SOI_MARKER = %04x\n", swapBytes(soi_marker));

	// =====================================================================
    // DHT HEADER
	// =====================================================================
   
    printf("\n\nDHT_HEADER\n\n");

    DHT_HEADER dht_header;
    fread(&dht_header, sizeof(dht_header), 1, fp);
    dht_header.swap();

    for (int i = 0; i < 16; i++)
    {
    	imageData.huffData[i]   = dht_header.huff_data_0[i];
    	imageData.huffValues[i] = dht_header.huff_vals_0[i];
    }

    printf("%6s = %04x\n", "Marker", dht_header.marker);
    printf("%6s = %d\n"  , "Length", dht_header.length);

	// =====================================================================
    // SOF3 HEADER
	// =====================================================================

	printf("\n\nSOF3_HEADER\n\n");    
 	
	int currentOffset = ftell(fp);

	SOF3_HEADER sof3_header;
	fread(&sof3_header, sizeof(sof3_header), 1, fp);
	sof3_header.swap();

	printf("%15s = %04x\n", "Marker", sof3_header.marker);
	printf("%15s = %04x\n", "Length", sof3_header.length);
	printf("%15s = %d\n"  , "SampleP", sof3_header.sampleP);
	printf("%15s = %d\n"  , "Num Lines", sof3_header.num_lines);
	printf("%15s = %d\n"  , "Samp per line", sof3_header.samp_per_lin);
	printf("%15s = %d\n"  , "# comp per frame", sof3_header.comp_per_frame);
	printf("\n");

	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			printf("%02x", sof3_header.sFactors[j + 3*i]);
			if (j != 2)
				printf(" : ");
			else
				printf("\n");
		}
	}

 	int num_lines = sof3_header.num_lines;
 	int samplesPerLine = sof3_header.samp_per_lin;

	// =====================================================================
    // SOS HEADER
	// =====================================================================

 	printf("\n\n\n");
 	printf("SOS_HEADER\n\n");
	imageData.raw_sos_offset = ftell(fp);

	SOS_HEADER sos_header;
	fread(&sos_header, sizeof(sos_header), 1, fp);
	sos_header.swap();

	printf("%6s = %04x\n", "Marker", sos_header.marker);
	printf("%6s = %d\n"  , "Length", sos_header.length);
	printf("%6s = %d\n"  , "# comp", sos_header.numComp);
	printf("\n");
	printf("%s", "Scan component selector : \n");
	
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 2; j++)
		{
			if (j != 1)
				printf("%20s %x"    , " ", sos_header.scanCompSel[j + 2*i]);
			else
				printf("%20s %02x\n", " ", sos_header.scanCompSel[j + 2*i]);
		}
	}
	printf("Last three bytes: \n");
	for (int i = 0; i < 3; i++)
	{
		printf("%20s %02x\n", " ", sos_header.remBytes[i]);
	}

	imageData.raw_scan_offset = ftell(fp);
	imageData.raw_scan_size = imageData.raw_size - (imageData.raw_scan_offset - imageData.raw_offset);

	// ==================================================================================================================================================================================================
	// DECODING SCAN DATA
	// ==================================================================================================================================================================================================
	
	printf("\n\nAttempting to decode from binary to difference values\n");

	int numDiffValues = imageData.sensor_width*imageData.sensor_height;
	int * diffValueList = new int [numDiffValues];

	// Function that decodes binary data to produce difference values (RGGB diff values in this case)
	getDiffValues(diffValueList, imageData);

	printf("Decoding complete!\n");

	// =====================================================================
    // Un-slicing difference values
	// =====================================================================

	// RG RG RG RG RG RG RG RG RG RG RG ...
	// GB GB GB GB GB GB GB GB GB GB GB ...

	// ==============================================================================
	// Cropped slices
	// ==============================================================================

	int maxInt, minInt, maxAbs;
	minInt = -1046;
	maxInt =  1210;
	maxAbs = abs(maxInt) < abs(minInt) ? abs(minInt) : abs(maxInt) ;



	int sliceOneLen = cr2slice[1]*imageData.sensor_height;
	int sliceOneWidth = cr2slice[1];
	int sliceOneHeight = imageData.sensor_height;
	unsigned char * sliceOne = new unsigned char [sliceOneLen];
	for (int i = 0; i < sliceOneLen; i++)
	{
		unsigned char tchar = toUChar(diffValueList[i], maxAbs); //(unsigned char) ((diffValueList[i] - minInt)*(255/float(maxInt - minInt)));
		sliceOne[i] = tchar;
	}

	int sliceTwoLen = cr2slice[1]*imageData.sensor_height;
	int sliceTwoWidth = cr2slice[1];
	int sliceTwoHeight = imageData.sensor_height;
	unsigned char * sliceTwo = new unsigned char [sliceTwoLen];
	int ctr = 0;
	for (int i = sliceOneLen; i < (sliceOneLen + sliceTwoLen); i++)
	{
		unsigned char tchar = toUChar(diffValueList[i], maxAbs);
		sliceTwo[ctr++] = tchar;
	}

	int sliceThreeLen = cr2slice[2]*imageData.sensor_height;
	int sliceThreeWidth = cr2slice[2];
	int sliceThreeHeight = imageData.sensor_height;
	unsigned char * sliceThree = new unsigned char [sliceThreeLen];
	ctr = 0;
	for (int i = (sliceOneLen + sliceTwoLen); i < (sliceOneLen + sliceTwoLen + sliceThreeLen); i++)
	{
		unsigned char tchar = toUChar(diffValueList[i], maxAbs);
		sliceThree[ctr++] = tchar;
	}

	int RAW_LENGTH = imageData.sensor_width*imageData.sensor_height;
	int RAW_WIDTH  = imageData.sensor_width;
	int RAW_HEIGHT = imageData.sensor_height;
	unsigned char * RAW_DIFF = new unsigned char [RAW_LENGTH];
	ctr = 0;
	int ctrOne = 0, ctrTwo = 0, ctrThree = 0;
	for (int i = 0; i < RAW_HEIGHT; i++)
	{
		for (int j = 0; j < RAW_WIDTH; j++)
		{
			if (j < sliceOneWidth)
				RAW_DIFF[ctr++] = sliceOne[ctrOne++];
			else if (j < 2*sliceOneWidth)
				RAW_DIFF[ctr++] = sliceTwo[ctrTwo++];
			else
				RAW_DIFF[ctr++] = sliceThree[ctrThree++];
		}
	}

	// Cropped Slice
	int cropTop = 0;
	int cropLeft = 0;
	int cropRight = 1728;
	int cropSliceW = cropRight - cropLeft;
	// int cropSliceH = sliceOneHeight;
	int cropSliceH = sliceTwoHeight;
	int cropSliceLen = cropSliceW*cropSliceH;

	uchar * cropSliceOne = new uchar [cropSliceLen];
	ctr = 0;
	for (int i = 0; i < cropSliceH; i++)
	{
		for (int j = cropLeft; j < cropRight; j++ )
		{
			// cropSliceOne[ctr++] = sliceOne[j + i*sliceOneWidth];
			cropSliceOne[ctr++] = sliceTwo[j + i*sliceTwoWidth];
		}
	}	

	printf("trying to get bayer data\n");
	// TRYING TO GET BAYER DATA

	// dSliceOne
	int dSliceOneLen = cr2slice[1]*imageData.sensor_height;
	int dSliceOneWidth = cr2slice[1];
	int dSliceOneHeight = imageData.sensor_height;
	int * dSliceOne = new int [dSliceOneLen];
	ctr = 0;
	for (int i = 0 + (sliceOneWidth*sliceOneHeight); i < (sliceTwoLen + (sliceOneWidth*sliceOneHeight)) /*sliceOneLen*/ ; i++)
	{
		dSliceOne[ctr++] = diffValueList[i];
	}

	printf("cropping\n");
	// Cropped dSlice
	int cdHeight = cropSliceH - cropTop;
	int cdWidth  = cropSliceW;
	int cdLen    = cdHeight*cdWidth;
	int * cdSliceOne = new int [cdLen];
	ctr = 0;
	for (int i = cropTop; i < cropSliceH; i++)
	{
		for (int j = cropLeft; j < cropRight; j++ )
		{
			cdSliceOne[ctr++] = dSliceOne[j + i*dSliceOneWidth];
		}
	}	

	printf("accumulating\n"); 
	// Accumulated slice
	int * accumSliceOne = new int [cdLen];
	int prevRG[2] = {0,0}; // RG previous
	int prevGB[2] = {0,0}; // GB previous
	ctr = 0;

	int lineBegRG[2] = {0,0};
	int lineBegGB[2] = {0,0};

	for (int i = 0; i < cdHeight; i++)
	{
		// Reset prev channel values for each row. We set it to the beginning of the prev row.
		prevRG[0] = lineBegRG[0];
		prevRG[1] = lineBegRG[1];
		prevGB[0] = lineBegGB[0];
		prevGB[1] = lineBegGB[1];

		int prev = 20000;

		for (int j = 0; j < cdWidth; j++)
		{
			if ( false ) // Ignore first two values
			{
				accumSliceOne[ctr] = 0;
				ctr++;
			}
			else
			{
				accumSliceOne[ctr] = prev + cdSliceOne[ctr];
				prev = accumSliceOne[ctr];

				// if (ctr % cdWidth == 0)//imageData.sensor_width < 4)
				// {
				// 	accumSliceOne[ctr] = -2000;
				// }
				// else
				// {
				// 	accumSliceOne[ctr] = -2000;
				// }
				// if (false)
				// {
				// 	accumSliceOne[ctr] = cdSliceOne[ctr];
				// }
				// else if (i % 2) // Even rows: 0, 2, 4,
				// {
				// 	accumSliceOne[ctr] = prevRG[j%2] + cdSliceOne[ctr];
				// 	prevRG[j%2] = accumSliceOne[ctr];

				// 	// Save the first two channel entries for every line
				// 	if (j == 2)
				// 		lineBegRG[0] = accumSliceOne[ctr];
				// 	if (j == 3)
				// 		lineBegRG[1] = accumSliceOne[ctr];
				// }
				// else // Odd rows
				// {
				// 	accumSliceOne[ctr] = prevGB[j%2] + cdSliceOne[ctr];
				// 	prevGB[j%2] = accumSliceOne[ctr];

				// 	// Save the first two channel entries for every line
				// 	if (j == 2)
				// 		lineBegGB[0] = accumSliceOne[ctr];
				// 	if (j == 3)
				// 		lineBegGB[1] = accumSliceOne[ctr];
				// }


				ctr++;
			}
		}
	}

	printf("converting to uchar\n");

	int min, max, absmax;
	getMinMax(&min, &max, accumSliceOne, 0, cdLen);
	printf("the Bayer (min,max) = (%d, %d)\n", min, max);
	absmax = (abs(min) > abs(max)) ? abs(min) : abs(max) ;
	printf("Absmax = %d\n", absmax);

	uchar * accum = new uchar [cdLen];
	for (int i = 0; i < cdLen; i++)
	{
		accum[i] = toUChar(accumSliceOne[i], absmax);
	}

	// =====================================================================
    // CLOSING FILE
	// =====================================================================

	fclose(fp);

	// =====================================================================
    // Writing channel data to file
	// =====================================================================

	toFile(out_fname, accum, cdWidth, cdHeight);

}

void cubicInterpol(int * output, int * input)
{
	double y1 = input[1];
	double y2 = input[2];
	double y3 = input[3];
	double y4 = input[4];

	for (int x = 2; x < 4; x++)
	{
		double temp = - (1.0/42.0)*(x - 7.0)*(x - 6.0)*(x - 1.0)*y1 
	                  + (1.0/30.0)*(x - 7.0)*(x - 6.0)*x*y2
	                  - (1.0/30.0)*(x - 7.0)*(x - 1.0)*x*y3
	                  + (1.0/42.0)*(x - 6.0)*(x - 1.0)*x*y4;

	    output[x - 2] = int(1.0*temp);	
	}
}

int getDiffValue(uint16_t code, int len)
{
	return (code >= (1 << len -1) ? code : (code - (1 << len) + 1)) ;
}

void getDiffValues(int * diffOut, ImData im)
{
	// Retrieve huffman codes
	uint16_t codes [16];
	huffCodes(im.huffData, codes);

    uint16_t minLen = 0;
    uint16_t maxLen = 0;
    uint16_t numCodes = 0;
	for (int i = 0; i < 16; i++)
	{
		numCodes += im.huffData[i];

		if (minLen == 0 && im.huffData[i] != 0)
			minLen = i + 1;

		if (maxLen < i + 1 && im.huffData[i] != 0)
			maxLen = i + 1;
	}

	uint16_t lenTab [numCodes];
	uint16_t ccounter = 0;
	for (int i = 0; i < 16; i++)
	{
		uint16_t HD = im.huffData[i];
		for (int n = 0; n < HD; n++)
		{
			lenTab[ccounter] = i+1;
			ccounter ++;
		}
	}

	printf("did we get the codes?\n\n");
	for (int i = 0; i < 15; i++)
	{
		printBits(codes[i]); printf("\n");
	}

	ByteStream bstream;
	fseek(im.file, im.raw_scan_offset, SEEK_SET);
	bstream.loadBytes(im.file, im.raw_scan_size);
	fseek(im.file, im.raw_scan_offset, SEEK_SET);

	int numDiffValues = im.sensor_width * im.sensor_height;

	uint16_t remainder = 0;
	uint16_t remLen = 0;
	uint16_t CodeBitBuffer;


	int defCount = 0;
	int * defStatistics[4];
	int defCounters[4] = {0,0,0,0};
	for (int i = 0; i < 4; i++)
		defStatistics[i] = new int [im.sensor_height/2];


	for (int i = 0; i < numDiffValues; i++)
	{
		uint16_t slidingBits, code, codeLen, counter, codeValue, tempBits;
		tempBits = bstream.readBits(maxLen - remLen);
		CodeBitBuffer = (remainder << (maxLen - remLen)) + tempBits;

		slidingBits = CodeBitBuffer;
		counter     = numCodes -1;
		
		bool foundCode = false;
		for (int L = maxLen; L > minLen - 1 ; L--)
		{
			for (int N = 0; N < im.huffData[L-1]; N++)
			{
				code = codes[counter];
				if (slidingBits == code)
				{
					foundCode = true;
					codeLen   = lenTab[counter];
					codeValue = im.huffValues[counter];
					break;
				}
				counter--;
			}
			slidingBits = slidingBits >> 1;

			if (foundCode)
				break;	
		}

		if (!foundCode)
			printf("We didn't find code :O :O \n\n");

		uint16_t numNewBits, newBits, diffCode; 
		int temp, diffValue;

		remainder  = CodeBitBuffer - (code << (maxLen - codeLen) );
		temp       = codeValue - (maxLen - codeLen); // WOOOPS be aware of overflow, we correct in line below!
		numNewBits = temp > 0 ? temp : 0;

		if ( numNewBits )
		{
			newBits = bstream.readBits(numNewBits);
			diffCode = (remainder << numNewBits) + newBits;
			remainder = 0;
			remLen = 0;

			if (i == numDiffValues -1)
			{
				printf("newBits = "); printBits(newBits); printf("\n\n");
			}
		}
		else
		{
			diffCode = (remainder >> abs(temp));
			remainder = remainder - (diffCode << abs(temp));
			remLen = maxLen - codeLen - codeValue;
		}

	 	diffValue = getDiffValue(diffCode, codeValue);

	 	int modi_def = i % im.sensor_width;

	 	if ( defCount % 2 && modi_def < 4) // This hits exactly where the defects are
	 	{
	 		diffOut[i] = diffValue;
	 		defStatistics[modi_def][defCounters[modi_def]] = diffValue;
	 		defCounters[modi_def] += 1;
	 	}
	 	if ( modi_def == 0)
 			defCount ++;
	 	else
			diffOut[i] = diffValue;

	}

	long long defTots[4] = {0,0,0,0};
	for (int i = 0; i < im.sensor_height/2; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			defTots[j] += defStatistics[j][i];
		}
	}

	for (int i = 0; i < 4; i++)
	{
		printf("defTots[%d] = %d\n", i, defTots[i]);
	}

	printf("attempting to correct \n");
	for (int i = 0; i < numDiffValues; i++)
	{
		int modi_def = i % im.sensor_width;

		if (modi_def == 0 && i) // Construct the interpolation
		{	
			//	left     interpol    right
			// R G R G   R G R G    R G R G
			// 4 3 2 1   0 1 2 3    4 5 6 7

			int interpolR[2];
			int interpolG[2];

			int edgesR[4];
			int edgesG[4];

			// Left
			edgesR[0] = diffOut[i-4];
			edgesG[0] = diffOut[i-3];
			edgesR[1] = diffOut[i-2];
			edgesG[1] = diffOut[i-1];
			
			// Right
			edgesR[2] = diffOut[i+4];
			edgesG[2] = diffOut[i+5];
			edgesR[3] = diffOut[i+6];
			edgesG[3] = diffOut[i+7];

			cubicInterpol(interpolR, edgesR);
			cubicInterpol(interpolG, edgesG);

			// diffOut[i - 1] = -2000; // For debugging and indicating the start of a defect region.

			// diffOut[i + 0] = interpolR[0];
			// diffOut[i + 1] = interpolG[0];
			// diffOut[i + 2] = interpolR[1];
			// diffOut[i + 3] = interpolG[1];

			// diffOut[i + 0] = 0;
			// diffOut[i + 1] = 0;
			// diffOut[i + 2] = 0;
			// diffOut[i + 3] = 0;

		}
	}

}

void printFatLine()
{
	printf("\n=====================================================================\n");
}

void printThinLine()
{
	printf("\n---------------------------------------------------------------------\n");
}


void printBits(uint16_t integer)
{
	for (int i = 0; i < 16; i++)
	{
		printf("%d", bool(integer & (1 << 15)));
		integer = integer << 1;
	}
}

void printBits(uint8_t integer)
{
	for (int i = 0; i < 8; i++)
	{
		printf("%d", bool(integer & (1 << 7)));
		integer = integer << 1;
	}
}

unsigned char toUChar(int dVal, int maxAbs)
{
	return (unsigned char) ((dVal + maxAbs)*(128/float(maxAbs)));
}


void huffCodes(uint8_t * huffData, uint16_t * table)
{
	// [0,1] then shift << 1 and add 0 , 1 repeat
	int tabCount = 0;
	int numNotUsed = 2;
	int numUsed    = 0;
	uint16_t unusedCodes [300];
	uint16_t usedCodes   [300];
	uint16_t temp 	     [300];
	unusedCodes[0] = 0;
	unusedCodes[1] = 1;

	for (int i = 0; i < 16; i++)
	{
		// Use a number of unused codes
		int num = huffData[i];
		for (int j = 0; j < num; j++)
			usedCodes[numUsed + j] = unusedCodes[j];

		// Remove those codes from unused codes list
		numUsed += num;
		for (int j = num; j < numNotUsed; j++)
			temp[j-num] = unusedCodes[j];
		numNotUsed -= num;

		// Update new codes
		for (int j = 0; j < numNotUsed; j++)
		{
			unusedCodes[2*j + 0] = (temp[j]<<1) + 0;
			unusedCodes[2*j + 1] = (temp[j]<<1) + 1;
		}
		numNotUsed *= 2;
	}

	for (int i = 0; i < numUsed; i++)
	{
		table[i] = usedCodes[i];
	}
}

void ByteStream::loadBytes(FILE * fp, long s)
{

	size = s;
	bytes = new uint8_t [size];
	long floc = ftell(fp);

	int fileErrorCode = ferror(fp);
	if (fileErrorCode != 0)
	{
		printf("fileErrorCode = %d\n", fileErrorCode);
	}

	long numReadBytes = fread(bytes, sizeof(uint8_t), size, fp);
	if (numReadBytes != size)
	{
		printf("Warning in loadBytes(): Attempted to load %d bytes from file location %d \nManaged to read %d bytes\n\n", size, floc, numReadBytes);
	}
	size = numReadBytes;
}

uint16_t ByteStream::readBits(int N)
{
	int count = 0;
	while ((32 - bitStart < 24) && byteLoc < size)
	{
		bitBuffer = bitBuffer << 8;
		uint8_t byte = bytes[byteLoc];
		if (byte == 0xFF && (byteLoc + 1 < size))
		{
			if (bytes[byteLoc + 1] != 0x00)
			{
				printf("Byteloc  = %d\n", byteLoc);
				printf("Bitstart = %d\n", bitStart);
				printBits(uint16_t(bitBuffer>>16)); printf(" ");
				printBits(uint16_t((bitBuffer<<16)>>16));
				printf("\n\n");
				printBits(bytes[byteLoc]); printf(" ");
				printBits(bytes[byteLoc+1]);
				printf("\n");

				printf("\n");

				// printf("byteLoc = %d\nSize = %d\n", byteLoc, size);
				if (byteLoc != size - 2)
				{
					printf("We encountered an unexpected marker! byteLoc = %d\n", byteLoc + 1);
					printf("Marker : %x%x \n", bytes[byteLoc], bytes[byteLoc+1]);	
				}
			}
			else
				byteLoc++;
		}
		bitBuffer += uint32_t(byte);
		byteLoc ++;
		bitStart -= 8;
		count ++;
	}
	uint32_t temp = bitBuffer << bitStart;
	bitStart += N;
	return uint16_t(temp >> (32 - N));
}

void ByteStream::print(int N)
{
	for (long i = 0; i < N; i++)
	{
		printBits(bytes[i]);
		printf(" ");
	}
	printf("\n");
}

template <typename T>
void getMinMax(T * outMin, T * outMax, T * in, int start, int stop)
{
	T min = in[start];
	T max = in[start];
	for (int i = start; i < stop; i++)
	{
		if (min > in[i])
		{
			min = in[i];
		}
		if (max < in[i])
		{
			max = in[i];
		}
	}
	*outMin = min; 
	*outMax = max;
}


void toFile(const char * fname, unsigned char * data, int width, int height)
{
	FILE * filep = fopen(fname, "wb");

	fwrite(&width,  sizeof(width),  1, filep);
	fwrite(&height, sizeof(height), 1, filep);

	int numWrites = fwrite(data, sizeof(char), width*height, filep);

	if (numWrites != width*height)
		printf("\n error writing to file \n");
	
	fclose(filep);
}

void printTiffTag(TIFF_TAG tiff_tag, FILE * fp)
{
	printThinLine();
    printTagInfo(tiff_tag);
    printTagType(tiff_tag);
    printf("\nData size   : %d", dataSizeTag(tiff_tag));
    if ( dataSizeTag(tiff_tag) > 4 )
    {
    	printf(" : Pointer type");
    	long currentOffset = ftell(fp);
    	fseek(fp, tiff_tag.value, SEEK_SET);
    	printPointerDataTag(tiff_tag, fp);
    	fseek(fp, currentOffset, SEEK_SET);
    }
    printThinLine();
    printf("\n");
}

int dataSizeTag(TIFF_TAG tag)
{
	return elementSizeTag(tag)*tag.values;
}

template <typename T>
void freadVar(T* varPtr, FILE * file)
{
	fread(varPtr, sizeof(*varPtr), 1, file);
}

void printPointerDataTag(TIFF_TAG tag, FILE * f)
{
	char charString[255];
	uint16_t ushortString[255];
	uint32_t ulongString[255];
	long longString[255];
	double dIEEE;

	switch(tag.type)
	{
		case 1 :
			// printf("Type        : %s", "unsigned char");

			break;
		case 2 : // string (with an ending zero)
			fread(&charString, sizeof(char), tag.values, f);
			printf("\nValue (Ptr) : %0.*s", tag.values, charString);
			break;
		case 3 :  // unsigned short (2 bytes)
			fread(&ushortString, sizeof(uint16_t), tag.values, f);
			printf("\nValue (Ptr) : [");
			for (int i = 0; i < tag.values-1; i++)
				printf("%d, ", ushortString[i]);
			printf("%d]", ushortString[tag.values-1]);
			break;
		case 4 : // unsigned long (4 bytes)
			break;
		case 5 :
			// printf("Type        : %s", "unsigned rationnal (2 unsigned long)");
			fread(&ulongString, sizeof(uint32_t), 2, f);
			printf("\nValue (Ptr) : %d / %d", ulongString[0], ulongString[1]);
			break;
		case 6 :
			// printf("Type        : %s", "signed char");

			break;
		case 7 :
			// printf("Type        : %s", "byte sequence");

			break;
		case 8 :
			// printf("Type        : %s", "signed short");

			break;
		case 9 :
			// printf("Type        : %s", "signed long");

			break;
		case 10: // signed rationnal (2 signed long)
			fread(&longString, sizeof(uint32_t), 2, f);
			printf("\nValue (Ptr) : %d / %d", longString[0], longString[1]);
			break;
		case 11:
			// printf("Type        : %s", " float, 4 bytes, IEEE format");

			break;
		case 12: // float, 8 bytes, IEEE format
			fread(&dIEEE, sizeof(double), 1, f);
			printf("\nValue (Ptr) : %f", dIEEE);
			break;
		default:
			// printf("Type        : %s", " unknown type");
			break;
	}
}

void printSensorDescriptor(int val)
{
	switch(val+1)
	{
		case 1 :
			printf("%15s", "Width");
			break;
		case 2 :
			printf("%15s", "Height");
			break;
		case 5 :
			printf("%15s", "Left Border");
			break;
		case 6 :
			printf("%15s", "Top Border");
			break;
		case 7 :
			printf("%15s", "Right Border");
			break;
		case 8 :
			printf("%15s", "Bottom Border");
			break;
		default :
			printf("%15s", "Unknown value");
			break;
	}
}

int elementSizeTag(TIFF_TAG tag)
{
	int elSize;
	switch(tag.type)
	{
		case 1 :
			elSize = 1;
			break;
		case 2 :
			elSize = 1;
			break;
		case 3 :
			elSize = 2;
			break;
		case 4 :
			elSize = 4;
			break;
		case 5 :
			elSize = 8;
			break;
		case 6 :
			elSize = 1;
			break;
		case 7 :
			elSize = 1;
			break;
		case 8 :
			elSize = 2;
			break;
		case 9 :
			elSize = 4;
			break;
		case 10:
			elSize = 8;
			break;
		case 11:
			elSize = 4;
			break;
		case 12:
			elSize = 8;
			break;
		default:
			elSize = 4;
			break;
	}
	return elSize;
}

void printTagType(TIFF_TAG tag)
{
	switch(tag.type)
	{
		case 1 :
			printf("Type        : %s", "unsigned char");
			break;
		case 2 :
			printf("Type        : %s", "string (with an ending zero)");
			break;
		case 3 :
			printf("Type        : %s", "unsigned short (2 bytes)");
			break;
		case 4 :
			printf("Type        : %s", "unsigned long (4 bytes)");
			break;
		case 5 :
			printf("Type        : %s", "unsigned rationnal (2 unsigned long)");
			break;
		case 6 :
			printf("Type        : %s", "signed char");
			break;
		case 7 :
			printf("Type        : %s", "byte sequence");
			break;
		case 8 :
			printf("Type        : %s", "signed short");
			break;
		case 9 :
			printf("Type        : %s", "signed long");
			break;
		case 10:
			printf("Type        : %s", " signed rationnal (2 signed long)");
			break;
		case 11:
			printf("Type        : %s", " float, 4 bytes, IEEE format");
			break;
		case 12:
			printf("Type        : %s", " float, 8 bytes, IEEE format");
			break;
		default:
			printf("Type        : %s", " unknown type");
			break;
	}
}

void printTagInfo(TIFF_TAG tag)
{
	switch(tag.ID) 
	{
		case 256:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "ImageWidth", tag.values, tag.value);
            break;
        case 257:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "ImageLength", tag.values, tag.value);
            break;
        case 258:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "BitsPerSample", tag.values, tag.value);
            break;
        case 259:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "Compression", tag.values, tag.value);
            break;
        case 262:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "PhotometricInterpretation", tag.values, tag.value);
            break;
        case 271:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "Make", tag.values, tag.value);
            break;
        case 272:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "Model", tag.values, tag.value);
            break;
        case 273:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "StripOffsets", tag.values, tag.value);
            break;
        case 274:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "Orientation", tag.values, tag.value);
            break;
        case 277:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "SamplesPerPixel", tag.values, tag.value);
            break;
        case 278:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "RowsPerStrip", tag.values, tag.value);
            break;
        case 279:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "StripByteCounts", tag.values, tag.value);
            break;
        case 282:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "XResolution", tag.values, tag.value);
            break;
        case 283:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "YResolution", tag.values, tag.value);
            break;
        case 284:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "PlanarConfiguration", tag.values, tag.value);
            break;
        case 296:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "ResolutionUnit", tag.values, tag.value);
            break;
        case 306:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "DateTime", tag.values, tag.value);
            break;
        case 315:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "Artist", tag.values, tag.value);
            break;
        case 513:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "JPEGInterchangeFormat", tag.values, tag.value);
            break;
        case 514:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "JPEGInterchangeFormatLength", tag.values, tag.value);
            break;
        case 33432:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "Copyright", tag.values, tag.value);
            break;
        case 34665:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "EXIF", tag.values, tag.value);
            break;
        case 34853:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "GPS", tag.values, tag.value);
            break;
        case 50648:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "??", tag.values, tag.value);
            break;
        case 50649:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "??", tag.values, tag.value);
            break;
        case 50656:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "??", tag.values, tag.value);
            break;
        case 50752:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "CR2 slice", tag.values, tag.value);
            break;
        case 50885:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "sRawType", tag.values, tag.value);
            break;
        case 50908:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "??", tag.values, tag.value);
            break;
        case 33434:
            printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "Exposure Time", tag.values, tag.value);
            break;
        case 33437:
		    printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "f Number", tag.values, tag.value);
		    break;		
        case 37500:
			printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "Makernote", tag.values, tag.value);
		    break;		
        default:
        	printf("Tag ID      : %d\nDescription : %s\nValues      : %d\nValue       : %d\n", tag.ID, "??", tag.values, tag.value);
            break;
	}
}

