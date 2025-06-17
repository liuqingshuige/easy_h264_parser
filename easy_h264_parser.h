/*
 * H264数据帧解析
 * Copyright FreeCode. All Rights Reserved.
 * MIT License (https://opensource.org/licenses/MIT)
 * 2025 by liuqingshuige
 */
#ifndef __FREE_EASY_H264_PARSER_H__
#define __FREE_EASY_H264_PARSER_H__
#include <stdio.h>
#include <vector>
#include <string>
using namespace std;

// 帧类型
#define NALU_TYPE_SLICE 1
#define NALU_TYPE_DPA 2
#define NALU_TYPE_DPB 3
#define NALU_TYPE_DPC 4
#define NALU_TYPE_IDR 5
#define NALU_TYPE_SEI 6
#define NALU_TYPE_SPS 7
#define NALU_TYPE_PPS 8
#define NALU_TYPE_AUD 9
#define NALU_TYPE_EOSEQ 10
#define NALU_TYPE_EOSTREAM 11
#define NALU_TYPE_FILL 12
#define READ_BUFF_SIZE (512*1024)


// 位操作：用于解析SPS帧信息
class BitStream
{
public:
	BitStream() = delete;
	BitStream(unsigned char *buf, int len)
	{
		start = buf; p = buf; size = len;
	}
	~BitStream()
	{}

	/* 读取1bit */
	int ReadU1();

	/* 读取n个bit */
	int ReadU(int n);

	/* 解码：无符号指数哥伦布熵编码 */
	int ReadUE();

	/* 解码：无符号指数哥伦布熵编码，与ReadUE()功能一样 */
	int ReadUE1();

	/* 解码：有符号指数哥伦布熵编码 */
	int ReadSE();

	/* 解码：有符号指数哥伦布熵编码，与ReadSE()功能一样 */
	int ReadSE1();

private:
	unsigned char *start = 0; // ptr of buffer
	int size = 0; // length of buffer in byte
	unsigned char *p = 0; // 当前读取的字节
	int bits_left = 8; // 当前字节中的第几位
};

// NALU：不包含起始码
typedef struct Nalu
{
	/* EBSP:不包含起始码;RBSP:EBSP去掉防竞争字节;SODB:RBSP去掉补齐数据 */
	Nalu()
	{
		pdata = 0; length = type = 0;
	}
	~Nalu()
	{}

	/* 获取nalu数据，不包含startcode，即EBSP */
	unsigned char *GetData()
	{
		return pdata;
	}

	/* 获取nalu数据长度 */
	int GetLength()
	{
		return length;
	}

	/* 获取nalutype，值为:GetData()&0x1f */
	int GetNaluType()
	{
		return type;
	}

	int GetForbiddenBit()
	{
		return forbidden_bit;
	}

	int GetNalRefIdc()
	{
		return nal_ref_idc;
	}

	/* data不包含startcode */
	void SetData(unsigned char *data, int len)
	{
		if (data && len > 0)
		{
			length = len;
			pdata = data;
			type = pdata[0] & 0x1f;
			forbidden_bit = (pdata[0] >> 7) & 0x1;
			nal_ref_idc = (pdata[0] >> 5) & 0x3;
		}
	}

	/* RBSP:EBSP去掉防竞争字节 */
	bool GetRBSP(std::vector<unsigned char> &rbsp)
	{
		if (!pdata || length <= 3)
			return false;
		rbsp.clear();
		for (int i = 0; i < length; i++)
		{
			if (pdata[i] == 0x03)
			{
				if (i > 2) // 检查前面2个字节是否是 0x00 0x00
				{
					if (pdata[i - 1] == 0x00 && pdata[i - 2] == 0x00)
					{
						if (i < length - 1) // 检查后一个字节是否是 0x00 0x01 0x02 0x03
						{
							if (pdata[i + 1] == 0x00
								|| pdata[i + 1] == 0x01
								|| pdata[i + 1] == 0x02
								|| pdata[i + 1] == 0x03)
							{
								continue; // 这个是防竞争字节
							}
						}
					}
				}
			}
			rbsp.push_back(pdata[i]);
		}
		return true;
	}

	unsigned char *pdata;
	int length;
	int type, forbidden_bit, nal_ref_idc;
}Nalu;

// 起始码信息
typedef struct StartCodeInfo
{
	int startCodeIndex;
	int startCodeLen;
}StartCodeInfo;

// NALU解析
class NaluParse
{
public:
	NaluParse()
	{
		stream = 0; len = 0; Nalus.clear();
	}
	~NaluParse()
	{
		if (stream) delete stream;
	}

	/* 解析h264流 */
	std::vector<Nalu> &GetNalusFromFrame(const unsigned char *h264Frame, const int h264FrameLen, int *lastFrameIndex = 0);

private:
	unsigned char *stream;
	int len;
	std::vector<Nalu> Nalus; // EBSP:不包含起始码;RBSP:EBSP去掉防竞争字节;SODB:RBSP去掉补齐数据
};

// H264文件解析
class H264FileParse
{
public:
	H264FileParse() = delete;
	H264FileParse(const std::string &filename);
	~H264FileParse();

	bool GetNextNalu(Nalu &nalu);

	H264FileParse &operator=(const H264FileParse &b) = delete;

private:
	FILE *fp;
	NaluParse *parser;
	unsigned char *stream;

	int realReadSize; // 上一次实际读取的字节数
	int lastFrameIndex; // 上一次解析的最后一帧的起始位置
	std::vector<Nalu> Nalus;
};

// SPS帧信息解析
class NaluSpsParse
{
	/*
	seq_parameter_set_data()	C	Descriptor
		profile_idc	            0	u(8)
		constraint_set0_flag	0	u(1)
		constraint_set1_flag	0	u(1)
		constraint_set2_flag	0	u(1)
		constraint_set3_flag	0	u(1)
		constraint_set4_flag	0	u(1)
		constraint_set5_flag	0	u(1)
		reserved_zero_2bits	    0	u(2)
		level_idc	            0	u(8)
		seq_parameter_set_id	0	ue(8)

	profile_idc:
	Baseline profile 66
	Main profile 77
	Extended profile 88
	High profile 100
	High 10 profile 110
	High 4:2:2 profile 122
	High 4:4:4 Predictive profile 244
	High 10 Intra profile 100 or 110
	High 4:2:2 Intra profile 100, 110, or 122
	High 4:4:4 Intra profile 44, 100, 110, 122, or 244
	CAVLC 4:4:4 Intra profile 44
	*/
public:
	NaluSpsParse() = delete;
	NaluSpsParse(unsigned char *sps, int len);

	~NaluSpsParse()
	{
		if (stream) delete stream; stream = 0;
		if (bs) delete bs; bs = 0;
		if (offset_for_ref_frame) delete offset_for_ref_frame; offset_for_ref_frame = 0;
	}

	NaluSpsParse &operator=(const NaluSpsParse &b) = delete;

	unsigned char GetProfileIdc()
	{
		return profile_idc;
	}

	unsigned char GetLevelIdc()
	{
		return level_idc;
	}

	unsigned char GetChromaFormatIdc()
	{
		return chroma_format_idc;
	}

	bool GetWidthHeight(int &width, int &height);

	bool GetRealWidthHeight(int &width, int &height);

private:
	BitStream *bs = 0;
	unsigned char *stream = 0;
	int length = 0;
	unsigned char profile_idc = 0;
	unsigned char constraint_set_flag = 0;
	unsigned char level_idc = 0;
	int seq_parameter_set_id;
	int chroma_format_idc = 1; // 码流格式，0：Y，1：YUV420，2：YUV422，3：YUV444
	unsigned char separate_colour_plane_flag = 0; // 0: UV依附于Y，1：UV与Y分开编码
	int bit_depth_luma_minus8 = 0;
	int bit_depth_chroma_minus8 = 0;
	unsigned char qpprime_y_zero_transform_bypass_flag = 0;
	unsigned char seq_scaling_matrix_present_flag = 0;
	unsigned char seq_scaling_list_present_flag[12];
	int log2_max_frame_num_minus4 = 0;
	int pic_order_cnt_type = 0;
	int log2_max_pic_order_cnt_lsb_minus4 = 0;
	unsigned char delta_pic_order_always_zero_flag = 0;
	int offset_for_non_ref_pic = 0, offset_for_top_to_bottom_field = 0;
	int num_ref_frames_in_pic_order_cnt_cycle = 0;
	int *offset_for_ref_frame = 0;
	int max_num_ref_frames;
	unsigned char gaps_in_frame_num_value_allowed_flag;
	int pic_width_in_mbs_minus1 = -1, pic_height_in_map_units_minus1 = -1;
	unsigned char frame_mbs_only_flag = 1; // 1：帧编码，0：场编码
	unsigned char mb_adaptive_frame_field_flag;
	unsigned char direct_8x8_inference_flag, frame_cropping_flag;
	int frame_crop_left_offset = 0, frame_crop_right_offset = 0;
	int frame_crop_top_offset = 0, frame_crop_bottom_offset = 0;
	unsigned char vui_parameters_present_flag;
	int chroma_array_type = 0;
	int sub_width_c = 2, sub_height_c = 2; // 表示YUV分量中，Y分量和UV分量在水平和竖直方向上的比值
};

// PPS帧信息解析
class NaluPpsParse
{
public:
	NaluPpsParse() = delete;
	NaluPpsParse(unsigned char *pps, int len);

	~NaluPpsParse()
	{
		if (stream) delete stream;
		if (bs) delete bs;
	}

	// 获取PPS参数
	int GetPicParameterSetId()
	{
		return pic_parameter_set_id;
	}
	int GetSeqParameterSetId()
	{
		return seq_parameter_set_id;
	}
	bool GetEntropyCodingModeFlag()
	{
		return entropy_coding_mode_flag;
	}
	bool GetBottomFieldPicOrderInFramePresentFlag()
	{
		return bottom_field_pic_order_in_frame_present_flag;
	}
	int GetNumSliceGroupsMinus1()
	{
		return num_slice_groups_minus1;
	}
	int GetSliceGroupMapType()
	{
		return slice_group_map_type;
	}
	int GetNumRefIdxL0DefaultActiveMinus1()
	{
		return num_ref_idx_l0_default_active_minus1;
	}
	int GetNumRefIdxL1DefaultActiveMinus1()
	{
		return num_ref_idx_l1_default_active_minus1;
	}
	bool GetWeightedPredFlag()
	{
		return weighted_pred_flag;
	}
	int GetWeightedBipredIdc()
	{
		return weighted_bipred_idc;
	}
	int GetPicInitQpMinus26()
	{
		return pic_init_qp_minus26;
	}
	int GetPicInitQsMinus26()
	{
		return pic_init_qs_minus26;
	}
	int GetChromaQpIndexOffset()
	{
		return chroma_qp_index_offset;
	}
	bool GetDeblockingFilterControlPresentFlag()
	{
		return deblocking_filter_control_present_flag;
	}
	int GetDisableDeblockingFilterIdc()
	{
		return disable_deblocking_filter_idc;
	}
	int GetSliceAlphaC0OffsetDiv2()
	{
		return slice_alpha_c0_offset_div2;
	}
	int GetSliceBetaOffsetDiv2()
	{
		return slice_beta_offset_div2;
	}
	bool GetConstrainedIntraPredFlag()
	{
		return constrained_intra_pred_flag;
	}
	bool GetRedundantPicCntPresentFlag()
	{
		return redundant_pic_cnt_present_flag;
	}

private:
	BitStream *bs = 0;
	unsigned char *stream = 0;
	int length = 0;

	// PPS参数
	int pic_parameter_set_id;                     // ue(v)
	int seq_parameter_set_id;                     // ue(v)
	bool entropy_coding_mode_flag;                // u(1)
	bool bottom_field_pic_order_in_frame_present_flag; // u(1)
	int num_slice_groups_minus1;                  // ue(v)
	int slice_group_map_type;                     // ue(v)
	std::vector<int> run_length_minus1;           // ue(v)
	std::vector<int> top_left;                    // ue(v)
	std::vector<int> bottom_right;                // ue(v)
	int slice_group_change_direction_flag;        // u(1)
	int slice_group_change_rate_minus1;           // ue(v)
	int pic_size_in_map_units_minus1;             // ue(v)
	std::vector<int> slice_group_id;              // u(v)
	int num_ref_idx_l0_default_active_minus1;     // ue(v)
	int num_ref_idx_l1_default_active_minus1;     // ue(v)
	bool weighted_pred_flag;                      // u(1)
	int weighted_bipred_idc;                      // u(2)
	int pic_init_qp_minus26;                      // se(v)
	int pic_init_qs_minus26;                      // se(v)
	int chroma_qp_index_offset;                   // se(v)
	bool deblocking_filter_control_present_flag;  // u(1)
	int disable_deblocking_filter_idc;            // ue(v)
	int slice_alpha_c0_offset_div2;               // se(v)
	int slice_beta_offset_div2;                   // se(v)
	bool constrained_intra_pred_flag;             // u(1)
	bool redundant_pic_cnt_present_flag;          // u(1)
};



#endif

