#include "opeswf.h"

#include <math.h>

SWF_FILE::SWF_FILE()
{
	m_pFileData		= NULL;
	m_byteOffset	= 0;
	m_bitOffset		= 0;
	m_uiFileSize	= 0;
	m_bLoaded		= false;
}

SWF_FILE::~SWF_FILE()
{
	if(m_pFileData)
	{
		delete m_pFileData;
		m_pFileData = NULL;
	}
	
	m_bLoaded = false;
}

int SWF_FILE::LoadSWF(const char* path)
{
	std::ifstream fSwfFile;
	fSwfFile.open(path, std::ifstream::in | std::ifstream::binary);

	if(!fSwfFile.is_open())
	{
#ifdef _DEBUG
		std::cerr << "Error - Failed to load SWF: \"" << path << "\"" << std::endl;
#endif
		m_bLoaded = false;
		return -1;
	}
	
	fSwfFile.seekg (0, std::ios::end);
	m_uiFileSize = fSwfFile.tellg();
	fSwfFile.seekg (0, std::ios::beg);

	m_pFileData = new char[m_uiFileSize];
	fSwfFile.read(m_pFileData, m_uiFileSize);
	fSwfFile.close();
	
	m_bLoaded = true;
	
	return 0;
}

//	NOTE: GetByte assumes the data is byte aligned.
void SWF_FILE::GetBytes(char* dataOut, const unsigned int numBytes)
{
	//	If there's a bit offset currently set then we can assume
	//	that GetBites was recently called.
	
	//	NOTE: Not sure if I'm making the right assumption here, but
	//		  we're assuming m_bitOffset should be no larger than 7.
	
	if(m_bitOffset > 0)
		m_byteOffset++;
	
	memcpy(dataOut, (char*)(m_pFileData+m_byteOffset), numBytes);
	m_byteOffset+=numBytes;
	
	//	If GetBits was recently called then there's going
	//	to be some excess padding. A simple fix is to reverse
	//	the byte order.
	if(m_bitOffset > 0)
	{
		m_bitOffset = 0;
		
		unsigned int k = numBytes - 1;
		
		//	reverse the byte order.
		while(k != 0)
		{
			for(unsigned int i = 0; i < k; ++i)
			{
				char d = *(dataOut+i);
				*(dataOut+i) = *(dataOut+i+1);
				*(dataOut+i+1) = d;
			}
			--k;
		}
	}
}

void SWF_FILE::GetBits(char* dataOut, const unsigned int numBits)
{
	unsigned int numBytes = (unsigned int)ceil(numBits / 8.0f);
	memset(dataOut, 0, numBytes);
	
	int offs = m_bitOffset;
	int srcByteOffs = m_byteOffset + (numBytes - 1);
	int	srcBitOffs = (numBytes * 8) - (numBits + m_bitOffset);
	
	// If the offset is greater than the number of dest bytes required, then
	// the bits extend across nBytes + 1 source bytes. Adjust by adding 1 to
	// the source byte offset, then taking the absolute difference of the bit 
	// offset to obtain the source bit offset
	if(srcBitOffs < 0)
	{
		srcByteOffs += 1;
		srcBitOffs += 8;
	}
	
	// Set source buffer bit/byte locators //
	m_bitOffset += numBits;
	m_byteOffset = srcByteOffs;

	// Grab the current read byte //
	char srcByte = m_pFileData[srcByteOffs];
	char srcMask;
	int destByte = 0;
	int destBit = 0;
	int bitCount = 0;
	
	while(bitCount < numBits)
	{
		srcMask = (srcBitOffs == 0) ? 0x01 : (2 << (srcBitOffs - 1));
		if(srcByte & srcMask)
			dataOut[destByte] |= (destBit == 0) ? 0x01 : (2 << (destBit - 1));
			
		++srcBitOffs;
		++bitCount;
		++destBit;
		
		// Need another source byte //
		if(srcBitOffs > 7)
		{
			--srcByteOffs;
			srcByte = m_pFileData[srcByteOffs];
			srcBitOffs = 0;
		}
		
		// Write to the next dest byte //
		if(destBit > 7)
		{
			destByte++;
			destBit = 0;
		}
	}

	if(m_bitOffset>7)
		m_bitOffset = (m_bitOffset%8);
}

void SWF_FILE::SetByteOffset(const unsigned int offset)
{
	m_byteOffset = offset;
}

void SWF_FILE::SetBitOffset(const unsigned int offset)
{
	m_bitOffset = offset;
}

bool SWF_FILE::IsLoaded()
{
	return m_bLoaded;
}

//	HACK: This isn't complete.
unsigned int GetEncodedU32(SWF_FILE* file)
{
	unsigned char encoded;
	file->GetBytes((char*)&encoded, sizeof(unsigned char));
	if((encoded & 0x80) == 0)
		return encoded;
	
	return 0;
}

SWF::SWF()
{
	m_bIsEnd		= false;
	
	m_pFile			= NULL;
	m_pHeader		= NULL;
	m_pAttributes	= NULL;
	m_pSceneAndFrameLabelData = NULL;
}

SWF::~SWF()
{
	if(m_pFile)
	{
		delete m_pFile;
		m_pFile = NULL;
	}

	if(m_pHeader)
	{
		if(m_pHeader->rect)
		{
			delete m_pHeader->rect;
			m_pHeader->rect = NULL;
		}
		delete m_pHeader;
		m_pHeader = NULL;
	}
	
	if(m_pAttributes)
	{
		delete m_pAttributes;
		m_pAttributes = NULL;
	}
	
	if(m_pSceneAndFrameLabelData)
	{
		delete m_pSceneAndFrameLabelData;
		m_pSceneAndFrameLabelData = NULL;
	}
}

int SWF::LoadSWF(const char* path)
{
	m_pFile = new SWF_FILE;
	m_pFile->LoadSWF(path);
	
	if(!m_pFile->IsLoaded())
		return -1;
	
	LoadHeader(m_pFile);

	while(!m_bIsEnd)
		LoadTag(m_pFile);
	
	return 0;
}

int SWF::LoadHeader(SWF_FILE* file)
{
	bool valid = false;
	
	m_pFile->SetByteOffset(0);
	m_pFile->SetBitOffset(0);
	
	m_pHeader = new SWF_HEADER;
	
	m_pFile->GetBytes((char*)&m_pHeader->signature[0]);
	m_pFile->GetBytes((char*)&m_pHeader->signature[1]);
	m_pFile->GetBytes((char*)&m_pHeader->signature[2]);
	
	if(m_pHeader->signature[1]=='W' && m_pHeader->signature[2]=='S')
	{
		if(m_pHeader->signature[0]=='C')
		{
			std::cout << "This flash is compressed, please uncompress and try again. Exiting..." << std::endl;
			valid = false;
		}
		else
		if(m_pHeader->signature[0]=='F')
			valid = true;
		else
		{
			std::cout << "Unknown file format, exiting..." << std::endl;
			valid = false;
		}
	}
	
	if(!valid)
		return -1;
	
	m_pFile->GetBytes((char*)&m_pHeader->version);
	m_pFile->GetBytes((char*)&m_pHeader->fileLength, sizeof(unsigned int));
	
	m_pHeader->rect = new SWF_RECT;
	
	GetRect(m_pHeader->rect);

	m_pFile->GetBytes((char*)&m_pHeader->fps, sizeof(unsigned short));
	m_pFile->GetBytes((char*)&m_pHeader->numFrames, sizeof(unsigned short));
	
	return 0;
}

int SWF::LoadTag(SWF_FILE *file)
{
	SWF_TAG tagHeader;
	unsigned short tagType;
	unsigned short tagLength;
	
	file->GetBytes((char*)&tagHeader.tagCodeAndLength, sizeof(unsigned short));
	tagType = tagHeader.tagCodeAndLength >> 6;
	tagLength = tagHeader.tagCodeAndLength & 0x3F;

	unsigned int tagLengthLong = 0;
	bool isLongTag = false;
	if(tagLength == 0x3F)
 	{
 		isLongTag = true;
 		file->GetBytes((char*)&tagLengthLong, sizeof(unsigned int));
 	}
 	
 	switch(tagType)
 	{
 		case TAG_END:
 			m_bIsEnd = true;
 			break;
 		case TAG_DEFINE_SHAPE:
 			LoadDefineShapeTag(file);
 			break;
 		case TAG_SET_BACKGROUND_COLOR:		
 			unsigned char red, green, blue;
 			file->GetBytes((char*)&red, sizeof(char));
 			file->GetBytes((char*)&green, sizeof(char));
 			file->GetBytes((char*)&blue, sizeof(char));
#ifdef _DEBUG
 			std::cout << "Dumping Tag[SetBackgroundColor]..." << std::endl;
 			std::cout << "Tag[FileAttributes]:RGB:Red\t= " << (unsigned int)red << std::endl;
 			std::cout << "Tag[FileAttributes]:RGB:Green\t= " << (unsigned int)green << std::endl;
 			std::cout << "Tag[FileAttributes]:RGB:Blue\t= " << (unsigned int)blue << std::endl;
#endif
 			break;
 		case TAG_FILE_ATTRIBUTES:
 			LoadFileAttributesTag(file);
 			break;
 		case TAG_METADATA:
#ifdef _DEBUG
 			std::cout << "Dumping Tag[Metadata]..." << std::endl;
#endif
 			char* data;
 			if(isLongTag)
 			{
 				data = new char[tagLengthLong];
 				file->GetBytes((char*)data, sizeof(char)*tagLengthLong);
#ifdef _DEBUG
 				std::cout << data << std::endl;
#endif
 			}
 
 			delete []data;
 			break;
 		case TAG_DEFINE_SCENE_AND_FRAME_LABEL_DATA:
 			LoadDefSceneAndFrameLabelTag(file);
 			break;
 		default:
#ifdef _DEBUG
 			std::cout << "Unsupported Tag Type:" << tagType << "\t\t" << "Tag Length: " << tagLength;
#endif
 			unsigned int pos = file->GetByteOffset();
 
 			if(isLongTag)
 				file->SetByteOffset(pos + tagLengthLong);
 			else
 				file->SetByteOffset(pos + tagLength);
 			break;
 	}

#ifdef _DEBUG	
 	std::cout << std::endl;
#endif
 
	return 0;
}

int SWF::LoadFileAttributesTag(SWF_FILE* file)
{
	m_pAttributes = new SWF_FILE_ATTRIBUTES;

	unsigned int fileAttributes = 0;
	file->GetBytes((char*)&fileAttributes, sizeof(unsigned int));
	
	bool useNetwork			= fileAttributes & 0x80;
	bool useActionScript3	= fileAttributes & 0x10;
	bool hasMetaData		= fileAttributes & 0x08;
	bool useGPU				= fileAttributes & 0x04;
	bool useDirectBlit		= fileAttributes & 0x02;
	
	m_pAttributes->useNetwork		= useNetwork;
	m_pAttributes->useActionScript3 = useActionScript3;
	m_pAttributes->hasMetaData		= hasMetaData;
	m_pAttributes->useGPU			= useGPU;
	m_pAttributes->useDirectBlit	= useDirectBlit;
	
#ifdef _DEBUG
	std::cout << "Dumping Tag[FileAttributes]..." << std::endl;
	std::cout << "Tag[FileAttributes]:UseDirectBlit\t= " << useDirectBlit << std::endl;
	std::cout << "Tag[FileAttributes]:UseGPU\t\t= " << useGPU << std::endl;
	std::cout << "Tag[FileAttributes]:HasMetadata\t\t= " << hasMetaData << std::endl;
	std::cout << "Tag[FileAttributes]:ActionScript3\t= " << useActionScript3 << std::endl;
	std::cout << "Tag[FileAttributes]:UseNetwork\t\t= " << useNetwork << std::endl;
	std::cout << std::endl;
#endif

	return 0;
}

//	HACK: This isn't complete.
int SWF::LoadDefSceneAndFrameLabelTag(SWF_FILE* file)
{
	m_pSceneAndFrameLabelData = new SWF_DEFINE_SCENE_AND_FRAME_LABEL_DATA;
	
	m_pSceneAndFrameLabelData->SceneCount = GetEncodedU32(file);
	
	for(unsigned int i = 0; i < m_pSceneAndFrameLabelData->SceneCount; ++i)
	{
		std::string name;
		
		unsigned char charbuff;
		
		m_pSceneAndFrameLabelData->FrameOffsets.push_back(GetEncodedU32(file));
			
		file->GetBytes((char*)&charbuff, sizeof(unsigned char));
		while(charbuff != 0)
		{
			name.push_back(charbuff);
			file->GetBytes((char*)&charbuff, sizeof(unsigned char));
		}
		
		m_pSceneAndFrameLabelData->Names.push_back(std::string(name));		
	}
	
	m_pSceneAndFrameLabelData->FrameLabelCount = GetEncodedU32(file);
	for(unsigned int i = 0; i < m_pSceneAndFrameLabelData->FrameLabelCount; ++i)
	{
		std::string label;
		
		unsigned char charbuff;
		
		m_pSceneAndFrameLabelData->FrameNums.push_back(GetEncodedU32(file));
		
		file->GetBytes((char*)&charbuff, sizeof(unsigned char));
		while(charbuff != 0)
		{
			label.push_back(charbuff);
			file->GetBytes((char*)&charbuff, sizeof(unsigned char));
		}
		
		m_pSceneAndFrameLabelData->FrameLabels.push_back(std::string(label));
	}
	
	return 0;
}

void SWF::GetRect(SWF_RECT* rect)
{
	m_pFile->GetBits((char*)&rect->Nbits, 5);

	unsigned int numBytes = (unsigned int)ceil(m_pHeader->rect->Nbits / 8.0f);

	char signed *xMin = new signed char[numBytes];
	char signed *xMax = new signed char[numBytes];
	char signed *yMin = new signed char[numBytes];
	char signed *yMax = new signed char[numBytes];
	
	m_pFile->GetBits((char*)xMin, (unsigned int)m_pHeader->rect->Nbits);
	m_pFile->GetBits((char*)xMax, (unsigned int)m_pHeader->rect->Nbits);
	m_pFile->GetBits((char*)yMin, (unsigned int)m_pHeader->rect->Nbits);
	m_pFile->GetBits((char*)yMax, (unsigned int)m_pHeader->rect->Nbits);

	memcpy(&rect->Xmin, xMin, numBytes);
	memcpy(&rect->Xmax, xMax, numBytes);
	memcpy(&rect->Ymin, yMin, numBytes);
	memcpy(&rect->Ymax, yMax, numBytes);

	delete []xMin;
	delete []xMax;
	delete []yMin;
	delete []yMax;
}

void SWF::GetFillStyles(SWF_FILL_STYLE_ARRAY* fillStyleArray)
{
	m_pFile->GetBytes((char*)&fillStyleArray->fillStyleCount);
	
	unsigned int fillStyleCount = fillStyleArray->fillStyleCount;
	
	if(fillStyleArray->fillStyleCount == 0xFF)
	{
		m_pFile->GetBytes((char*)&fillStyleArray->fillStyleCountExtended, sizeof(unsigned short));
		fillStyleCount = fillStyleArray->fillStyleCountExtended;
	}
	
	for(unsigned int i = 0; i < fillStyleCount; ++i)
	{
		unsigned char fillStyleType = 1;
		m_pFile->GetBytes((char*)&fillStyleType);
	}
}

void SWF::GetLineStyles(SWF_LINE_STYLE_ARRAY* lineStyleArray)
{
	m_pFile->GetBytes((char*)&lineStyleArray->lineStyleCount);
	
	unsigned int lineStyleCount = lineStyleArray->lineStyleCount;
	
	if(lineStyleArray->lineStyleCount == 0xFF)
	{
		m_pFile->GetBytes((char*)lineStyleArray->lineStyleCountExtended, sizeof(unsigned short));
		lineStyleCount = lineStyleArray->lineStyleCountExtended;
	}
	
	for(unsigned int i = 0; i < lineStyleCount; ++i)
	{
		unsigned short width = 0;
		m_pFile->GetBytes((char*)&width, sizeof(unsigned short));
		
		SWF_RGB color;
		m_pFile->GetBytes((char*)&color, sizeof(SWF_RGB));
	}
}

void SWF::GetShapeWithStyle(SWF_SHAPE_WITH_STYLE* shapeWithStyle)
{
	SWF_FILL_STYLE_ARRAY* pFillStyleArray = new SWF_FILL_STYLE_ARRAY;
	GetFillStyles(pFillStyleArray);
	
	SWF_LINE_STYLE_ARRAY* pLineStyleArray = new SWF_LINE_STYLE_ARRAY;
	GetLineStyles(pLineStyleArray);
	
	unsigned char numFillBits = 0;
	unsigned char numLineBits = 0;
	
	m_pFile->GetBits((char*)&numFillBits, 4);
	m_pFile->GetBits((char*)&numLineBits, 4);
	
	bool endOfShape = false;
	
	while(!endOfShape)
	{
		unsigned char typeFlag = 2;
		m_pFile->GetBits((char*)&typeFlag, 1);
		
		switch(typeFlag)
		{
			case 0:	// None-edge
			{	
				char flags = 0;
				m_pFile->GetBits(&flags, 5);
				if(flags == 0)
				{
					endOfShape = true;
					break;
				}
				else
				{
					
				}
				break;
			}
			case 1: // Edge record
				break;
			default:
				break;
		}
	}
	
	return;
}

int SWF::LoadDefineShapeTag(SWF_FILE* file)
{
	SWF_DEFINE_SHAPE* pDefineShape = new SWF_DEFINE_SHAPE;
	
	file->GetBytes((char*)&pDefineShape->shapeId, sizeof(unsigned short));
	
	SWF_RECT* pRect = new SWF_RECT;
	
	GetRect(pRect);
	
	SWF_SHAPE_WITH_STYLE* pShapeWithStyle = new SWF_SHAPE_WITH_STYLE;
	GetShapeWithStyle(pShapeWithStyle);
	
	return 0;
}

