/*
 * H264数据帧解析实现
 * Copyright FreeCode. All Rights Reserved.
 * MIT License (https://opensource.org/licenses/MIT)
 * 2025 by liuqingshuige
 */
#include <stdlib.h>
#include <string.h>
#include "easy_h264_parser.h"

 //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
 // 位操作：用于解析SPS帧信息
 /* 读取1bit */
int BitStream::ReadU1()
{
	int r = 0;
	bits_left--;
	r = ((*(p)) >> bits_left) & 0x01;
	if (bits_left == 0)
	{
		p++;
		bits_left = 8;
	}
	return r;
}

/* 读取n个bit */
int BitStream::ReadU(int n)
{
	int r = 0, i;
	for (i = 0; i < n; i++)
	{
		int rval = ReadU1();
		int rshift = (n - i - 1);
		r |= (rval << rshift);
	}
	return r;
}

/* 解码：无符号指数哥伦布熵编码 */
int BitStream::ReadUE()
{
	int r = 0, i = 0;
	while ((ReadU1() == 0) && (i < size * 8/*32*/))
	{
		i++;
	}
	/* 上面已经把 00101 中的第一个1读取了，当前指向的是倒数第二个0 */
	r = ReadU(i); // 有效bit数是0的个数+1，本来是i+1，但1已经读了
	r += (1 << i) - 1;
	return r;
}

/* 解码：无符号指数哥伦布熵编码，与ReadUE()功能一样 */
int BitStream::ReadUE1()
{
	int r = 0, i = 0, zero = 0, shift = 0;
	while (i < size * 8)
	{
		i++;
		r = ReadU1();
		if (r)
		{
			shift = zero;
			r = r << shift;
			break;
		}
		else
			zero++;
	}
	for (int n = 0; n < zero; n++) // 读取剩余位
	{
		int tmp = ReadU1();
		tmp = tmp << (shift - n - 1);
		r += tmp;
	}
	r -= 1; // 减去1得到真实值
	return r;
}

/* 解码：有符号指数哥伦布熵编码 */
int BitStream::ReadSE()
{
	int r = ReadUE();
	if (r & 0x1)
		r = (r + 1) / 2;
	else
		r = -(r / 2);
	return r;
}

/* 解码：有符号指数哥伦布熵编码，与ReadSE()功能一样 */
int BitStream::ReadSE1()
{
	int r = 0, i = 0, zero = 0, shift = 0;
	while (i < size * 8)
	{
		i++;
		r = ReadU1();
		if (r)
		{
			shift = (i - 2);
			r = r << shift;
			break;
		}
		else
			zero++;
	}
	for (int n = 0; n < zero - 1; n++) // 读取剩余位
	{
		int tmp = ReadU1();
		tmp = tmp << (shift - n - 1);
		r += tmp;
	}
	int sign = ReadU1(); // 读取符号位：0正1负
	return sign ? -r : r;
}

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// NALU解析
/* 解析h264流 */
std::vector<Nalu> &NaluParse::GetNalusFromFrame(const unsigned char *h264Frame, const int h264FrameLen, int *lastFrameIndex)
{
	Nalus.clear();
	if (h264Frame && h264FrameLen > 3)
	{
		if (stream)
			delete stream;

		if (lastFrameIndex)
			*lastFrameIndex = -1;

		len = h264FrameLen;
		stream = new unsigned char[len];
		memcpy(this->stream, h264Frame, len);

		int i = 0, startCode = -1;
		std::vector<StartCodeInfo> startCodeIdx; // 保存每一帧的起始码下标索引
		for (i = 0; i < h264FrameLen - 4;)
		{
			if (stream[i] == 0 && stream[i + 1] == 0
				&& stream[i + 2] == 1)
			{
				startCode = i + 3;
			}
			else if (stream[i] == 0 && stream[i + 1] == 0
				&& stream[i + 2] == 0 && stream[i + 3] == 1)
			{
				startCode = i + 4;
			}

			/* 得到起始码 */
			if (startCode > 0)
			{
				StartCodeInfo info;
				info.startCodeIndex = i;
				info.startCodeLen = (startCode - i);
				startCodeIdx.push_back(info);

				if (lastFrameIndex) // 记录最后一帧的起始码位置
					*lastFrameIndex = i;

				i = startCode;
				startCode = -1; // 继续查找下一帧
			}
			else
				i++;
		}

		if (startCodeIdx.size() > 0)
		{
			for (int n = 0; n < startCodeIdx.size(); n++)
			{
				int plen = h264FrameLen;
				if (n == startCodeIdx.size() - 1)
					plen = h264FrameLen;
				else
					plen = startCodeIdx[n + 1].startCodeIndex;

				Nalu packet;
				packet.SetData(
					stream + startCodeIdx[n].startCodeIndex + startCodeIdx[n].startCodeLen,
					plen - startCodeIdx[n].startCodeIndex - startCodeIdx[n].startCodeLen);
				Nalus.push_back(packet);
			}
		}
	}
	return Nalus;
}

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// H264文件解析
H264FileParse::H264FileParse(const std::string &filename)
{
	fp = fopen(filename.c_str(), "rb");

	realReadSize = 0;
	lastFrameIndex = 0;

	parser = NULL;
	stream = NULL;

	Nalus.clear();
	
	if (fp)
	{
		parser = new NaluParse();
		stream = new unsigned char [READ_BUFF_SIZE];
	}
}

H264FileParse::~H264FileParse()
{
	if (fp) fclose(fp); fp = NULL;
	if (parser) delete parser; parser = NULL;
	if (stream) delete stream; stream = NULL;
}

// 获取一帧NALU
bool H264FileParse::GetNextNalu(Nalu &nalu)
{
	if (fp)
	{
		if (Nalus.size() > 0)
		{
			nalu = Nalus[0];
			Nalus.erase(Nalus.begin());
			return true;
		}

		/* 读取文件数据 */
		int left = (realReadSize - lastFrameIndex) > 0 ? (realReadSize - lastFrameIndex) : 0;
		memmove(stream, stream + lastFrameIndex, left); // 将上一次解析后剩余的数据移动到前面

		realReadSize = fread(stream + left, 1, READ_BUFF_SIZE - left, fp);
		if (realReadSize == 0)
			return false;

		realReadSize += left;
		Nalus = parser->GetNalusFromFrame(stream, realReadSize, &lastFrameIndex);
		if (Nalus.size() > 0)
		{
			nalu = Nalus[0];
			Nalus.erase(Nalus.begin());

			/* 将最后一帧去掉，避免重复获取，因为下次解析时会从这一帧(lastFrameIndex)开始 */
			if ((Nalus.size() > 0) && (!feof(fp)))
				Nalus.erase(Nalus.end() - 1);
			
			return true;
		}
	}

	return false;
}

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// SPS帧信息解析
NaluSpsParse::NaluSpsParse(unsigned char *sps, int len)
{
	int startCodeLen = 0;
	if (sps && len > 3)
	{
		/* 如果包含起始码，则跳过起始码 */
		if (sps[0] == 0 && sps[1] == 0 && sps[2] == 1)
		{
			startCodeLen = 3;
		}
		else if (sps[0] == 0 && sps[1] == 0
			&& sps[2] == 0 && sps[3] == 1)
		{
			startCodeLen = 4;
		}

		length = len - startCodeLen;
		stream = new unsigned char[length];
		memcpy(stream, sps + startCodeLen, length);

		/* init bit stream */
		bs = new BitStream(stream + 1, length - 1); // 跳过头字节

		/* 获取SPS信息 */
		profile_idc = bs->ReadU(8);
		constraint_set_flag = bs->ReadU(8);
		level_idc = bs->ReadU(8);
		seq_parameter_set_id = bs->ReadUE1();

		if (profile_idc == 100 || profile_idc == 110
			|| profile_idc == 122 || profile_idc == 244
			|| profile_idc == 44 || profile_idc == 83
			|| profile_idc == 86 || profile_idc == 118
			|| profile_idc == 128 || profile_idc == 138
			|| profile_idc == 139 || profile_idc == 134 || profile_idc == 135)
		{
			chroma_format_idc = bs->ReadUE1();
			if (chroma_format_idc == 3)
			{
				separate_colour_plane_flag = bs->ReadU1();
				if (separate_colour_plane_flag == 0)
					chroma_array_type = chroma_format_idc;
			}

			bit_depth_luma_minus8 = bs->ReadUE1();
			bit_depth_chroma_minus8 = bs->ReadUE1();
			qpprime_y_zero_transform_bypass_flag = bs->ReadU1();
			seq_scaling_matrix_present_flag = bs->ReadU1();

			if (seq_scaling_matrix_present_flag)
			{
				for (int i = 0; i < (chroma_format_idc != 3) ? 8 : 12; i++)
				{
					seq_scaling_list_present_flag[i] = bs->ReadU1();
				}
			}
		}

		/* 确定YUV比值 */
		if (chroma_array_type == 1)
		{
			sub_width_c = 2;
			sub_height_c = 2;
		}
		else if (chroma_array_type == 2)
		{
			sub_width_c = 2;
			sub_height_c = 1;
		}
		else if (chroma_array_type == 3)
		{
			sub_width_c = 1;
			sub_height_c = 1;
		}

		log2_max_frame_num_minus4 = bs->ReadUE1();
		pic_order_cnt_type = bs->ReadUE1();
		if (pic_order_cnt_type == 0)
			log2_max_pic_order_cnt_lsb_minus4 = bs->ReadUE1();
		else if (pic_order_cnt_type == 1)
		{
			delta_pic_order_always_zero_flag = bs->ReadU1();
			offset_for_non_ref_pic = bs->ReadSE1();
			offset_for_top_to_bottom_field = bs->ReadSE1();
			num_ref_frames_in_pic_order_cnt_cycle = bs->ReadUE1();

			if (num_ref_frames_in_pic_order_cnt_cycle > 0)
				offset_for_ref_frame = new int[num_ref_frames_in_pic_order_cnt_cycle];
			for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++)
				offset_for_ref_frame[i] = bs->ReadSE1();
		}

		max_num_ref_frames = bs->ReadUE1();
		gaps_in_frame_num_value_allowed_flag = bs->ReadU1();

		/* 图像宽高 */
		pic_width_in_mbs_minus1 = bs->ReadUE1();
		pic_height_in_map_units_minus1 = bs->ReadUE1();

		/* 确定编码方式 */
		frame_mbs_only_flag = bs->ReadU1();
		if (frame_mbs_only_flag == 0)
			mb_adaptive_frame_field_flag = bs->ReadU1();

		direct_8x8_inference_flag = bs->ReadU1();
		frame_cropping_flag = bs->ReadU1();
		if (frame_cropping_flag)
		{
			frame_crop_left_offset = bs->ReadUE1();
			frame_crop_right_offset = bs->ReadUE1();
			frame_crop_top_offset = bs->ReadUE1();
			frame_crop_bottom_offset = bs->ReadUE1();
		}

		vui_parameters_present_flag = bs->ReadU1();
	}
}

// 获取图像宽高信息
bool NaluSpsParse::GetWidthHeight(int &width, int &height)
{
	width = (pic_width_in_mbs_minus1 + 1) * 16;
	height = (pic_height_in_map_units_minus1 + 1) * 16;
	return true;
}

bool NaluSpsParse::GetRealWidthHeight(int &width, int &height)
{
	width = (pic_width_in_mbs_minus1 + 1) * 16;
	height = (2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16;

	if (frame_cropping_flag)
	{
		int crop_unit_x = 0;
		int crop_unit_y = 0;

		if (chroma_array_type == 0)
		{
			crop_unit_x = 1;
			crop_unit_y = 2 - frame_mbs_only_flag;
		}
		else if (chroma_array_type == 1 || chroma_array_type == 2 || chroma_array_type == 3)
		{
			crop_unit_x = sub_width_c;
			crop_unit_y = sub_height_c * (2 - frame_mbs_only_flag);
		}

		width -= crop_unit_x * (frame_crop_left_offset + frame_crop_right_offset);
		height -= crop_unit_y * (frame_crop_top_offset + frame_crop_bottom_offset);
	}
	return true;
}

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// PPS帧信息解析
NaluPpsParse::NaluPpsParse(unsigned char *pps, int len)
{
	int startCodeLen = 0;
	if (pps && len > 3)
	{
		/* 如果包含起始码，则跳过起始码 */
		if (pps[0] == 0 && pps[1] == 0 && pps[2] == 1)
		{
			startCodeLen = 3;
		}
		else if (pps[0] == 0 && pps[1] == 0
			&& pps[2] == 0 && pps[3] == 1)
		{
			startCodeLen = 4;
		}

		length = len - startCodeLen;
		stream = new unsigned char[length];
		memcpy(stream, pps + startCodeLen, length);

		/* init bit stream */
		bs = new BitStream(stream + 1, length - 1); // 跳过头字节

		/* 解析PPS信息 */
		pic_parameter_set_id = bs->ReadUE1();
		seq_parameter_set_id = bs->ReadUE1();
		entropy_coding_mode_flag = bs->ReadU1();
		bottom_field_pic_order_in_frame_present_flag = bs->ReadU1();
		num_slice_groups_minus1 = bs->ReadUE1();

		if (num_slice_groups_minus1 > 0)
		{
			slice_group_map_type = bs->ReadUE1();
			if (slice_group_map_type == 0)
			{
				for (int i = 0; i <= num_slice_groups_minus1; i++)
				{
					run_length_minus1.push_back(bs->ReadUE1());
				}
			}
			else if (slice_group_map_type == 2)
			{
				for (int i = 0; i < num_slice_groups_minus1; i++)
				{
					top_left.push_back(bs->ReadUE1());
					bottom_right.push_back(bs->ReadUE1());
				}
			}
			else if (slice_group_map_type == 3 ||
				slice_group_map_type == 4 ||
				slice_group_map_type == 5)
			{
				slice_group_change_direction_flag = bs->ReadU1();
				slice_group_change_rate_minus1 = bs->ReadUE1();
			}
			else if (slice_group_map_type == 6)
			{
				pic_size_in_map_units_minus1 = bs->ReadUE1();
				for (int i = 0; i <= pic_size_in_map_units_minus1; i++)
				{
					slice_group_id.push_back(bs->ReadU(16)); // 实际应根据bit_depth读取
				}
			}
		}

		num_ref_idx_l0_default_active_minus1 = bs->ReadUE1();
		num_ref_idx_l1_default_active_minus1 = bs->ReadUE1();
		weighted_pred_flag = bs->ReadU1();
		weighted_bipred_idc = bs->ReadU(2);
		pic_init_qp_minus26 = bs->ReadSE1();
		pic_init_qs_minus26 = bs->ReadSE1();
		chroma_qp_index_offset = bs->ReadSE1();
		deblocking_filter_control_present_flag = bs->ReadU1();
		constrained_intra_pred_flag = bs->ReadU1();
		redundant_pic_cnt_present_flag = bs->ReadU1();

		if (deblocking_filter_control_present_flag)
		{
			disable_deblocking_filter_idc = bs->ReadUE1();
			if (disable_deblocking_filter_idc != 1)
			{
				slice_alpha_c0_offset_div2 = bs->ReadSE1();
				slice_beta_offset_div2 = bs->ReadSE1();
			}
		}
	}
}










