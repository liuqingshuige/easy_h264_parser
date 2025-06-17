#include "easy_h264_parser.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline const char *get_time()
{
	static char temp[32] = { 0 };
	struct timeval tv;
	gettimeofday(&tv, 0);
	snprintf(temp, sizeof(temp), "%zu.%03zu", tv.tv_sec, tv.tv_usec / 1000);
	return temp;
}
#define LOG(fmt, ...) printf("[%s %s:%d] " fmt, get_time(), __FUNCTION__, __LINE__, ##__VA_ARGS__)


int GetFileSize(FILE *pFile)
{
	if (!pFile)
		return 0;
	fseek(pFile, 0L, SEEK_END);
	int size = ftell(pFile);
	fseek(pFile, 0L, SEEK_SET);
	return size;
}

// 加载文件, 返回文件内容，需要手动释放
char *LoadFile(const char *pwsFilePath, int *pdwSize)
{
	if (!pwsFilePath || !pdwSize)
		return NULL;

	*pdwSize = 0;
	FILE *pFile = fopen(pwsFilePath, "rb");
	if (NULL == pFile)
		return NULL;

	int iSize = GetFileSize(pFile);
	LOG("%s Size = %d\n", pwsFilePath, iSize);

	if (-1 == iSize || 0 == iSize)
	{
		fclose(pFile);
		return NULL;
	}

	char *pFileContent = (char *)malloc(iSize + 1);
	if (pFileContent == NULL)
	{
		fclose(pFile);
		return NULL;
	}

	fseek(pFile, 0, SEEK_SET);
	int len = fread(pFileContent, 1, iSize, pFile);
	*pdwSize = len;
	if (*pdwSize == iSize)
	{
		pFileContent[iSize] = 0;
		fclose(pFile);
		return pFileContent;
	}
	LOG("read file %s fail\n", pwsFilePath);
	fclose(pFile);
	free(pFileContent);
	pFileContent = NULL;
	return NULL;
}


int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("Usage: \n\t%s <input.h264>\n", argv[0]);
		return -1;
	}

	// 读取H264文件数据，并解析出所有帧
	NaluParse parse;
	int len = 0;
	int last_pos = -1;
	char *pdata = LoadFile(argv[1], &len);

	if (pdata)
	{
		std::vector<Nalu> &frames = parse.GetNalusFromFrame((unsigned char *)pdata, len, &last_pos);
		LOG("frame count: %lu, last_pos: %d\n\n", frames.size(), last_pos);
		for (int i = 0; i < frames.size(); i++)
		{
			int type = frames[i].GetNaluType();
			int Size = frames[i].GetLength();
			LOG("idx: %d, type: 0x%x, Size: %d\n", i, type, Size);
#if 0
			if (type == NALU_TYPE_SPS)
			{
				int width, height, width1, height1;
				NaluSpsParse sps(frames[i].GetData(), frames[i].GetLength());
				int format_idc = sps.GetChromaFormatIdc();
				int level_idc = sps.GetLevelIdc();
				int profile_idc = sps.GetProfileIdc();
				sps.GetWidthHeight(width, height);
				sps.GetRealWidthHeight(width1, height1);
				LOG("format: %d, level: %d, profile: %d, w: %d(%d), h: %d(%d)\n",
					format_idc, level_idc, profile_idc,
					width, width1, height, height1);
			}

			if (type == NALU_TYPE_PPS)
			{
				NaluPpsParse pps(frames[i].GetData(), frames[i].GetLength());
				int pps_id = pps.GetPicParameterSetId();
				int sps_id = pps.GetSeqParameterSetId();
				int mode = pps.GetEntropyCodingModeFlag();
				LOG("pps: %d, sps: %d, mode: %d\n", pps_id, sps_id, mode);
			}
#endif
		}
		free(pdata);
	}
	
	printf("\n=====H264FileParse result=====\n\n");
	
	H264FileParse h264(argv[1]);
	Nalu nalu;
	int idx = 0;
	while (h264.GetNextNalu(nalu))
	{
		int type = nalu.GetNaluType();
		int Size = nalu.GetLength();
		LOG("idx: %d, type: 0x%x, Size: %d\n", idx++, type, Size);
	}

	return 0;
}

