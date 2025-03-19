/*
 * Intel tile Dash muxer
 *
 * Copyright (c) 2018 Intel Cooperation 
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>

#include "tiled_dash_parse.h"

void format_date_now(char *buf, int size)
{
    time_t t = time(NULL);
    struct tm *ptm, tmbuf;
    ptm = gmtime_r(&t, &tmbuf);
    if (ptm) {
        if (!strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", ptm))
            buf[0] = '\0';
    }
}

/**
 * A function which takes FFmpeg H264 extradata (SPS/PPS) and bring them ready to be pushed to the MP4 muxer.
 * @param extradata
 * @param extradata_size
 * @param dstcfg
 * @returns GF_OK is the extradata was parsed and is valid, other values otherwise.
 */
static GF_Err dc_avc_import_ffextradata(const u8 *extradata, const u64 extradata_size, GF_AVCConfig *dstcfg)
{
#ifdef GPAC_DISABLE_AV_PARSERS
	return GF_OK;
#else
	u8 nal_size;
	AVCState avc;
	GF_BitStream *bs;
	if (!extradata || (extradata_size < sizeof(u32)))
		return GF_BAD_PARAM;
	bs = gf_bs_new((const char *) extradata, extradata_size, GF_BITSTREAM_READ);
	if (!bs)
		return GF_BAD_PARAM;
	if (gf_bs_read_u32(bs) != 0x00000001) {
		gf_bs_del(bs);
		return GF_BAD_PARAM;
	}

	//SPS
	{
		s32 idx;
		char *buffer = NULL;
		const u64 nal_start = 4;
		nal_size = gf_media_nalu_next_start_code_bs(bs);
		if (nal_start + nal_size > extradata_size) {
			gf_bs_del(bs);
			return GF_BAD_PARAM;
		}
		buffer = (char*)gf_malloc(nal_size);
		gf_bs_read_data(bs, buffer, nal_size);
		gf_bs_seek(bs, nal_start);
		if ((gf_bs_read_u8(bs) & 0x1F) != GF_AVC_NALU_SEQ_PARAM) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		idx = gf_media_avc_read_sps(buffer, nal_size, &avc, 0, NULL);
		if (idx < 0) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		dstcfg->configurationVersion = 1;
		dstcfg->profile_compatibility = avc.sps[idx].prof_compat;
		dstcfg->AVCProfileIndication = avc.sps[idx].profile_idc;
		dstcfg->AVCLevelIndication = avc.sps[idx].level_idc;
		dstcfg->chroma_format = avc.sps[idx].chroma_format;
		dstcfg->luma_bit_depth = 8 + avc.sps[idx].luma_bit_depth_m8;
		dstcfg->chroma_bit_depth = 8 + avc.sps[idx].chroma_bit_depth_m8;

		{
			GF_AVCConfigSlot *slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
			slc->size = nal_size;
			slc->id = idx;
			slc->data = buffer;
			gf_list_add(dstcfg->sequenceParameterSets, slc);
		}
	}

	//PPS
	{
		s32 idx;
		char *buffer = NULL;
		const u64 nal_start = 4 + nal_size + 4;
		gf_bs_seek(bs, nal_start);
		nal_size = gf_media_nalu_next_start_code_bs(bs);
		if (nal_start + nal_size > extradata_size) {
			gf_bs_del(bs);
			return GF_BAD_PARAM;
		}
		buffer = (char*)gf_malloc(nal_size);
		gf_bs_read_data(bs, buffer, nal_size);
		gf_bs_seek(bs, nal_start);
		if ((gf_bs_read_u8(bs) & 0x1F) != GF_AVC_NALU_PIC_PARAM) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		idx = gf_media_avc_read_pps(buffer, nal_size, &avc);
		if (idx < 0) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		{
			GF_AVCConfigSlot *slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
			slc->size = nal_size;
			slc->id = idx;
			slc->data = buffer;
			gf_list_add(dstcfg->pictureParameterSets, slc);
		}
	}

	gf_bs_del(bs);
	return GF_OK;
#endif
}

/**
 * A function which takes FFmpeg H265 extradata (SPS/PPS) and bring them ready to be pushed to the MP4 muxer.
 * @param extradata
 * @param extradata_size
 * @param dstcfg
 * @returns GF_OK is the extradata was parsed and is valid, other values otherwise.
 */
static GF_Err dc_hevc_import_ffextradata(const u8 *extradata, const u64 extradata_size, HEVCState* hevc, GF_HEVCConfig *dst_cfg)
{
#ifdef GPAC_DISABLE_AV_PARSERS
    return GF_OK;
#else
    //HEVCState hevc;
    GF_HEVCParamArray *vpss = NULL, *spss = NULL, *ppss = NULL, *seis = NULL;
    GF_BitStream *bs;
    char *buffer = NULL;
    u32 buffer_size = 0;
    if (!extradata || (extradata_size < sizeof(u32)))
  	return GF_BAD_PARAM;
    bs = gf_bs_new((const char *) extradata, extradata_size, GF_BITSTREAM_READ);
    if (!bs)
 	return GF_BAD_PARAM;
    
    if( NULL == dst_cfg->param_array ) dst_cfg->param_array = gf_list_new();
    
    memset(hevc, 0, sizeof(HEVCState));
    hevc->sps_active_idx = -1;

    while (gf_bs_available(bs)) {
 	s32 idx;
	GF_AVCConfigSlot *slc;
	u8 nal_unit_type, temporal_id, layer_id;
	u64 nal_start, start_code;
	u32 nal_size;

	start_code = gf_bs_read_u32(bs);
	if (start_code>>8 == 0x000001) {
            nal_start = gf_bs_get_position(bs) - 1;
	    gf_bs_seek(bs, nal_start);
	    start_code = 1;
	}
	if (start_code != 0x00000001) {
	    gf_bs_del(bs);
	    if (buffer) gf_free(buffer);
		if (vpss && spss && ppss) return GF_OK;
		return GF_BAD_PARAM;
	    }
	    nal_start = gf_bs_get_position(bs);
	    nal_size = gf_media_nalu_next_start_code_bs(bs);
	    if (nal_start + nal_size > extradata_size) {
		gf_bs_del(bs);
		return GF_BAD_PARAM;
	    }

	    if (nal_size > buffer_size) {
		buffer = (char*)gf_realloc(buffer, nal_size);
		buffer_size = nal_size;
	    }
	    gf_bs_read_data(bs, buffer, nal_size);

	    gf_media_hevc_parse_nalu(buffer, nal_size, hevc, &nal_unit_type, &temporal_id, &layer_id);
	    if (layer_id) {
		gf_bs_del(bs);
		gf_free(buffer);
		return GF_BAD_PARAM;
	    }

	    switch (nal_unit_type) {
	    case GF_HEVC_NALU_VID_PARAM:
		idx = gf_media_hevc_read_vps(buffer, nal_size , hevc);
		if (idx < 0) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		assert(hevc.vps[idx].state == 1); //we don't expect multiple VPS
		if (hevc->vps[idx].state == 1) {
		    hevc->vps[idx].state = 2;
		    hevc->vps[idx].crc = gf_crc_32(buffer, nal_size);

                    dst_cfg->avgFrameRate = hevc->vps[idx].rates[0].avg_pic_rate;
     		    dst_cfg->constantFrameRate = hevc->vps[idx].rates[0].constand_pic_rate_idc;
		    dst_cfg->numTemporalLayers = hevc->vps[idx].max_sub_layers;
		    dst_cfg->temporalIdNested = hevc->vps[idx].temporal_id_nesting;

		    if (!vpss) {
		 	GF_SAFEALLOC(vpss, GF_HEVCParamArray);
			if (vpss) {
			    vpss->nalus = gf_list_new();
			    gf_list_add(dst_cfg->param_array, vpss);
			    vpss->array_completeness = 1;
			    vpss->type = GF_HEVC_NALU_VID_PARAM;
			}
		    }

		    slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
		    if (slc) {
			slc->size = nal_size;
			slc->id = idx;
			slc->data = (char*)gf_malloc(sizeof(char)*slc->size);
			if (slc->data)
		     	    memcpy(slc->data, buffer, sizeof(char)*slc->size);
			if (vpss)
			    gf_list_add(vpss->nalus, slc);
		    }
		}
			break;
		case GF_HEVC_NALU_SEQ_PARAM:
		    idx = gf_media_hevc_read_sps(buffer, nal_size, hevc);
		    if (idx < 0) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		    }

		    assert(!(hevc->sps[idx].state & AVC_SPS_DECLARED)); //we don't expect multiple SPS
		    if ((hevc->sps[idx].state & AVC_SPS_PARSED) && !(hevc->sps[idx].state & AVC_SPS_DECLARED)) {
			hevc->sps[idx].state |= AVC_SPS_DECLARED;
			hevc->sps[idx].crc = gf_crc_32(buffer, nal_size);
		    }

		    dst_cfg->configurationVersion = 1;
		    dst_cfg->profile_space = hevc->sps[idx].ptl.profile_space;
		    dst_cfg->tier_flag = hevc->sps[idx].ptl.tier_flag;
		    dst_cfg->profile_idc = hevc->sps[idx].ptl.profile_idc;
		    dst_cfg->general_profile_compatibility_flags = hevc->sps[idx].ptl.profile_compatibility_flag;
		    dst_cfg->progressive_source_flag = hevc->sps[idx].ptl.general_progressive_source_flag;
		    dst_cfg->interlaced_source_flag = hevc->sps[idx].ptl.general_interlaced_source_flag;
		    dst_cfg->non_packed_constraint_flag = hevc->sps[idx].ptl.general_non_packed_constraint_flag;
		    dst_cfg->frame_only_constraint_flag = hevc->sps[idx].ptl.general_frame_only_constraint_flag;

		    dst_cfg->constraint_indicator_flags = hevc->sps[idx].ptl.general_reserved_44bits;
		    dst_cfg->level_idc = hevc->sps[idx].ptl.level_idc;

                    dst_cfg->chromaFormat = hevc->sps[idx].chroma_format_idc;
		    dst_cfg->luma_bit_depth = hevc->sps[idx].bit_depth_luma;
		    dst_cfg->chroma_bit_depth = hevc->sps[idx].bit_depth_chroma;

		    if (!spss) {
		 	GF_SAFEALLOC(spss, GF_HEVCParamArray);
			if (spss) {
		   	    spss->nalus = gf_list_new();
			    gf_list_add(dst_cfg->param_array, spss);
			    spss->array_completeness = 1;
			    spss->type = GF_HEVC_NALU_SEQ_PARAM;
			}
		    }

                    slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
		    if (slc) {
			slc->size = nal_size;
			slc->id = idx;
			slc->data = (char*)gf_malloc(sizeof(char)*slc->size);
			if (slc->data)
		   	    memcpy(slc->data, buffer, sizeof(char)*slc->size);
			    if (spss)
				gf_list_add(spss->nalus, slc);
		    }
		    break;
		case GF_HEVC_NALU_PIC_PARAM:
		    idx = gf_media_hevc_read_pps(buffer, nal_size, hevc);
		    if (idx < 0) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		    }

		    assert(hevc->pps[idx].state == 1); //we don't expect multiple PPS
		    if (hevc->pps[idx].state == 1) {
			hevc->pps[idx].state = 2;
			hevc->pps[idx].crc = gf_crc_32(buffer, nal_size);

			if (!ppss) {
		   	    GF_SAFEALLOC(ppss, GF_HEVCParamArray);
			    if (ppss) {
				ppss->nalus = gf_list_new();
				gf_list_add(dst_cfg->param_array, ppss);
				ppss->array_completeness = 1;
				ppss->type = GF_HEVC_NALU_PIC_PARAM;
			    }
			}

			slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
			if (slc) {
			    slc->size = nal_size;
			    slc->id = idx;
			    slc->data = (char*)gf_malloc(sizeof(char)*slc->size);
			    if (slc->data)
				memcpy(slc->data, buffer, sizeof(char)*slc->size);
    			        if (ppss)
				    gf_list_add(ppss->nalus, slc);
		        }
		    }
			break;
		case GF_HEVC_NALU_SEI_PREFIX:
		    if (!seis) {
			GF_SAFEALLOC(seis, GF_HEVCParamArray);
			if (seis) {
			    seis->nalus = gf_list_new();
			    seis->array_completeness = 0;
			    seis->type = GF_HEVC_NALU_SEI_PREFIX;
			}
		    }
		    slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
		    if (slc) {
			slc->size = nal_size;
			slc->data = (char*)gf_malloc(sizeof(char)*slc->size);
			if (slc->data)
		  	    memcpy(slc->data, buffer, sizeof(char)*slc->size);
			if (seis)
			     gf_list_add(seis->nalus, slc);
		    }
		    break;
		default:
		    break;
	    }
    }

    gf_bs_del(bs);
    if (buffer) gf_free(buffer);

    return GF_OK;
#endif
}

static int hevc_get_tile_info(HEVCState *hevc, u32 *tile_x, u32 *tile_y, u32 *tile_width, u32 *tile_height)
{
	HEVCSliceInfo *si = &hevc->s_info;
	u32 i, tbX, tbY, PicWidthInCtbsY, PicHeightInCtbsY, tileX, tileY, oX, oY, val;

        if( (0==si->sps->max_CU_width)||
            (0==si->pps->num_tile_columns)||
            (0==si->pps->num_tile_rows) ){
            av_log(NULL, AV_LOG_ERROR, "si->slice_segment_address=%d, hevc_get_tile_info: si->sps->max_CU_width=%d, si->pps->num_tile_columns =%d, si->pps->num_tile_rows =%d \n", 
                    si->slice_segment_address,
                    si->sps->max_CU_width,
                    si->pps->num_tile_columns,
                    si->pps->num_tile_rows);
            return -1;
        }
	PicWidthInCtbsY = si->sps->width / si->sps->max_CU_width;
	if (PicWidthInCtbsY * si->sps->max_CU_width < si->sps->width) PicWidthInCtbsY++;
	PicHeightInCtbsY = si->sps->height / si->sps->max_CU_width;
	if (PicHeightInCtbsY * si->sps->max_CU_width < si->sps->height) PicHeightInCtbsY++;

	tbX = si->slice_segment_address % PicWidthInCtbsY;
	tbY = si->slice_segment_address / PicWidthInCtbsY;

	tileX = tileY = 0;
	oX = oY = 0;
	for (i=0; i < si->pps->num_tile_columns; i++) {
		if (si->pps->uniform_spacing_flag) {
			val = (i+1)*PicWidthInCtbsY / si->pps->num_tile_columns - (i)*PicWidthInCtbsY / si->pps->num_tile_columns;
		} else {
			if (i<si->pps->num_tile_columns-1) {
				val = si->pps->column_width[i];
			} else {
				val = (PicWidthInCtbsY - si->pps->column_width[i-1]);
			}
		}
		*tile_x = oX;
		*tile_width = val;

		if (oX >= tbX) break;
		oX += val;
		tileX++;
	}
	for (i=0; i<si->pps->num_tile_rows; i++) {
		if (si->pps->uniform_spacing_flag) {
			val = (i+1)*PicHeightInCtbsY / si->pps->num_tile_rows - (i)*PicHeightInCtbsY / si->pps->num_tile_rows;
		} else {
			if (i<si->pps->num_tile_rows-1) {
				val = si->pps->row_height[i];
			} else {
				val = (PicHeightInCtbsY - si->pps->row_height[i-1]);
			}
		}
		*tile_y = oY;
		*tile_height = val;

		if (oY >= tbY) break;
		oY += val;
		tileY++;
	}
	*tile_x = *tile_x * si->sps->max_CU_width;
	*tile_y = *tile_y * si->sps->max_CU_width;
	*tile_width = *tile_width * si->sps->max_CU_width;
	*tile_height = *tile_height * si->sps->max_CU_width;

	if (*tile_x + *tile_width > si->sps->width)
		*tile_width = si->sps->width - *tile_x;
	if (*tile_y + *tile_height > si->sps->height)
		*tile_height = si->sps->height - *tile_y;

	return tileX + tileY * si->pps->num_tile_columns;
}

static int hevc_get_tile_rect(HEVCState hevc, int idx, u32 *tile_x, u32 *tile_y, u32 *tile_width, u32 *tile_height)
{
           
    u32 i, tbX, tbY, PicWidthInCtbsY, PicHeightInCtbsY, tileX, tileY, oX, oY, val;

    HEVC_SPS sps = hevc.sps[0];
    HEVC_PPS pps = hevc.pps[0];
    if( (0==sps.max_CU_width)||
        (0==pps.num_tile_columns)||
        (0==pps.num_tile_rows) ){
            av_log(NULL, AV_LOG_ERROR, "hevc_get_tile_rect: sps->max_CU_width=%d, pps->num_tile_columns =%d, pps->num_tile_rows =%d \n", 
                    sps.max_CU_width,
                    pps.num_tile_columns,
                    pps.num_tile_rows);
            return -1;
    }
    
    PicWidthInCtbsY = sps.width / sps.max_CU_width;
    if (PicWidthInCtbsY * sps.max_CU_width < sps.width) PicWidthInCtbsY++;

    PicHeightInCtbsY = sps.height / sps.max_CU_width;
    if (PicHeightInCtbsY * sps.max_CU_width < sps.height) PicHeightInCtbsY++;

    tbX = idx % pps.num_tile_columns;
    tbY = idx / pps.num_tile_columns;

    tileX = tileY = 0;
    oX = oY = 0;
    for (i=0; i < pps.num_tile_columns; i++) {
        if (pps.uniform_spacing_flag) {
	    val = (i+1)*PicWidthInCtbsY / pps.num_tile_columns - (i)*PicWidthInCtbsY / pps.num_tile_columns;
	} else {
	    if (i<pps.num_tile_columns-1) {
		val = pps.column_width[i];
	    } else {
		val = (PicWidthInCtbsY - pps.column_width[i-1]);
	    }
        }
	*tile_x = oX;
	*tile_width = val;

	if (oX >= (tbX * (PicWidthInCtbsY / pps.num_tile_columns))) break;
	oX += val;
	tileX++;
    }
    for (i=0; i<pps.num_tile_rows; i++) {
        if (pps.uniform_spacing_flag) {
            val = (i+1)*PicHeightInCtbsY / pps.num_tile_rows - (i)*PicHeightInCtbsY / pps.num_tile_rows;
	} else {
            if (i<pps.num_tile_rows-1) {
	        val = pps.row_height[i];
	    } else {
		val = (PicHeightInCtbsY - pps.row_height[i-1]);
	    }
	}
	*tile_y = oY;
	*tile_height = val;

	if (oY >= (tbY * (PicHeightInCtbsY / pps.num_tile_rows))) break;
	oY += val;
	tileY++;
    }
    
    *tile_x = *tile_x * sps.max_CU_width;
    *tile_y = *tile_y * sps.max_CU_width;
    *tile_width = *tile_width * sps.max_CU_width;
    *tile_height = *tile_height * sps.max_CU_width;

    if (*tile_x + *tile_width > sps.width)
	*tile_width = sps.width - *tile_x;
    if (*tile_y + *tile_height > sps.height)
	*tile_height = sps.height - *tile_y;

    return tileX + tileY * pps.num_tile_columns;
}

static GF_Err dc_gpac_video_write_config(DashOutStream* os, int idx, u32 *di, u32 track) 
{
    GF_Err ret;
    VideoOutput* video_output_file = os->video_out[idx];
    
    if (os->codec_ctx->codec_id == AV_CODEC_ID_H264){
	ret = gf_isom_avc_config_new(video_output_file->isof, track, &os->avc_cfg, NULL, NULL, di);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_avc_config_new\n", gf_error_to_string(ret));
		return ret;
	}

        //inband SPS/PPS
	ret = gf_isom_avc_set_inband_config(video_output_file->isof, track, 1);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_avc_set_inband_config\n", gf_error_to_string(ret));
		return ret;
	}
    } else if (os->codec_ctx->codec_id == AV_CODEC_ID_HEVC) { //FIXME CODEC_ID_HEVC would break on old releases
	ret = gf_isom_hevc_config_new(video_output_file->isof, track, &os->hevc_cfg, NULL, NULL, di);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_hevc_config_new\n", gf_error_to_string(ret));
		return ret;
	}

        //inband SPS/PPS
	//ret = gf_isom_hevc_set_inband_config(video_output_file->isof, track, 1);
	//if (ret != GF_OK) {
	 //   av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_hevc_set_inband_config\n", gf_error_to_string(ret));
	 //   return ret;
	//}
    }

    return GF_OK;
}

static int dc_gpac_video_moov_create_tile(DashOutStream* os, int idx)
{
    GF_Err ret;
    u32 TrackNum;
    VideoOutput* video_output_file = os->video_out[idx];
    char filename[1024]; 
    snprintf(filename, sizeof(filename), "%s%s", 
                 os->dir_name, 
                 os->video_out[idx]->seg_init_name);
    
    if( idx == 0 ) return -1;
    
    video_output_file->isof = gf_isom_open(filename, GF_ISOM_OPEN_WRITE, NULL);
    
    ret = gf_isom_clone_movie( os->video_out[0]->isof, 
                               video_output_file->isof, 
                               GF_FALSE, GF_FALSE, GF_TRUE, 
                               GF_FALSE );
    if (ret) return -1;


    /*because of movie fragments MOOF based offset, ISOM <4 is forbidden*/
    gf_isom_set_brand_info(video_output_file->isof, GF_ISOM_BRAND_ISO5, 1);
    gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_ISOM, 0);
    gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_ISO1, 0);
    gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_ISO2, 0);
    gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_ISO3, 0);
    gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_MP41, 0);
    gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_MP42, 0);

    gf_isom_remove_root_od(video_output_file->isof);
    
    ret = gf_isom_clone_track(os->video_out[0]->isof, idx+1, video_output_file->isof, GF_FALSE, &TrackNum);
    if (ret) return -1;
    
    video_output_file->iso_track_ID = gf_isom_get_track_id(video_output_file->isof, TrackNum);
    video_output_file->iso_track = TrackNum;
    
    ret = gf_isom_setup_track_fragment( video_output_file->isof, gf_isom_get_track_id(video_output_file->isof, TrackNum), 0, 0, 0, 0, 0, 0);

    if (ret) return -1;
  //"hvt1.1.6.L186.80"
    gf_isom_set_brand_info(video_output_file->isof, GF_ISOM_BRAND_ISO5, 1);
    gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_DASH, 1);
    
    ret = gf_isom_finalize_for_fragment(video_output_file->isof, 1);
    
    return 0;
}

static void hevc_add_trif(GF_ISOFile *file, u32 track, u32 id, Bool full_picture, u32 independent, Bool filtering_disable, u32 tx, u32 ty, u32 tw, u32 th, Bool is_default)
{
	char data[11];
	u32 di, data_size=7;
	GF_BitStream *bs;
	//write TRIF sample group description
	bs = gf_bs_new((const char*)data, 11, GF_BITSTREAM_WRITE);
	gf_bs_write_u16(bs, id);	//groupID
	gf_bs_write_int(bs, 1, 1); //tile Region flag always true for us
	gf_bs_write_int(bs, independent, 2); //independentIDC: set to 1 (motion-constrained tiles but not all tiles RAP)
	gf_bs_write_int(bs, full_picture, 1);//full picture: false since we don't do L-HEVC tiles
	gf_bs_write_int(bs, filtering_disable, 1); //filtering disabled: set to 1 (always true on our bitstreams for now) - Check xPS to be sure ...
	gf_bs_write_int(bs, 0, 1);//has dependency list: false since we don't do L-HEVC tiles
	gf_bs_write_int(bs, 0, 2); //reserved
	if (!full_picture) {
		gf_bs_write_u16(bs, tx);
		gf_bs_write_u16(bs, ty);
		data_size+=4;
	}
	gf_bs_write_u16(bs, tw);
	gf_bs_write_u16(bs, th);
	gf_bs_del(bs);

	gf_isom_add_sample_group_info(file, track, GF_ISOM_SAMPLE_GROUP_TRIF, data, data_size, is_default, &di);
}

static int dc_gpac_video_moov_create_root(DashOutStream* os)
{
	GF_Err ret;
	u32 di=1, track;
        u32 width, height;
	s32 translation_x, translation_y;
	s16 layer;

        VideoOutput* video_output_file = os->video_out[0];
        char filename[1024]; 
        snprintf(filename, sizeof(filename), "%s%s", 
                 os->dir_name, 
                 os->video_out[0]->seg_init_name);
	//TODO: For the moment it is fixed
	//u32 sample_dur = video_output_file->codec_ctx->time_base.den;

	//int64_t profile = 0;
	//av_opt_get_int(video_output_file->codec_ctx->priv_data, "level", AV_OPT_SEARCH_CHILDREN, &profile);

	video_output_file->isof = gf_isom_open(filename, GF_ISOM_OPEN_WRITE, NULL);
	if (!video_output_file->isof) {
		av_log(NULL, AV_LOG_ERROR, "Cannot open iso file %s\n", filename);
		return -1;
	}
	//gf_isom_store_movie_config(video_output_file->isof, 0);

	track = gf_isom_new_track(video_output_file->isof, 0, GF_ISOM_MEDIA_VISUAL, (int)(os->frame_rate + 0.5));
        video_output_file->iso_track = track;
	video_output_file->iso_track_ID = gf_isom_get_track_id(video_output_file->isof, track);

	if (!video_output_file->frame_dur)
		video_output_file->frame_dur = os->timescale.num / os->timescale.den;

	if (!track) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create new track\n");
		return -1;
	}

	ret = gf_isom_set_track_enabled(video_output_file->isof, track, 1);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_set_track_enabled\n", gf_error_to_string(ret));
		return -1;
	}

	ret = dc_gpac_video_write_config(os, 0, &di, track);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: dc_gpac_video_write_config\n", gf_error_to_string(ret));
		return -1;
	}
        gf_isom_set_nalu_extract_mode(video_output_file->isof, track, GF_ISOM_NALU_EXTRACT_INSPECT);
	gf_isom_set_visual_info(video_output_file->isof, track, di, os->codec_ctx->width, os->codec_ctx->height);
	gf_isom_set_sync_table(video_output_file->isof, track);

        /*because of movie fragments MOOF based offset, ISOM <4 is forbidden*/
        gf_isom_set_brand_info(video_output_file->isof, GF_ISOM_BRAND_ISO5, 1);
        gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_ISOM, 0);
        gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_ISO1, 0);
        gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_ISO2, 0);
        gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_ISO3, 0);
        gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_MP41, 0);
        gf_isom_modify_alternate_brand(video_output_file->isof, GF_ISOM_BRAND_MP42, 0);

        gf_isom_remove_root_od(video_output_file->isof);
        
        ret = gf_isom_setup_track_fragment( video_output_file->isof, 
                                            track, 1, 
                                            video_output_file->use_source_timing ? (u32) video_output_file->frame_dur : 1, 
                                            0, 0, 0, 0);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_setup_track_fragment\n", gf_error_to_string(ret));
		return -1;
	}
        
        for( int i=1; i<os->nb_tiles+1; i++){
            ret = gf_isom_clone_track(video_output_file->isof, track, video_output_file->isof, GF_FALSE, &os->video_out[i]->iso_track );
            if (ret) return ret;
            os->video_out[i]->iso_track_ID = gf_isom_get_track_id(video_output_file->isof, os->video_out[i]->iso_track);
        }
        
	for( int i=1; i<os->nb_tiles+1; i++){
            width = 0; 
            height = 0;
	    translation_x = 0;
            translation_y = 0;
	    layer = 0;
            
            
            gf_isom_hevc_set_tile_config(video_output_file->isof, os->video_out[i]->iso_track, 1, NULL, GF_FALSE);

	    // setup track references from tile track to base
            gf_isom_set_track_reference( video_output_file->isof, 
                                         os->video_out[i]->iso_track,                                         
                                         GF_ISOM_REF_TBAS, 
                                         video_output_file->iso_track_ID);
            if(ret != GF_OK){
                   av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_set_track_reference failed\n", gf_error_to_string(ret));
            }
            
            if (! gf_isom_has_track_reference(os->video_out[0]->isof, os->video_out[0]->iso_track, GF_ISOM_REF_SABT, os->video_out[i]->dash_track_ID)) {
                ret = gf_isom_set_track_reference( os->video_out[0]->isof, 
                                                  os->video_out[0]->iso_track, 
                                                  GF_ISOM_REF_SABT, 
                                                  os->video_out[i]->dash_track_ID);
                if(ret != GF_OK){
                   av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_set_track_reference failed\n", gf_error_to_string(ret));
               }
	    }
           
            hevc_add_trif( video_output_file->isof, 
                           os->video_out[i]->iso_track, 
                           os->video_out[i]->iso_track_ID, 
                           GF_FALSE, 
                           (os->video_out[i]->tile.all_intra) ? 2 : 1, 
                           GF_TRUE, 
                           os->video_out[i]->tile.tx, 
                           os->video_out[i]->tile.ty, 
                           os->video_out[i]->tile.tw,
                           os->video_out[i]->tile.th, 
                           GF_TRUE);
            
	    gf_isom_set_visual_info( video_output_file->isof, 
                                     os->video_out[i]->iso_track, 
                                     1, 
                                     os->video_out[i]->tile.tw, 
                                     os->video_out[i]->tile.th);

	    gf_isom_get_track_layout_info( video_output_file->isof, 
                                           track, 
                                           &width, &height, 
                                           &translation_x, &translation_y, 
                                           &layer);
	    gf_isom_set_track_layout_info( video_output_file->isof, 
                                           os->video_out[i]->iso_track, 
                                           width<<16, height<<16, 
                                           translation_x, translation_y, 
                                           layer);
 
            ret = gf_isom_setup_track_fragment( video_output_file->isof, 
                                                os->video_out[i]->iso_track, 1, 
                                                video_output_file->use_source_timing ? (u32) video_output_file->frame_dur : 1, 
                                                0, 0, 0, 0);
	    if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_setup_track_fragment\n", gf_error_to_string(ret));
		return -1;
	    }
        }
        
        

	ret = gf_isom_finalize_for_fragment(video_output_file->isof, track);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_finalize_for_fragment\n", gf_error_to_string(ret));
		return -1;
	}

	/*ret = gf_media_get_rfc_6381_codec_name(video_output_file->isof, track, video_output_file->video_data_conf->codec6381, GF_FALSE, GF_FALSE);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_finalize_for_fragment\n", gf_error_to_string(ret));
		return -1;
	}*/
        
	return 0;
}

static int dc_gpac_video_moov_create(DashOutStream* os, int idx)
{
	GF_Err ret;
	u32 di=1, track;

        VideoOutput* video_output_file = os->video_out[idx];
        char filename[1024]; 
        snprintf(filename, sizeof(filename), "%s%s", 
                 os->dir_name, 
                 os->video_out[idx]->seg_init_name );
        
        //TODO: For the moment it is fixed
	//u32 sample_dur = video_output_file->codec_ctx->time_base.den;

	//int64_t profile = 0;
	//av_opt_get_int(video_output_file->codec_ctx->priv_data, "level", AV_OPT_SEARCH_CHILDREN, &profile);

	video_output_file->isof = gf_isom_open(filename, GF_ISOM_OPEN_WRITE, NULL);
	if (!video_output_file->isof) {
		av_log(NULL, AV_LOG_ERROR, "Cannot open iso file %s\n", filename);
		return -1;
	}
	//gf_isom_store_movie_config(video_output_file->isof, 0);
	track = gf_isom_new_track(video_output_file->isof, 0, GF_ISOM_MEDIA_VISUAL, (int)(os->frame_rate + 0.5));
	video_output_file->iso_track_ID = gf_isom_get_track_id(video_output_file->isof, track);

	//video_output_file->timescale = os->timescale.den;
	if (!video_output_file->frame_dur)
		video_output_file->frame_dur = os->timescale.num / os->timescale.den;

	if (!track) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create new track\n");
		return -1;
	}

	ret = gf_isom_set_track_enabled(video_output_file->isof, track, 1);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_set_track_enabled\n", gf_error_to_string(ret));
		return -1;
	}

	ret = dc_gpac_video_write_config(os, idx, &di, track);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: dc_gpac_video_write_config\n", gf_error_to_string(ret));
		return -1;
	}
        
	gf_isom_set_visual_info(video_output_file->isof, track, di, os->codec_ctx->width, os->codec_ctx->height);
	gf_isom_set_sync_table(video_output_file->isof, track);

	ret = gf_isom_setup_track_fragment(video_output_file->isof, track, 1, video_output_file->use_source_timing ? (u32) video_output_file->frame_dur : 1, 0, 0, 0, 0);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_setup_track_fragment\n", gf_error_to_string(ret));
		return -1;
	}

	ret = gf_isom_finalize_for_fragment(video_output_file->isof, track);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_finalize_for_fragment\n", gf_error_to_string(ret));
		return -1;
	}

	/*ret = gf_media_get_rfc_6381_codec_name(video_output_file->isof, track, video_output_file->video_data_conf->codec6381, GF_FALSE, GF_FALSE);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_finalize_for_fragment\n", gf_error_to_string(ret));
		return -1;
	}*/

	return 0;
}

static int dc_gpac_video_isom_open_seg(DashOutStream* os, int idx)
{
	GF_Err ret;
        VideoOutput* video_output_file = os->video_out[idx];
	ret = gf_isom_start_segment(video_output_file->isof, os->video_out[idx]->seg_media_name, 1);
	if (ret != GF_OK) {
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_start_segment\n", gf_error_to_string(ret));
		return -1;
	}
	av_log(NULL, AV_LOG_ERROR, "[DashCast] Opening new segment %s at UTC "LLU" ms\n", os->video_out[idx]->seg_media_name, gf_net_get_utc() );
	return 0;
}

static int dc_gpac_video_isom_write(DashOutStream* os, int idx)
{
	GF_Err ret;
	VideoOutput* video_output_file = os->video_out[idx];

	u32 sc_size = 0;
	u32 nalu_size = 0;

	u32 buf_len = video_output_file->encoded_frame_size;
	u8 *buf_ptr = video_output_file->vbuf;

	GF_BitStream *out_bs = gf_bs_new(NULL, 2 * buf_len, GF_BITSTREAM_WRITE);
        nalu_size = gf_media_nalu_next_start_code(buf_ptr, buf_len, &sc_size);
	if (0 != nalu_size) {
		gf_bs_write_u32(out_bs, nalu_size);
		gf_bs_write_data(out_bs, (const char*) buf_ptr, nalu_size);
	}
	
        buf_ptr += (nalu_size + sc_size);
	buf_len -= (nalu_size + sc_size);
	
	while (buf_len) {
		nalu_size = gf_media_nalu_next_start_code(buf_ptr, buf_len, &sc_size);
		if (nalu_size != 0) {
			gf_bs_write_u32(out_bs, nalu_size );
			gf_bs_write_data(out_bs, (const char*) buf_ptr, nalu_size );
		}

		buf_ptr += nalu_size;

		if (!sc_size || (buf_len < nalu_size + sc_size))
			break;
		buf_len -= nalu_size + sc_size;
		buf_ptr += sc_size;
	}
	gf_bs_get_content(out_bs, &video_output_file->sample->data, &video_output_file->sample->dataLength);
	//video_output_file->sample->data = //(char *) (video_output_file->vbuf + nalu_size + sc_size);
	//video_output_file->sample->dataLength = //video_output_file->encoded_frame_size - (sc_size + nalu_size);

	video_output_file->sample->DTS = os->video_out[idx]->cur_pts;
	video_output_file->sample->CTS_Offset = (s32) (os->video_out[idx]->cur_pts - video_output_file->sample->DTS);
	video_output_file->sample->IsRAP = os->video_out[idx]->cur_keyframe;
	av_log(NULL, AV_LOG_DEBUG, "%d, Isom Write: RAP %d , DTS "LLD" CTS offset %d \n",  
                    video_output_file->iso_track_ID,  
                    video_output_file->sample->IsRAP,  
                    video_output_file->sample->DTS,  
                    video_output_file->sample->CTS_Offset); 
	ret = gf_isom_fragment_add_sample( video_output_file->isof, 
                                           video_output_file->iso_track_ID, 
                                           video_output_file->sample, 1, 
                                           video_output_file->use_source_timing ? (u32) video_output_file->frame_dur : 1, 
                                           0, 0, 0);
	if (ret != GF_OK) {
		gf_bs_del(out_bs);
		av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_fragment_add_sample\n", gf_error_to_string(ret));
		return -1;
	}
              
        video_output_file->sample_count++;
	//free data but keep sample structure alive
	gf_free(video_output_file->sample->data);
	video_output_file->sample->data = NULL;
	video_output_file->sample->dataLength = 0;

	gf_bs_del(out_bs);
	return 0;
}

static int get_tile_output(DashOutStream* os, int x, int y, int w, int h)
{
    for(int i = 1; i < os->nb_tiles+1; i++){
        if( (os->video_out[i]->tile.tx == x)&&
            (os->video_out[i]->tile.ty == y)&&
            (os->video_out[i]->tile.tw == w)&&
            (os->video_out[i]->tile.th == h))
            return i;
    }
    return 0;
}

static int dc_gpac_video_isom_tile_write(DashOutStream* os, AVPacket *pkt)
{
    int ret = 0;
    GF_Err e = GF_OK;
    u8 nal_type = 0;
    u8 temporal_id, layer_id;
    int cur_tile, tx, ty, tw, th, idx;
    int nalu_size = 0;
    int sc_size = 0;
    int buf_len = pkt->size;
    char *buf_ptr = pkt->data;
    HEVCState hevc = os->hevc_state;

    av_log(NULL, AV_LOG_DEBUG, "pkt->data=%p, pkt->size=%d,\n", buf_ptr, buf_len);
    GF_BitStream *out_bs = NULL;  
    GF_BitStream *bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
    
    nalu_size = gf_media_nalu_next_start_code(buf_ptr, buf_len, &sc_size);
    if (nalu_size)
    {
        av_log(NULL, AV_LOG_ERROR, "The NALU size before first start code should be zero. \n");
    } 
    if (sc_size) {
        buf_ptr += (nalu_size + sc_size);
        buf_len -= (nalu_size + sc_size);
    }
    idx = 0;
    while (buf_len) {
        sc_size = 0;
        if(NULL==out_bs) out_bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
        nalu_size = gf_media_nalu_next_start_code(buf_ptr, buf_len, &sc_size);
        av_log(NULL, AV_LOG_DEBUG, "buf_ptr=%p, buf_len=%d, nalu_size=%d, sc_size=%d\n", buf_ptr, buf_len, nalu_size, sc_size);
        if (nalu_size) {
            ret = gf_media_hevc_parse_nalu(buf_ptr, nalu_size, &hevc, &nal_type, &temporal_id, &layer_id);

	    //error parsing NAL, set nal to fallback to regular import
	    if (ret<0) nal_type = -1;

	    switch (nal_type) {
                case GF_HEVC_NALU_VID_PARAM:
                case GF_HEVC_NALU_SEQ_PARAM:
                case GF_HEVC_NALU_PIC_PARAM:
                     break;
                case GF_HEVC_NALU_SLICE_TRAIL_N:
		case GF_HEVC_NALU_SLICE_TRAIL_R:
		case GF_HEVC_NALU_SLICE_TSA_N:
		case GF_HEVC_NALU_SLICE_TSA_R:
		case GF_HEVC_NALU_SLICE_STSA_N:
		case GF_HEVC_NALU_SLICE_STSA_R:
		case GF_HEVC_NALU_SLICE_BLA_W_LP:
		case GF_HEVC_NALU_SLICE_BLA_W_DLP:
		case GF_HEVC_NALU_SLICE_BLA_N_LP:
		case GF_HEVC_NALU_SLICE_IDR_W_DLP:
		case GF_HEVC_NALU_SLICE_IDR_N_LP:
		case GF_HEVC_NALU_SLICE_CRA:
		case GF_HEVC_NALU_SLICE_RADL_R:
		case GF_HEVC_NALU_SLICE_RADL_N:
		case GF_HEVC_NALU_SLICE_RASL_R:
		case GF_HEVC_NALU_SLICE_RASL_N:
		    tx = ty = tw = th = 0;
		    cur_tile = hevc_get_tile_info( &hevc, &tx, &ty, &tw, &th);
		    if (cur_tile>=os->nb_tiles) {
			av_log(NULL, AV_LOG_ERROR, "[HEVC Tiles] Tile index %d is greater than number of tiles %d in PPS\n", cur_tile, os->nb_tiles);
			e = GF_NON_COMPLIANT_BITSTREAM;
		    }
		    if (e)
			continue;
                    idx = get_tile_output(os, tx, ty, tw, th);

                    if (hevc.s_info.slice_type != GF_HEVC_SLICE_TYPE_I) {
                        os->video_out[idx]->cur_keyframe = 0;
		    }else{
                        os->video_out[idx]->cur_keyframe = 1;
                    }
                    
                    os->video_out[idx]->encoded_frame_size = nalu_size + sc_size;
                    os->video_out[idx]->cur_pts = pkt->pts;
                    gf_bs_write_u32(out_bs, nalu_size );
		    gf_bs_write_data(out_bs, (const char*) buf_ptr, nalu_size );
                        
                    gf_bs_get_content(out_bs, &os->video_out[idx]->sample->data, &os->video_out[idx]->sample->dataLength);
          	    os->video_out[idx]->sample->DTS = os->video_out[idx]->cur_pts;
	            os->video_out[idx]->sample->CTS_Offset = (s32) (os->video_out[idx]->cur_pts - os->video_out[idx]->sample->DTS);
	            os->video_out[idx]->sample->IsRAP = os->video_out[idx]->cur_keyframe;
                    gf_bs_del(out_bs);
                    out_bs = NULL;
                    break;
                default:
                    os->video_out[0]->encoded_frame_size = nalu_size + sc_size;
                    os->video_out[0]->cur_pts = pkt->pts;
                    gf_bs_write_u32(bs, nalu_size );
		    gf_bs_write_data(bs, (const char*) buf_ptr, nalu_size );
                    
                    break;
            }
            buf_ptr = buf_ptr + (nalu_size + sc_size);
            buf_len = buf_len - (nalu_size + sc_size);

        }

    }
    
    gf_bs_get_content(bs, &os->video_out[0]->sample->data, &os->video_out[0]->sample->dataLength);
    gf_bs_del(bs);
    
    for(int i=0; i<os->nb_tiles+1; i++){
        ret = gf_isom_fragment_add_sample( os->video_out[i]->isof, 
                                           os->video_out[i]->iso_track_ID, 
                                           os->video_out[i]->sample, 1, 
                                           os->video_out[i]->use_source_timing ? (u32) os->video_out[i]->frame_dur : 1, 
                                           0, 0, 0);
        if(ret != GF_OK){
             av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_fragment_add_sample tiles=%d\n", gf_error_to_string(ret), i);
        }
        
        if(i > 0 ){
           /*if (! gf_isom_has_track_reference(os->video_out[0]->isof, os->video_out[0]->iso_track, GF_ISOM_REF_SABT, os->video_out[i]->dash_track_ID)) {
               ret = gf_isom_set_track_reference(os->video_out[0]->isof, os->video_out[0]->iso_track, GF_ISOM_REF_SABT, os->video_out[i]->dash_track_ID);
               if(ret != GF_OK){
                   av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_set_track_reference failed\n", gf_error_to_string(ret));
               }
	    }*/
            e = gf_isom_copy_sample_info(os->video_out[i]->isof, 1, os->video_out[0]->isof, 1, os->video_out[i]->sample_count+1);
	    if (ret != GF_OK){
                   av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_copy_sample_info failed\n", gf_error_to_string(ret));
               }
        }
        os->video_out[i]->sample_count++;
	//free data but keep sample structure alive
        gf_free(os->video_out[i]->sample->data);
        os->video_out[i]->sample->data = NULL;
        os->video_out[i]->sample->dataLength = 0;
    }
    
    return 0;
}

static int dc_gpac_video_isom_close_seg(DashOutStream* os, int idx)
{
    u64 seg_size;
    VideoOutput* video_output_file = os->video_out[idx];
    GF_Err ret = gf_isom_close_segment(video_output_file->isof, 0, 0, 0, 0, 0, 0, GF_TRUE, GF_FALSE, video_output_file->seg_marker, NULL, NULL, &seg_size);
    if (ret != GF_OK) {
	av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_close_segment\n", gf_error_to_string(ret));
	return -1;
    }
    av_log(NULL, AV_LOG_DEBUG, "[DashCast] Rep %s Closing segment %s at UTC "LLU" ms - size "LLU" bytes\n", video_output_file->rep_id, gf_isom_get_segment_name(video_output_file->isof), gf_net_get_utc(), seg_size );

    return 0;
}

static int gpac_video_isom_close(DashOutStream* os, int idx)
{
    GF_Err ret;
    VideoOutput* video_output_file = os->video_out[idx];
    //dc_gpac_video_isom_close_seg(os, idx);
    os->video_out[idx]->iso_created = 0;
    video_output_file->sample_count = 0;
    ret = gf_isom_close(video_output_file->isof);
    if (ret != GF_OK) {
	av_log(NULL, AV_LOG_ERROR, "%s: gf_isom_close\n", gf_error_to_string(ret));
	return -1;
    }
     
    return 0;
}

static int gpac_video_isom_open(DashOutStream* os, int idx)
{
    GF_Err ret;
    
    if( idx == 0 ){
        ret = dc_gpac_video_moov_create_root(os);
    }else{
        ret = dc_gpac_video_moov_create_tile(os, idx);
    }
    if(ret != GF_OK){
        
        av_log(NULL, AV_LOG_VERBOSE, "failed to init_video_ouput::dc_gpac_video_moov_create\n");
        return -1;
    }
    
    /*
    ret = dc_gpac_video_isom_open_seg(os, idx);
    if(ret != GF_OK){
        av_log(NULL, AV_LOG_VERBOSE, "failed to init_video_ouput::dc_gpac_video_isom_open_seg\n");
        return -1;
    }*/
    
     return 0;
}

int dash_probe_extra_data(DashOutStream* os, char* buf, int size)
{
    GF_Err ret;
    
    if( NULL == buf || 0 == size){
        av_log(NULL, AV_LOG_ERROR, "extra data buffer is NULL or size = 0; cannot proceed\n");
        return -1;
    }
    
    if( os->codec_ctx)
    switch( os->codec_ctx->codec_id ){
        case AV_CODEC_ID_HEVC:
            ret = dc_hevc_import_ffextradata(buf, size, &os->hevc_state, &os->hevc_cfg);
            break;
        case AV_CODEC_ID_H264:
            ret = dc_avc_import_ffextradata(buf, size, &os->avc_cfg);
            break;
        default:
           break;
    }
    
    if(ret != GF_OK){
        av_log(NULL, AV_LOG_ERROR, "import_ffextradata; cannot proceed\n");
        return -1;
    }
            
    return 0;
}

static void get_timescale(DashOutStream* os, int *timescale)
{
    int fps_1000 = (int)(os->frame_rate * 1000 + 0.5);

    if (fps_1000 == 29970)
    {
        *timescale = 30000;
    }
    else if (fps_1000 == 23976)
    {
        *timescale = 24000;
    }
    else if (fps_1000 == 59940)
    {
        *timescale = 60000;
    }
    else
    {
        *timescale = fps_1000;
    }
}
static int init_video_ouput_tile(DashOutStream* os, int idx)
{
    //GF_Err ret;
    int val;
    
    os->video_out[idx] = av_malloc(sizeof(VideoOutput));
    
#ifndef GPAC_DISABLE_ISOM
    os->video_out[idx]->sample = gf_isom_sample_new();
    os->video_out[idx]->isof = NULL;
#endif
    os->video_out[idx]->dash_track_ID = idx + 1;
    os->video_out[idx]->iso_track_ID = 1;
    /* Variables that encoder needs to encode data */
    os->video_out[idx]->bit_rate = os->bit_rate / os->nb_tiles; 

    os->video_out[idx]->seg_marker = 0;
    //gdr;
    os->video_out[idx]->use_source_timing = 0;
    os->video_out[idx]->nb_segments = 0;
    os->video_out[idx]->frame_dur = os->frame_dur;
    os->video_out[idx]->encoded_frame_size = 0;
    os->video_out[idx]->segment_index = 0;
    os->video_out[idx]->sample_count = 0;
    os->video_out[idx]->iso_created = 0;
    get_timescale(os, &(os->video_out[idx]->timescale));
    val = hevc_get_tile_rect(os->hevc_state,
                             idx - 1,
                             &(os->video_out[idx]->tile.tx),
                             &(os->video_out[idx]->tile.ty),
                             &(os->video_out[idx]->tile.tw),
                             &(os->video_out[idx]->tile.th));
    if(val < 0 || val > os->nb_tiles ){
        av_log(NULL, AV_LOG_ERROR, "init_video_ouput_tile: %d; hevc_get_tile_rect failed\n", idx );
        return -1;
    }
    
    os->video_out[idx]->tile.data_offset = 0;
    os->video_out[idx]->tile.nb_nalus_in_sample = 0;
    os->video_out[idx]->tile.all_intra = 0;
            
    os->video_out[idx]->dependency_id = os->stream_index;
    snprintf(os->video_out[idx]->rep_id, sizeof(os->video_out[idx]->rep_id), 
            "%s_%d", os->video_out[0]->rep_id, idx );
    snprintf(os->video_out[idx]->seg_init_name, sizeof(os->video_out[idx]->seg_init_name), 
            "%s_track%d_init.mp4", os->out_name, os->video_out[idx]->dash_track_ID );
    snprintf(os->video_out[idx]->seg_media_name_tmpl, sizeof(os->video_out[idx]->seg_media_name_tmpl), 
            "%s_track%d_$Number$.m4s", os->out_name, os->video_out[idx]->dash_track_ID);
    snprintf(os->video_out[idx]->seg_media_name, sizeof(os->video_out[idx]->seg_media_name), 
            "%s%s_track%d_%d.m4s", os->dir_name, os->out_name, os->video_out[idx]->dash_track_ID, os->video_out[idx]->segment_index+1);
    av_log(NULL, AV_LOG_VERBOSE, "main stream init mp4 name: %s; segment name templ: %s\n", 
            os->video_out[idx]->seg_init_name, os->video_out[idx]->seg_media_name_tmpl);

    return 0;
}

static int init_video_ouput(DashOutStream* os)
{
    //GF_Err ret;
    
    os->video_out[0] = malloc(sizeof(VideoOutput));
    
#ifndef GPAC_DISABLE_ISOM
    os->video_out[0]->sample = gf_isom_sample_new();
    os->video_out[0]->isof = NULL;
#endif
    
    os->video_out[0]->dash_track_ID = 1;
    os->video_out[0]->iso_track_ID = 1;
    /* Variables that encoder needs to encode data */
    
    os->video_out[0]->seg_marker = 0;
    //gdr;
    os->video_out[0]->use_source_timing = 0;
    os->video_out[0]->nb_segments = 0;
    os->video_out[0]->segment_index = 0;
    os->video_out[0]->frame_dur = os->frame_dur;
    os->video_out[0]->encoded_frame_size = 0;
    os->video_out[0]->iso_created = 0;
    get_timescale(os, &(os->video_out[0]->timescale));

    snprintf(os->video_out[0]->rep_id, sizeof(os->video_out[0]->rep_id), "%d", os->stream_index );
    snprintf(os->video_out[0]->seg_init_name, sizeof(os->video_out[0]->seg_init_name), "%s_set1_init.mp4", os->out_name );
    snprintf(os->video_out[0]->seg_media_name_tmpl, sizeof(os->video_out[0]->seg_media_name_tmpl), "%s_track1_$Number$.m4s", os->out_name);
    snprintf(os->video_out[0]->seg_media_name, sizeof(os->video_out[0]->seg_media_name), "%s%s_track1_%d.m4s", os->dir_name, os->out_name, os->video_out[0]->segment_index+1);
    av_log(NULL, AV_LOG_VERBOSE, "main stream init mp4 name: %s; segment name templ: %s\n", os->video_out[0]->seg_init_name, os->video_out[0]->seg_media_name);
    
    return 0;
}

void dash_end_output_stream(DashOutStream* os)
{
    for(int i=0; i<os->nb_tiles + 1; i++){
        gf_isom_flush_fragments(os->video_out[i]->isof, GF_TRUE);
        gpac_video_isom_close(os, i);
    }
}

void dash_free_output_stream(DashOutStream* os)
{
    //if(NULL != os){
    //    av_freep(os);
    //    os = NULL;
    //}    
}

int dash_init_output_stream(GPAC_DASHContext* ctx, DashOutStream* os)
{
    int ret = 0;
    int sps_id = 0;

    os->availability_time_offset = 0;
    os->seg_dur = ctx->seg_duration / 1000;    //ms
    os->frag_dur = ctx->seg_duration / 1000;  //ms
    os->frame_dur = (int64_t)(1000 / os->frame_rate); //ms
    if (ctx->window_size) {
        os->minimum_update_period = (os->seg_dur * ctx->window_size) / 1000;
    } else {
        os->minimum_update_period = os->seg_dur / 1000;
    }
        
    os->frame_per_segment= os->seg_dur / os->frame_dur;
    os->frame_per_fragment = os->frag_dur / os->frame_dur;
    os->first_dts_in_fragment = 0;
    os->fragment_started = 0;
    os->segment_started = 0;
    os->last_pts = AV_NOPTS_VALUE;
    os->last_dts = AV_NOPTS_VALUE;
    os->first_pts = AV_NOPTS_VALUE;
    os->start_pts = AV_NOPTS_VALUE;
    
    os->nb_tiles = 0;
    
    ret = init_video_ouput(os);
    if(ret){
        av_log(NULL, AV_LOG_ERROR, "failed to initial stream = %d, main output\n", os->vstream_idx );
        dash_free_output_stream(os);
    }
    
    sps_id = os->hevc_state.sps_active_idx;
    os->max_width = os->hevc_state.sps[sps_id].width;
    os->max_height = os->hevc_state.sps[sps_id].height;
    if(os->hevc_state.pps[0].tiles_enabled_flag && os->split_tile){
        os->nb_tiles = os->hevc_state.pps[0].num_tile_columns
                    * os->hevc_state.pps[0].num_tile_rows;

        for(int i = 1; i < os->nb_tiles+1; i++)
        {
            ret = init_video_ouput_tile(os, i);
            if(ret){
                dash_free_output_stream(os);
                av_log(NULL, AV_LOG_ERROR, "failed to initial stream = %d, output tile =%d\n", os->vstream_idx, i);
                break;
            }
        }
    }
    
    return ret;
}

int dash_write_segment(GPAC_DASHContext* ctx, DashOutStream* os, AVPacket *pkt)
{
    int ret =0;
    int i;
    int start_idx = 0;
    char filename[1024];
    int fragment_idx = 0;
 
    if(os->nb_frames % os->frame_per_segment == 0){
        for(i=0; i<os->nb_tiles+1; i++){
            if(0 == os->video_out[i]->iso_created){
                gpac_video_isom_open(os, i); 
                os->video_out[i]->iso_created = 1;
            }
            ret = dc_gpac_video_isom_open_seg(os, i);
            if(ret != GF_OK){
               av_log(NULL, AV_LOG_VERBOSE, "failed to init_video_ouput::dc_gpac_video_isom_open_seg\n");
               return -1;
            }
                      
        }
        os->segment_started = 1;
    }
    
    if(os->nb_frames % os->frame_per_fragment == 0){
        for(i=0; i<os->nb_tiles+1; i++){
            fragment_idx = i * (os->nb_tiles + 1) + 1;            
            gf_isom_set_next_moof_number(os->video_out[i]->isof, fragment_idx);
            gf_isom_start_fragment(os->video_out[i]->isof, 1);
            gf_isom_set_traf_base_media_decode_time(os->video_out[i]->isof, os->video_out[i]->iso_track_ID, os->first_dts_in_fragment);
        }
        os->first_dts_in_fragment += os->frame_per_fragment;
        os->fragment_started = 1;
    }
    
    if(os->hevc_state.pps[0].tiles_enabled_flag && os->split_tile){
        ret = dc_gpac_video_isom_tile_write(os, pkt);
        if(ret)
            av_log(NULL, AV_LOG_ERROR, "failed to write_segment:dc_gpac_video_isom_tile_write\n");
    }else{
        os->video_out[0]->encoded_frame_size = pkt->size;
        os->video_out[0]->cur_pts = pkt->pts;
	os->video_out[0]->cur_keyframe = (pkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0;
        
        os->video_out[0]->vbuf = pkt->data;
        os->video_out[0]->vbuf_size = pkt->size;
        
        ret = dc_gpac_video_isom_write(os, 0);
        if(ret)
            av_log(NULL, AV_LOG_ERROR, "failed to write_segment:dc_gpac_video_isom_write\n");
    }
    
    av_log(NULL, AV_LOG_DEBUG, "frame_per_fragment=%d, os->nb_frames=%d, os->nb_frames MOD os->frame_per_fragment=%d \n",
                                os->frame_per_fragment, os->nb_frames, 
                                os->nb_frames%os->frame_per_fragment);
    /// reach the segment duration, close current segment and init an new one.
    if(os->nb_frames%os->frame_per_fragment == os->frame_per_fragment - 1){
        for(i=0; i<os->nb_tiles+1; i++){
            gf_isom_flush_fragments(os->video_out[i]->isof, GF_TRUE);
        }
        os->fragment_started = 0;
    }
    
    av_log(NULL, AV_LOG_DEBUG, "frame_per_segment=%d, os->nb_frames%d, os->nb_frames MOD os->frame_per_segment=%d \n",
                                os->frame_per_segment, os->nb_frames, 
                                os->nb_frames%os->frame_per_segment);
    if(os->nb_frames%os->frame_per_segment == os->frame_per_fragment - 1){
        for(i=0; i<os->nb_tiles+1; i++){
            gpac_video_isom_close(os, i);
            os->video_out[i]->nb_segments++;
            os->video_out[i]->segment_index++;
            snprintf(os->video_out[i]->seg_media_name, sizeof(os->video_out[i]->seg_media_name), "%s%s_track%d_%d.m4s", 
                os->dir_name, os->out_name, os->video_out[i]->dash_track_ID, os->video_out[i]->segment_index+1);
        }
        os->segment_started = 0;
        
        
        if (ctx->streaming) {
            for(i=0; i<os->nb_tiles+1; i++){
                av_log(NULL, AV_LOG_DEBUG, "$$$$ windows_size=%d, extra_window_size %d, video_out[%d]->nb_segments=%d, segment_index=%d\n", 
                                                    ctx->window_size, ctx->extra_window_size, i, os->video_out[i]->nb_segments,
                                                    os->video_out[i]->segment_index);
                int remove_cnt = os->video_out[i]->nb_segments - ctx->window_size - ctx->extra_window_size;
                if (remove_cnt > 0)
                {
                        
                    snprintf(filename, sizeof(filename), "%s%s_track%d_%d.m4s", 
                                  os->dir_name, os->out_name, os->video_out[i]->dash_track_ID, remove_cnt);
                    remove(filename);
                    av_log(NULL, AV_LOG_DEBUG, "remove file %s\n", filename);
                           
                }
            }
        }
    }
    os->nb_frames++;
    
    return 0;
}

void dash_write_mpd(GPAC_DASHContext *ctx, int is_final)
{
	u32 sec;
	time_t gtime;
	struct tm *t;
	FILE *f;
        char mpd_name[1024];
	char name[GF_MAX_PATH];
        int duration = 0;
        int hour, minute, second, msecond;
        char presentation_duration[1024];
        int start_number = 1;
        DashOutStream *os = &ctx->streams[0];

	snprintf(name, sizeof(name), "%s%s.mpd", ctx->dirname, ctx->out_name);
        snprintf(mpd_name, sizeof(mpd_name), "%s.mpd", ctx->out_name);

	f = gf_fopen(name, "w");
	//TODO: if (!f) ...

	//	time_t t = time(NULL);
	//	time_t t2 = t + 2;
	//	t += (2 * (cmddata->seg_dur / 1000.0));
	//	tm = *gmtime(&t2);
	//	snprintf(availability_start_time, "%d-%d-%dT%d:%d:%dZ", tm.tm_year + 1900,
	//			tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	//	fprintf(stdout, "%s \n", availability_start_time);

	fprintf(f, "<?xml version=\"1.0\"?>\n");
    fprintf(f, "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\"  minBufferTime=\"PT%fS\" maxSegmentDuration=\"PT%fS\"", (double)(os->seg_dur/1000), (double)(os->seg_dur/1000));
    fprintf(f, " profiles=\"%s\"", ctx->streaming ? "urn:mpeg:dash:profile:isoff-live:2011" : "rn:mpeg:dash:profile:isoff-on-demand:2011");
        fprintf(f, " type=\"%s\"", is_final ? "static" : "dynamic");

    if (ctx->streaming)
    {
        start_number = 0;
    }
    if (is_final && ctx->streaming) {
        ctx->streaming = 0;
        os->total_frames = os->total_frames % os->frame_per_fragment;
    }

    if (is_final)
    {
        duration = (int)(os->total_frames * 1000 / os->frame_rate + 0.5);
        hour = duration / 3600000;
        duration = duration % 3600000;
        minute = duration / 60000;
        duration = duration % 60000;
        second = duration / 1000;
        msecond = duration % 1000;
        snprintf(presentation_duration, sizeof(presentation_duration), "PT%02dH%02dM%02d.%03dS", hour, minute, second, msecond);
        fprintf(f, " mediaPresentationDuration=\"%s\"", presentation_duration);
    } else {
        gf_net_get_ntp(&sec, NULL);
        gtime = sec - GF_NTP_SEC_1900_TO_1970;
        t = gmtime(&gtime);
        if (os->video_out[0]->segment_index == 0) {
            snprintf(os->available_start_time, sizeof(os->available_start_time), "%d-%d-%dT%d:%d:%dZ", 1900+t->tm_year,
                    t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
        }
        fprintf(f, " availabilityStartTime=\"%s\"", os->available_start_time);
        fprintf(f, " timeShiftBufferDepth=\"PT5M\"");
        if (os->minimum_update_period > 0)
    	    fprintf(f, " minimumUpdatePeriod=\"PT%dS\"", os->minimum_update_period);

        fprintf(f, " publishTime=\"%d-%02d-%02dT%02d:%02d:%02dZ\"", 1900+t->tm_year, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    }
	fprintf(f, ">\n");

	fprintf(f,
	        " <ProgramInformation moreInformationURL=\"http://gpac.io\">\n"
	        "  <Title>%s</Title>\n"
	        " </ProgramInformation>\n", mpd_name);

        if (ctx->base_url) {
	    if (strcmp(ctx->base_url, "") != 0) {
	        fprintf(f, " <BaseURL>%s</BaseURL>\n", ctx->base_url);
	    }
        }
        if (!ctx->streaming)
        {
            fprintf(f, " <Period duration=\"%s\">\n", presentation_duration);
        }
        else
        {
	    fprintf(f, " <Period start=\"PT0H0M0.000S\" id=\"P1\">\n");
        }
        

        for (int stream_idx = 0; stream_idx < ctx->nb_streams; stream_idx++)
        {
            os = &ctx->streams[stream_idx];
	    fprintf(f, "  <AdaptationSet segmentAlignment=\"true\" maxWidth=\"%d\" maxHeight=\"%d\" bitstreamSwitching=\"false\">\n",
                        os->max_width, os->max_height);
            fprintf(f, "   <EssentialProperty schemeIdUri=\"urn:mpeg:dash:srd:2014\" value=\"1,0,0,0,0\"/>\n");
            fprintf(f,
	        "   <SegmentTemplate initialization=\"%s\"/>\n",
	        os->video_out[0]->seg_init_name);
            fprintf(f, "   <Representation id=\"%s\" mimeType=\"video/mp4\" codecs=\"%s\" "
                        "width=\"%d\" height=\"%d\" frameRate=\"%d/%d\" sar=\"1:1\" startWithSAP=\"1\" bandwidth=\"%d\">\n",
                        os->video_out[0]->rep_id,
                        "hvc2.1.6.L186.80",
                        os->max_width, os->max_height, os->timescale.den,
                        os->timescale.num, os->bit_rate);
            fprintf(f, "    <SegmentTemplate timescale=\"%d\" duration=\"%d\" media=\"%s\""
                        " startNumber=\"%d\"/>\n",
                        os->video_out[0]->timescale, (os->seg_dur * os->video_out[0]->timescale) / 1000, os->video_out[0]->seg_media_name_tmpl, start_number);
            fprintf(f, "   </Representation>\n");
            fprintf(f, "  </AdaptationSet>\n");

	    for (int tile_idx = 1; tile_idx < os->nb_tiles + 1; tile_idx++) {
		fprintf(f, "  <AdaptationSet segmentAlignment=\"true\" maxWidth=\"%d\" maxHeight=\"%d\" bitstreamSwitching=\"false\">\n",
                            os->video_out[tile_idx]->tile.tw, os->video_out[tile_idx]->tile.th);
                fprintf(f, "   <SupplementalProperty schemeIdUri=\"urn:mpeg:dash:srd:2014\" value=\"1,%d,%d,%d,%d\"/>\n",
                            os->video_out[tile_idx]->tile.tx, os->video_out[tile_idx]->tile.ty,
                            os->video_out[tile_idx]->tile.tw, os->video_out[tile_idx]->tile.th);
		fprintf(f, "   <Representation id=\"%s\" mimeType=\"video/mp4\" codecs=\"%s\" "
		        "width=\"%d\" height=\"%d\" frameRate=\"%d/%d\" sar=\"1:1\" startWithSAP=\"1\" bandwidth=\"%d\" dependencyId=\"%d\">\n",
		        os->video_out[tile_idx]->rep_id, "hvc2.1.6.L186.80",
		        os->video_out[tile_idx]->tile.tw, os->video_out[tile_idx]->tile.th, os->timescale.den, os->timescale.num,
		        os->video_out[tile_idx]->bit_rate, os->video_out[tile_idx]->dependency_id);
                fprintf(f, "    <SegmentTemplate timescale=\"%d\" duration=\"%d\" media=\"%s\""
                            " startNumber=\"%d\"/>\n",
                            os->video_out[tile_idx]->timescale, (os->seg_dur * os->video_out[tile_idx]->timescale) / 1000, os->video_out[tile_idx]->seg_media_name_tmpl, start_number);
                fprintf(f, "   </Representation>\n");
                fprintf(f, "  </AdaptationSet>\n");
	    }

        }

	fprintf(f, " </Period>\n");

	fprintf(f, "</MPD>\n");

	gf_fclose(f);
}
int dash_update_mpd(GPAC_DASHContext* dash_ctx, int is_final)
{
    DashOutStream *os = &dash_ctx->streams[0];
    if (dash_ctx->window_size) {
        if (os->video_out[0]->segment_index % dash_ctx->window_size == 0)
        {
            dash_write_mpd(dash_ctx, is_final);
            return 0;
        }
    }
    else {
        if (os->nb_frames % os->frame_per_fragment == 0)
        {
            dash_write_mpd(dash_ctx, is_final);
            return 0;
        }
    }

    if (is_final) {
        dash_write_mpd(dash_ctx, is_final);
        return 0;
    }

    return 0;
}
