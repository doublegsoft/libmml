/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>

#include "libmml.h"
#include "libmml-internal.h"

static char err_msg[4096 * 4];

int 
mml_encoder_init(mml_encoder_p* encoder, int encoder_id)
{
  *encoder = (mml_encoder_p)malloc(sizeof(mml_encoder_t));
  if (!(*encoder)) 
    return MML_ERROR_CODEC_NOT_CREATED;
  (*encoder)->enc = (AVCodec*) avcodec_find_encoder(encoder_id);
  if (!(*encoder)->enc)
    return MML_ERROR_CODEC_NOT_FOUND;
  (*encoder)->ctx = avcodec_alloc_context3((*encoder)->enc);
  if (!(*encoder)->ctx)
    return MML_ERROR_CODEC_NOT_CREATED;
  (*encoder)->pkt = av_packet_alloc();
  if (!(*encoder)->pkt)
    return MML_ERROR_PACKET_NOT_CREATED;
  return MML_SUCCESS;
}

void
mml_encoder_free(mml_encoder_p encoder) 
{
  if (encoder == NULL) 
    return;
  if (encoder->ctx != NULL) 
    avcodec_free_context(&encoder->ctx);
  if (encoder->pkt != NULL)
    av_packet_free(&encoder->pkt);
  free(encoder);
  encoder = NULL;
}

/*!
** Instantiates encoder codec context and codec objects.
**
** @param filename
**				the output file name
** 
** @param codec_id
**				the codec id
**
** @param fmt_ctx
**				the format context instance variable
**
** @param enc_ctx
**				the encoder context instance variable
**
** @param enc
**				the encoder codec instance variable
**
** @return success or error code
*/
static int 
mml_enc_init(const char* 							filename, 
             int 										  codec_id,
             AVFormatContext** 				fmt_ctx, 
             AVCodecContext** 				enc_ctx,
             AVCodec**				        enc)
{
	int 				ret;
  
  avformat_alloc_output_context2(fmt_ctx, NULL, NULL, filename);
  if (!(*fmt_ctx))
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "failed to create encoder format context");
    return ret;
  }

  *enc = (AVCodec*)avcodec_find_encoder(codec_id);
  if (!(*enc))
  {
    ret = MML_ERROR_CODEC_NOT_FOUND;
    sprintf(err_msg, "no codec found for id: %d", codec_id);
    return ret;
  }

  *enc_ctx = avcodec_alloc_context3(*enc);
  if (!(*enc_ctx)) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "no encoder codec created for id: %d", codec_id);
    return ret;
  }
  
  return MML_SUCCESS;
}

/*!
**
*/
static int 
mml_format_open(const char* 							filename, 
               	AVFormatContext** 				fmt_ctx)
{
	int 				ret;
  if ((ret = avformat_open_input(fmt_ctx, filename, NULL, NULL)) < 0) 
  {
		ret = MML_ERROR_FILE_OPEN_FAILED;
    sprintf(err_msg, "'%s' file not open", filename);
    return ret;
  }
  
  return MML_SUCCESS;
}

/*!
**
*/
static int 
mml_stream_open(const char* 							filename, 
                int												stream_type,
               	AVFormatContext** 				fmt_ctx, 
               	AVCodecContext** 					codec_ctx,
               	AVStream**								stream,
               	int* 											stream_index) 
{
  int 				ret;
  AVCodec* 		codec;

  if ((ret = mml_format_open(filename, fmt_ctx)) != MML_SUCCESS) 
  	return ret;

  if ((ret = avformat_find_stream_info(*fmt_ctx, NULL)) < 0) 
  {
  	ret = MML_ERROR_STREAM_NOT_FOUND;
   	sprintf(err_msg, "'%s' stream not found", filename);
    return ret;
  }

  for (int i = 0; i < (*fmt_ctx)->nb_streams; i++) 
  {
    if ((*fmt_ctx)->streams[i]->codecpar->codec_type == stream_type) 
    {
      *stream_index = i;
      *stream = (*fmt_ctx)->streams[i];
      break;
    }
  }

  if (!(*stream)) 
  {
    ret = MML_ERROR_STREAM_NOT_FOUND;
    sprintf(err_msg, "no audio stream found for '%s'", filename);
		return ret;
  }

  codec = (AVCodec*)avcodec_find_decoder((*stream)->codecpar->codec_id);
  if (!codec) 
  {
    ret = MML_ERROR_CODEC_NOT_FOUND;
    sprintf(err_msg, "no codec found for id: %d", (*stream)->codecpar->codec_id);
    return ret;
  }

  *codec_ctx = avcodec_alloc_context3(codec);
  if (!*codec_ctx) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "no codec created for id: %d", (*stream)->codecpar->codec_id);
    return ret;
  }

  if ((ret = avcodec_parameters_to_context(*codec_ctx, (*stream)->codecpar)) < 0) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "codec parameters copy failed for stream");
    return ret;
  }

  if ((ret = avcodec_open2(*codec_ctx, codec, NULL)) < 0) 
  {
    ret = MML_ERROR_CODEC_OPEN_FAILED;
    sprintf(err_msg, "failed to open codec for '%s'", filename);
    return ret;
  }

  return MML_SUCCESS;
}

/*!
** Creates new stream encoder codec context and codec objects.
**
** @param fmt_ctx
**				the format context instance 
**
** @param enc_ctx
**				the encoder context instance
**
** @param enc
**				the encoder codec instance 
**
** @param stream
**				the stream instance variable
**
** @return success or error code
*/
static int 
mml_stream_new(AVFormatContext* 				fmt_ctx, 
               AVCodecContext* 					enc_ctx,
               AVCodec* 								enc,
               AVStream**								stream) 
{
  int ret;
  if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) 
  {
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
	
  ret = avcodec_open2(enc_ctx, enc, NULL);
  if (ret < 0) 
  {
    char err[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, err, sizeof(err));
    ret = MML_ERROR_CODEC_OPEN_FAILED;
    sprintf(err_msg, "failed to open encoder codec: %s", err);
    return ret;
  }

  *stream = avformat_new_stream(fmt_ctx, NULL);
  if (!(*stream)) 
  {
    ret = MML_ERROR_STREAM_NOT_CREATED;
    sprintf(err_msg, "failed to create stream");
    return ret;
  }

  if (avcodec_parameters_from_context((*stream)->codecpar, enc_ctx) < 0) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "encoder codec parameters copy failed for stream");
    return ret;
  }
	return MML_SUCCESS;
}

/*!
** Remux audio streams.
*/
static void
mml_stream_remux(AVPacket* pkt, 
                 AVRational dec_tb, 
                 AVRational enc_tb,
                 AVFormatContext* output_fmt_ctx,
                 int64_t* prev_dts, 
                 int64_t* prev_pts, 
                 int64_t* prev_dur, 
                 int64_t* offset_dts, 
                 int64_t* offset_pts)
{
  int rc;
	pkt->pts = av_rescale_q_rnd(pkt->pts, dec_tb, enc_tb, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
  pkt->dts = av_rescale_q_rnd(pkt->dts, dec_tb, enc_tb, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
  pkt->duration = av_rescale_q(pkt->duration, dec_tb, enc_tb);
  pkt->pos = -1;
	
  int64_t orig_pts = pkt->pts;
  int64_t orig_dts = pkt->dts;

  pkt->pts += (*offset_pts);
  pkt->dts += (*offset_dts);

  /*!
  ** The input video file wrapped around - we have to fix dts_offset for 
  ** keeping the DTS monotonous increment.
  */
  if (pkt->dts <= (*prev_dts))
  {
    /*!
    ** Wrapped around...
    ** Set the DTS to be the previous DTS plus duration of a single packet.
    ** Note: add the duration of the previous packet instead of adding 
    ** (*pkt)->duration, because the last audio packet in the file may be 
    ** shorter than the nominal audio packet duration.
    */
    pkt->dts = (*prev_dts) + (*prev_dur);
    *offset_dts = pkt->dts - orig_dts;
  }
  
  /*!
  ** Use the same solution for the PTS
  ** In case of audio, the PTS are monotonously increased.
  ** In case of video the PTS may not be monotonously increased 
  ** (due to B-Frames).
  ** The solution may not work for video packets.
  */
  if (pkt->pts <= (*prev_pts))
  {
    /*!
    ** Set the PTS to be the previous PTS plus duration 
    ** of a single packet.
    */
    pkt->pts = (*prev_pts) + (*prev_dur); 
    *offset_pts = pkt->pts - orig_pts;
  }

WRITE:  
  *prev_dts = pkt->dts;
  *prev_pts = pkt->pts;
  *prev_dur = pkt->duration;
  
  rc = av_interleaved_write_frame(output_fmt_ctx, pkt);
}

static void
mml_stream_transcode(AVStream* input_video_stream,
                     int stream_index,
  									AVCodecContext* dec_ctx, 
                     AVPacket* pkt,
                     AVStream* output_video_stream,
                     AVFormatContext* output_fmt_ctx,
                     AVCodecContext* enc_ctx,
                     int64_t* prev_dts, 
                     int64_t* prev_pts, 
                     int64_t* prev_dur, 
                     int64_t* offset_dts, 
                     int64_t* offset_pts)
{
  AVFrame* frame = av_frame_alloc();
  frame->pict_type = AV_PICTURE_TYPE_NONE;
  
	int rc = avcodec_send_packet(dec_ctx, pkt);
  while (rc >= 0)
  {
    rc = avcodec_receive_frame(dec_ctx, frame);

    if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
      break;
    if (rc >= 0)
    {
      AVPacket* out_pkt = av_packet_alloc();
      int rc = avcodec_send_frame(enc_ctx, frame);
      if (rc < 0) 
      {
				char error_buf[128];
        av_strerror(rc, error_buf, sizeof(error_buf));
        printf("error: %s\n", error_buf);
      }
      while (rc >= 0)
      {
        rc = avcodec_receive_packet(enc_ctx, out_pkt);
        
        int64_t orig_pts = out_pkt->pts;
        int64_t orig_dts = out_pkt->dts;

        out_pkt->pts += (*offset_pts);
        out_pkt->dts += (*offset_dts);
        
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
          break;
        
        out_pkt->stream_index = stream_index;
        out_pkt->duration = input_video_stream->time_base.den / input_video_stream->time_base.num;

        av_packet_rescale_ts(out_pkt, input_video_stream->time_base, output_video_stream->time_base);
        
        if (out_pkt->dts <= (*prev_dts))
        {
          /*!
          ** Wrapped around...
          ** Set the DTS to be the previous DTS plus duration of a single packet.
          ** Note: add the duration of the previous packet instead of adding 
          ** (*pkt)->duration, because the last audio packet in the file may be 
          ** shorter than the nominal audio packet duration.
          */
          out_pkt->dts = (*prev_dts) + (*prev_dur);
          *offset_dts = out_pkt->dts - orig_dts;
        }

        /*!
        ** Use the same solution for the PTS
        ** In case of audio, the PTS are monotonously increased.
        ** In case of video the PTS may not be monotonously increased 
        ** (due to B-Frames).
        ** The solution may not work for video packets.
        */
        if (out_pkt->pts <= (*prev_pts))
        {
          /*!
          ** Set the PTS to be the previous PTS plus duration 
          ** of a single packet.
          */
          out_pkt->pts = (*prev_pts) + (*prev_dur); 
          *offset_pts = out_pkt->pts - orig_pts;
        }

        *prev_dts = out_pkt->dts;
        *prev_pts = out_pkt->pts;
        *prev_dur = out_pkt->duration;
        rc = av_interleaved_write_frame(output_fmt_ctx, out_pkt);
      }
      av_packet_unref(out_pkt);
      av_packet_free(&out_pkt);
    }
    av_frame_unref(frame);
  }
  av_frame_free(&frame);
}

/**
 * Add padding to a scaled frame, centering the frame and filling the remaining area with black.
 * 
 * @param padded_data      The data pointers for the padded frame.
 * @param padded_linesize  The linesize for the padded frame.
 * @param scaled_data      The data pointers for the scaled frame.
 * @param scaled_linesize  The linesize for the scaled frame.
 * @param scaled_width     The width of the scaled frame.
 * @param scaled_height    The height of the scaled frame.
 * @param pad_left         The number of pixels to pad on the left.
 * @param pad_right        The number of pixels to pad on the right.
 * @param pad_top          The number of pixels to pad on the top.
 * @param pad_bottom       The number of pixels to pad on the bottom.
 * @param target_width     The width of the target frame.
 * @param target_height    The height of the target frame.
 * @param target_format    The pixel format of the target frame.
 */
static void 
mml_frame_pad(uint8_t *padded_data[AV_NUM_DATA_POINTERS], int padded_linesize[AV_NUM_DATA_POINTERS],
              const uint8_t *scaled_data[AV_NUM_DATA_POINTERS], int scaled_linesize[AV_NUM_DATA_POINTERS],
              int scaled_width, int scaled_height,
              int pad_left, int pad_right, int pad_top, int pad_bottom,
              int target_width, int target_height, enum AVPixelFormat target_format)
{
  // Ensure the format is YUV420P
  if (target_format != AV_PIX_FMT_YUV420P) {
    fprintf(stderr, "Unsupported pixel format\n");
    return;
  }

  // Fill the padded frame with black (Y=0, U=128, V=128 for YUV420P)
  for (int y = 0; y < target_height; y++) {
    for (int x = 0; x < target_width; x++) {
      padded_data[0][y * padded_linesize[0] + x] = 0; // Y plane
      if (y < target_height / 2 && x < target_width / 2) {
        padded_data[1][y * padded_linesize[1] + x] = 128; // U plane
        padded_data[2][y * padded_linesize[2] + x] = 128; // V plane
      }
    }
  }

  // Copy the scaled frame into the padded frame at the centered position
  int y_offset = pad_top;
  int x_offset = pad_left;

  for (int y = 0; y < scaled_height; y++) {
    memcpy(padded_data[0] + (y + y_offset) * padded_linesize[0] + x_offset,
           scaled_data[0] + y * scaled_linesize[0],
           scaled_width);
  }

  for (int y = 0; y < scaled_height / 2; y++) {
    memcpy(padded_data[1] + (y + y_offset / 2) * padded_linesize[1] + x_offset / 2,
           scaled_data[1] + y * scaled_linesize[1],
           scaled_width / 2);
    memcpy(padded_data[2] + (y + y_offset / 2) * padded_linesize[2] + x_offset / 2,
           scaled_data[2] + y * scaled_linesize[2],
           scaled_width / 2);
  }
}

/*
********************************************************************************
**
** mml_error
**
********************************************************************************
*/
const char*
mml_error(void)
{
	return err_msg;
}

/*
********************************************************************************
**
** mml_audio_remove
**
********************************************************************************
*/
int  
mml_audio_remove(const char* original_video_path, const char* output_video_path)
{
  int ret = MML_SUCCESS;
  AVFormatContext* input_format_context = NULL;
  AVFormatContext* output_format_context = NULL;
  
  if (avformat_open_input(&input_format_context, original_video_path, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open input file.\n");
    return -1;
  }

  if (avformat_find_stream_info(input_format_context, NULL) < 0) {
    fprintf(stderr, "Could not find stream information.\n");
    return -1;
  }

  avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_video_path);
  if (!output_format_context) {
    fprintf(stderr, "Could not create output context.\n");
    return -1;
  }

  // 5. 复制视频流到输出上下文
  int video_stream_index = -1;
  for (int i = 0; i < input_format_context->nb_streams; i++) 
  {
    if (input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) 
    {
      video_stream_index = i;
      AVStream* in_stream = input_format_context->streams[i];
      const AVCodec* codec = avcodec_find_decoder(in_stream->codecpar->codec_id);
      AVStream* out_stream = avformat_new_stream(output_format_context, codec);
      if (!out_stream) 
      {
        fprintf(stderr, "Failed to create output video stream.\n");
        return -1;
      }
      output_format_context->streams[output_format_context->nb_streams - 1] = out_stream;
      if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) 
      {
        ret = MML_ERROR_CODEC_NOT_COPIED;
        sprintf(err_msg, "failed to copy codec parameters");
        goto RELEASE;
      }
      out_stream->sample_aspect_ratio = in_stream->sample_aspect_ratio;
      break;
    }
  }
  if (video_stream_index == -1)
  {
    fprintf(stderr, "Could not find video stream.\n");
    return -1;
  }

  // 6. 打开输出文件
  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) 
  {
    if (avio_open(&output_format_context->pb, output_video_path, AVIO_FLAG_WRITE) < 0) 
    {
      fprintf(stderr, "Could not open output file.\n");
      return -1;
    }
  }

  // 7. 写入输出文件的头部信息
  if (avformat_write_header(output_format_context, NULL) < 0) {
    fprintf(stderr, "Could not write header.\n");
    return -1;
  }

  // 8. 读取输入视频文件的视频帧
  AVPacket packet;
  while (av_read_frame(input_format_context, &packet) >= 0) 
  {
    if (packet.stream_index == video_stream_index) 
    {
      // 9. 将视频帧写入输出文件
      // 复制元数据
      av_packet_rescale_ts(&packet, input_format_context->streams[packet.stream_index]->time_base, output_format_context->streams[0]->time_base);
      if (av_interleaved_write_frame(output_format_context, &packet) < 0) {
        fprintf(stderr, "Could not write video frame.\n");
      }
    }
    av_packet_unref(&packet);
  }

  av_write_trailer(output_format_context);

RELEASE:
  
  avformat_free_context(input_format_context);
  avformat_free_context(output_format_context);

	return 0;
}

/*
********************************************************************************
**
** mml_audio_exist
**
********************************************************************************
*/
int
mml_audio_exist(const char* original_video_path)
{
  AVFormatContext* format_context = NULL;
  if (avformat_open_input(&format_context, original_video_path, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open input file.\n");
    return -1;
  }

  if (avformat_find_stream_info(format_context, NULL) < 0) {
    fprintf(stderr, "Could not find stream information.\n");
    return -1;
  }

  int has_audio = 0;
  for (unsigned i = 0; i < format_context->nb_streams; i++) {
    if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      has_audio = 1;
      break;
    }
  }

  avformat_close_input(&format_context);
  return has_audio;
}

/*
********************************************************************************
**
** mml_audio_extract
**
********************************************************************************
*/
int 
mml_audio_extract(const char* original_video_path, 
                  const char* output_audio_path)
{
  int									 ret 										= MML_SUCCESS;
  AVFormatContext* 	 		input_format_context	 	= NULL;
  AVFormatContext*	 		output_format_context 	= NULL;
  AVCodecContext*		 		input_codec_context 		= NULL;
  AVCodecContext*		 		output_codec_context 		= NULL;
  AVCodec*		 			 		input_codec 						= NULL;
  AVCodec*		 			 	 	output_codec 						= NULL;
  AVCodecParameters*  	codecpar								=	NULL;
  SwrContext*						swr_ctx 								= NULL;
  AVPacket 							packet;
  AVFrame* 							frame 									= NULL;
  
  ret = avformat_open_input(&input_format_context, 
                            original_video_path, 
                            NULL, 
                            NULL);
  if (ret < 0)
  {
    ret = MML_ERROR_FILE_OPEN_FAILED;
    sprintf(err_msg, "'%s' file not open", original_video_path);
    goto RELEASE;
  }

  ret = avformat_find_stream_info(input_format_context, NULL);
  if (ret < 0) 
  {
    ret = MML_ERROR_STREAM_NOT_FOUND;
   	sprintf(err_msg, "'%s' stream not found", original_video_path);
    goto RELEASE;
  }
	
  /*!
  ** 查找音频流索引号
  */
  int audio_stream_index = -1;
  for (int i = 0; i < input_format_context->nb_streams; i++) 
  {
    codecpar = input_format_context->streams[i]->codecpar;
    if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
        codecpar->codec_id == AV_CODEC_ID_AAC) 
    {
      audio_stream_index = i;
      break;
    }
  }

  if (audio_stream_index == -1) 
  {
    ret = MML_ERROR_STREAM_NOT_FOUND;
    sprintf(err_msg, "no audio stream found for '%s'", original_video_path);
		goto RELEASE;
  }
  
  /*! 
  ** 打开AAC解码器（这里我们只提取AAC流，不进行解码）
  */
  codecpar = input_format_context->streams[audio_stream_index]->codecpar;
  input_codec = (AVCodec*) avcodec_find_decoder(codecpar->codec_id);
  if (!input_codec) 
  {
    ret = MML_ERROR_CODEC_NOT_FOUND;
    sprintf(err_msg, "no codec found for id: %d", codecpar->codec_id);
    goto RELEASE;
  }
  input_codec_context = avcodec_alloc_context3(input_codec);
  if (!input_codec_context) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "no codec created for id: %d", codecpar->codec_id);
    goto RELEASE;
  }

  if (avcodec_parameters_to_context(input_codec_context, codecpar) < 0) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "codec parameters copy failed for input codec context");
    goto RELEASE;
  }

  if (avcodec_open2(input_codec_context, input_codec, NULL) < 0) {
    ret = MML_ERROR_CODEC_OPEN_FAILED;
    sprintf(err_msg, "failed to open input codec");
    goto RELEASE;
  }
  
  ret = avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_audio_path);
  if (!output_format_context) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "failed to allocate output format context for '%s'", output_audio_path);
    goto RELEASE;
  }
  
  // Find output codec
  output_codec = (AVCodec*)avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!output_codec) 
  {
    ret = MML_ERROR_CODEC_NOT_FOUND;
    sprintf(err_msg, "no codec found for id: %d", AV_CODEC_ID_AAC);
    goto RELEASE;
  }

  // Allocate codec context for output stream
  output_codec_context = avcodec_alloc_context3(output_codec);
  if (!output_codec_context) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "failed to allocate output codec context");
    goto RELEASE;
  }

  output_codec_context->ch_layout = input_codec_context->ch_layout;
  output_codec_context->sample_rate = input_codec_context->sample_rate;
  // output_codec_context->sample_fmt = output_codec->sample_fmts[0];
  output_codec_context->bit_rate = output_codec_context->bit_rate;

  if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) 
  {
    output_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  if (avcodec_open2(output_codec_context, output_codec, NULL) < 0) 
  {
    ret = MML_ERROR_CODEC_OPEN_FAILED;
    sprintf(err_msg, "failed to open output codec");
    goto RELEASE;
  }

  AVStream* output_audio_stream = avformat_new_stream(output_format_context, NULL);
  if (!output_audio_stream) 
  {
    ret = MML_ERROR_STREAM_NOT_CREATED;
    sprintf(err_msg, "failed to create output audio stream");
    goto RELEASE;
  }

  if (avcodec_parameters_from_context(output_audio_stream->codecpar, output_codec_context) < 0) 
  {
    ret = MML_ERROR_STREAM_NOT_CREATED;
    sprintf(err_msg, "failed to copy codec parameters for output audio stream");
    goto RELEASE;
  }

  output_audio_stream->time_base = (AVRational){1, output_codec_context->sample_rate};

  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&output_format_context->pb, output_audio_path, AVIO_FLAG_WRITE) < 0) 
    {
      ret = MML_ERROR_FILE_OPEN_FAILED;
      sprintf(err_msg, "failed to open file '%s'", output_audio_path);
      goto RELEASE;
    }
  }

  if (avformat_write_header(output_format_context, NULL) < 0) 
  {
    ret = MML_ERROR_STREAM_WRITE_FAILED;
    sprintf(err_msg, "failed to write header to '%s'", output_audio_path);
    goto RELEASE;
  }

  // Initialize the resampler to convert the input audio to the output format
  swr_ctx = swr_alloc();
  av_opt_set_int(swr_ctx, "in_sample_rate", input_codec_context->sample_rate, 0);
  av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", input_codec_context->sample_fmt, 0);
  av_opt_set_int(swr_ctx, "out_sample_rate", output_codec_context->sample_rate, 0);
  av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", output_codec_context->sample_fmt, 0);
  swr_init(swr_ctx);
  
  frame = av_frame_alloc();
  while (av_read_frame(input_format_context, &packet) >= 0) {
    if (packet.stream_index == audio_stream_index) {
      if (avcodec_send_packet(input_codec_context, &packet) == 0) {
        while (avcodec_receive_frame(input_codec_context, frame) == 0) {
          AVFrame *resampled_frame = av_frame_alloc();
          resampled_frame->ch_layout = output_codec_context->ch_layout;
          resampled_frame->sample_rate = output_codec_context->sample_rate;
          resampled_frame->format = output_codec_context->sample_fmt;
          resampled_frame->nb_samples = frame->nb_samples;

          if (av_frame_get_buffer(resampled_frame, 0) < 0) {
            fprintf(stderr, "Could not allocate output frame samples\n");
            return -1;
          }

          if (swr_convert_frame(swr_ctx, resampled_frame, frame) < 0) {
            fprintf(stderr, "Error while converting\n");
            return -1;
          }

          if (avcodec_send_frame(output_codec_context, resampled_frame) < 0) {
            fprintf(stderr, "Error sending frame to encoder\n");
            return -1;
          }

          AVPacket* out_packet = av_packet_alloc();

          while (avcodec_receive_packet(output_codec_context, out_packet) == 0) {
            av_packet_rescale_ts(out_packet, 
                                 output_codec_context->time_base, 
                                 output_audio_stream->time_base);
            out_packet->stream_index = output_audio_stream->index;
            if (av_interleaved_write_frame(output_format_context, out_packet) < 0) {
              fprintf(stderr, "Error writing frame\n");
              return -1;
            }
            av_packet_unref(out_packet);
          }
          av_packet_free(&out_packet);
          av_frame_free(&resampled_frame);
        }
      }
      av_packet_unref(&packet);
    }
  }

  av_write_trailer(output_format_context);

  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) 
  {
    avio_closep(&output_format_context->pb);
  }

RELEASE:
	if (input_codec_context != NULL)
    avcodec_free_context(&input_codec_context);
  if (output_codec_context != NULL)
    avcodec_free_context(&output_codec_context);
  if (input_format_context != NULL)
  	avformat_close_input(&input_format_context);
 	if (output_format_context != NULL)
  	avformat_close_input(&output_format_context);
  if (swr_ctx != NULL)
    swr_free(&swr_ctx);
  if (frame != NULL) 
    av_frame_free(&frame);

  return ret;
}

/*
********************************************************************************
**
** mml_video_resolution
**
********************************************************************************
*/
int
mml_video_resolution(const char* 		original_video_path, 
                     int* 					width, 
                     int* 					height)
{
  AVFormatContext* format_context = NULL;
  int ret = avformat_open_input(&format_context, original_video_path, NULL, NULL);
  if (ret < 0) 
  {
    fprintf(stderr, "Could not open input file '%s'.\n", original_video_path);
    return ret;
  }

  ret = avformat_find_stream_info(format_context, NULL);
  if (ret < 0) 
  {
    fprintf(stderr, "Could not find stream information.\n");
    return ret;
  }

  int video_stream_index = -1;
  for (int i = 0; i < format_context->nb_streams; i++) 
  {
    if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) 
    {
      video_stream_index = i;
      break;
    }
  }

  if (video_stream_index == -1) 
  {
    fprintf(stderr, "Could not find video stream in the input file.\n");
    return -1;
  }

  AVCodecParameters* codecpar = format_context->streams[video_stream_index]->codecpar;
  *width = codecpar->width;
  *height = codecpar->height;
	
 	if (format_context != NULL)
  	avformat_close_input(&format_context);
  
  return MML_SUCCESS;
}

/*
********************************************************************************
**
** mml_video_resize
**
********************************************************************************
*/
int
mml_video_resize(const char* 	original_path, 
                 const char* 	output_path, 
                 int 					width, 
                 int 					height)
{
	AVFormatContext* input_format_context = NULL;
  AVFormatContext* output_format_context = NULL;
  AVCodecContext* input_codec_context = NULL;
  AVCodecContext* output_codec_context = NULL;
  AVCodec* output_codec = NULL;
  AVStream* input_video_stream = NULL;
  AVStream* output_video_stream = NULL;
  AVPacket* packet = NULL;
  AVFrame* frame = NULL;
  AVFrame* scaled_frame = NULL;
  struct SwsContext *sws_ctx = NULL;
  int video_stream_index = -1;
  int ret;

  ret = mml_stream_open(original_path, 
                       	AVMEDIA_TYPE_VIDEO, 
                       	&input_format_context, 
                       	&input_codec_context,
                       	&input_video_stream,
                       	&video_stream_index);
  
  if (ret != MML_SUCCESS)
		goto RELEASE;
  
  input_codec_context->time_base = input_video_stream->time_base;

  ret = mml_enc_init(output_path, 
                     AV_CODEC_ID_H264, 
                     &output_format_context, 
                     &output_codec_context,
                     &output_codec);

  output_codec_context->width = width;
  output_codec_context->height = height;
  output_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
  output_codec_context->time_base = input_codec_context->time_base;
  output_codec_context->bit_rate = 400000;

  ret = mml_stream_new(output_format_context, 
                       output_codec_context, 
                       output_codec, 
                       &output_video_stream);
	if (ret != MML_SUCCESS)
		goto RELEASE;
  
  output_video_stream->time_base = output_codec_context->time_base;

  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&output_format_context->pb, output_path, AVIO_FLAG_WRITE) < 0) 
    {
      ret = MML_ERROR_FILE_OPEN_FAILED;
      sprintf(err_msg, "failed to open output file '%s'", output_path);
      goto RELEASE;
    }
  }

  if (avformat_write_header(output_format_context, NULL) < 0) 
  {
    ret = MML_ERROR_FILE_NOT_WRITTEN;
    sprintf(err_msg, "failed to write header to '%s'", output_path);
    goto RELEASE;
  }

  // Initialize the scaling context
  sws_ctx = sws_getContext(input_codec_context->width, 
                           input_codec_context->height, 
                           input_codec_context->pix_fmt,
                           width, 
                           height, 
                           AV_PIX_FMT_YUV420P,
                           SWS_BILINEAR, 
                           NULL, 
                           NULL, 
                           NULL);

  if (!sws_ctx) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "failed to allocate conversion context");
    goto RELEASE;
  }

  frame = av_frame_alloc();
  scaled_frame = av_frame_alloc();
  if (!frame || !scaled_frame) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate frame");
    goto RELEASE;
  }

  scaled_frame->format = AV_PIX_FMT_YUV420P;
  scaled_frame->width = width;
  scaled_frame->height = height;

  if (av_frame_get_buffer(scaled_frame, 32) < 0) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate frame");
    goto RELEASE;
  }

  packet = av_packet_alloc();
  if (!packet) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate input packet");
    goto RELEASE;
  }

  while (av_read_frame(input_format_context, packet) >= 0) {
    if (packet->stream_index == video_stream_index) {
      if (avcodec_send_packet(input_codec_context, packet) == 0) {
        while (avcodec_receive_frame(input_codec_context, frame) == 0) {
          // Scale the frame
          sws_scale(sws_ctx,
                    (const uint8_t * const *)frame->data, 
                    frame->linesize,
                    0,
                    input_codec_context->height,
                    scaled_frame->data, 
                    scaled_frame->linesize);

          scaled_frame->pts = av_rescale_q(frame->pts, 
                                           input_codec_context->time_base, 
                                           output_codec_context->time_base);

          if (avcodec_send_frame(output_codec_context, scaled_frame) < 0) 
          {
            ret = MML_ERROR_FRAME_NOT_SENT;
            sprintf(err_msg, "failed to send frame to encoder");
            goto RELEASE;
          }

          AVPacket* out_packet = av_packet_alloc();
          if (!out_packet) 
          {
            ret = MML_ERROR_PACKET_NOT_CREATED;
            sprintf(err_msg, "failed to allocate output packet");
            goto RELEASE;
          }

          while (avcodec_receive_packet(output_codec_context, out_packet) == 0) 
          {
            av_packet_rescale_ts(out_packet, output_codec_context->time_base, output_video_stream->time_base);
            out_packet->stream_index = output_video_stream->index;
            if (av_interleaved_write_frame(output_format_context, out_packet) < 0) 
            {
              ret = MML_ERROR_FRAME_NOT_WRITTEN;
              sprintf(err_msg, "failed to write output frame");
              goto RELEASE;
            }
            av_packet_unref(out_packet);
          }

          av_packet_free(&out_packet);
        }
      }
      else 
      {
				// TODO: audio stream
      }
      av_packet_unref(packet);
    }
  }

  av_write_trailer(output_format_context);

  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) 
  {
    avio_closep(&output_format_context->pb);
  }

RELEASE:
  
  if (input_codec_context != NULL)
  	avcodec_free_context(&input_codec_context);
  if (output_codec_context != NULL)
  	avcodec_free_context(&output_codec_context);
  if (input_format_context != NULL)
  	avformat_close_input(&input_format_context);
  if (output_format_context != NULL)
  	avformat_free_context(output_format_context);
  if (sws_ctx != NULL)
  	sws_freeContext(sws_ctx);
  if (frame != NULL)
  	av_frame_free(&frame);
  if (scaled_frame != NULL)
  	av_frame_free(&scaled_frame);
  if (packet != NULL)
  	av_packet_free(&packet);

  return 0;
}

/*
********************************************************************************
**
** mml_video_pad
**
********************************************************************************
*/
int
mml_video_pad(const char* 	original_path, 
              const char* 	output_path, 
              int 					width, 
              int 					height)
{
	AVFormatContext* input_format_context = NULL;
  AVFormatContext* output_format_context = NULL;
  AVCodecContext* input_codec_context = NULL;
  AVCodecContext* output_codec_context = NULL;
  AVCodec* output_codec = NULL;
  AVStream* input_video_stream = NULL;
  AVStream* output_video_stream = NULL;
  AVPacket* packet = NULL;
  AVFrame* frame = NULL;
  AVFrame* padded_frame = NULL;
  struct SwsContext *sws_ctx = NULL;
  int video_stream_index = -1;
  int ret;

  ret = mml_stream_open(original_path, 
                       	AVMEDIA_TYPE_VIDEO, 
                       	&input_format_context, 
                       	&input_codec_context,
                       	&input_video_stream,
                       	&video_stream_index);
  
  if (ret != MML_SUCCESS)
		goto RELEASE;
  
  input_codec_context->time_base = input_video_stream->time_base;

  ret = mml_enc_init(output_path, 
                     AV_CODEC_ID_H264, 
                     &output_format_context, 
                     &output_codec_context,
                     &output_codec);

  output_codec_context->width = width;
  output_codec_context->height = height;
  output_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
  output_codec_context->time_base = input_codec_context->time_base;
  output_codec_context->bit_rate = 400000;

  ret = mml_stream_new(output_format_context, 
                       output_codec_context, 
                       output_codec, 
                       &output_video_stream);
	if (ret != MML_SUCCESS)
		goto RELEASE;
  
  output_video_stream->time_base = output_codec_context->time_base;

  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&output_format_context->pb, output_path, AVIO_FLAG_WRITE) < 0) 
    {
      ret = MML_ERROR_FILE_OPEN_FAILED;
      sprintf(err_msg, "failed to open output file '%s'", output_path);
      goto RELEASE;
    }
  }

  if (avformat_write_header(output_format_context, NULL) < 0) 
  {
    ret = MML_ERROR_FILE_NOT_WRITTEN;
    sprintf(err_msg, "failed to write header to '%s'", output_path);
    goto RELEASE;
  }
  
  // Target resolution with aspect ratio preserved
  int target_width = width;  
  int target_height = height;  
  int target_format = AV_PIX_FMT_YUV420P;

  // Calculate aspect ratio-preserved dimensions
  int input_width = input_codec_context->width;
  int input_height = input_codec_context->height;
  int scaled_width, scaled_height;

  float input_aspect = (float)input_width / input_height;
  float target_aspect = (float)target_width / target_height;

  if (input_aspect > target_aspect) 
  {
    scaled_width = target_width;
    scaled_height = (int)(target_width / input_aspect);
  } 
  else 
  {
    scaled_height = target_height;
    scaled_width = (int)(target_height * input_aspect);
  }

  // Calculate padding (black borders)
  int pad_left = (target_width - scaled_width) / 2;
  int pad_right = target_width - scaled_width - pad_left;
  int pad_top = (target_height - scaled_height) / 2;
  int pad_bottom = target_height - scaled_height - pad_top;

  // Initialize the scaling context
  sws_ctx = sws_getContext(input_codec_context->width, 
                           input_codec_context->height, 
                           input_codec_context->pix_fmt,
                           scaled_width, 
                           scaled_height, 
                           AV_PIX_FMT_YUV420P,
                           SWS_BILINEAR, 
                           NULL, 
                           NULL, 
                           NULL);

  if (!sws_ctx) 
  {
    ret = MML_ERROR_CODEC_NOT_CREATED;
    sprintf(err_msg, "failed to allocate conversion context");
    goto RELEASE;
  }

  frame = av_frame_alloc();
  padded_frame = av_frame_alloc();
  if (!frame || !padded_frame) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate frame");
    goto RELEASE;
  }

  padded_frame->format = AV_PIX_FMT_YUV420P;
  padded_frame->width = width;
  padded_frame->height = height;

  if (av_frame_get_buffer(padded_frame, 32) < 0) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate frame");
    goto RELEASE;
  }

  packet = av_packet_alloc();
  if (!packet) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate input packet");
    goto RELEASE;
  }

  while (av_read_frame(input_format_context, packet) >= 0) {
    if (packet->stream_index == video_stream_index) {
      if (avcodec_send_packet(input_codec_context, packet) == 0) {
        while (avcodec_receive_frame(input_codec_context, frame) == 0) {
          // Scale the frame
          AVFrame* scaled_frame = av_frame_alloc();
          av_image_alloc(scaled_frame->data, scaled_frame->linesize, scaled_width, scaled_height, target_format, 1);
          
          sws_scale(sws_ctx,
                    (const uint8_t * const *)frame->data, 
                    frame->linesize,
                    0,
                    input_codec_context->height,
                    scaled_frame->data, 
                    scaled_frame->linesize);

          // Copy the scaled frame into the padded frame at the centered position
          
          mml_frame_pad(padded_frame->data, padded_frame->linesize,
            					 (const uint8_t **)scaled_frame->data, scaled_frame->linesize,
                      	scaled_width, scaled_height, pad_left, pad_right, pad_top, pad_bottom,
                      	target_width, target_height, target_format);
          padded_frame->pts = frame->pts;
         	padded_frame->duration = frame->duration;
          
          scaled_frame->pts = av_rescale_q(frame->pts, 
                                           input_codec_context->time_base, 
                                           output_codec_context->time_base);

          if (avcodec_send_frame(output_codec_context, padded_frame) < 0) 
          {
            ret = MML_ERROR_FRAME_NOT_SENT;
            sprintf(err_msg, "failed to send frame to encoder");
            goto RELEASE;
          }

          AVPacket* out_packet = av_packet_alloc();
          if (!out_packet) 
          {
            ret = MML_ERROR_PACKET_NOT_CREATED;
            sprintf(err_msg, "failed to allocate output packet");
            goto RELEASE;
          }

          while (avcodec_receive_packet(output_codec_context, out_packet) == 0) 
          {
            av_packet_rescale_ts(out_packet, output_codec_context->time_base, output_video_stream->time_base);
            out_packet->stream_index = output_video_stream->index;
            if (av_interleaved_write_frame(output_format_context, out_packet) < 0) 
            {
              ret = MML_ERROR_FRAME_NOT_WRITTEN;
              sprintf(err_msg, "failed to write output frame");
              goto RELEASE;
            }
            av_packet_unref(out_packet);
          }
          av_frame_free(&scaled_frame);
          av_packet_free(&out_packet);
        }
      }
      av_packet_unref(packet);
    }
  }

  av_write_trailer(output_format_context);

  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) 
  {
    avio_closep(&output_format_context->pb);
  }

RELEASE:
  
  if (input_codec_context != NULL)
  	avcodec_free_context(&input_codec_context);
  if (output_codec_context != NULL)
  	avcodec_free_context(&output_codec_context);
  if (input_format_context != NULL)
  	avformat_close_input(&input_format_context);
  if (output_format_context != NULL)
  	avformat_free_context(output_format_context);
  if (sws_ctx != NULL)
  	sws_freeContext(sws_ctx);
  if (frame != NULL)
  	av_frame_free(&frame);
  if (padded_frame != NULL)
  	av_frame_free(&padded_frame);
  if (packet != NULL)
  	av_packet_free(&packet);

  return 0;
}

/*
********************************************************************************
**
** mml_video_concat
**
********************************************************************************
*/
int
mml_video_concat(const char* original_path1, 
                 const char* original_path2, 
                 const char* output_path)
{
  AVFormatContext* 		input_fmt_ctx1 				= NULL;
  AVFormatContext* 		input_fmt_ctx2 				= NULL;
  AVFormatContext* 		output_fmt_ctx 				= NULL;
  AVCodecContext* 		dec_ctx1 							= NULL;
  AVCodecContext* 		dec_ctx2 							= NULL;
  AVCodecContext* 		enc_ctx 							= NULL;
  AVCodec*				 		enc			 							= NULL;
  AVPacket* 					packet                 = NULL;
  AVFrame* 						frame 								= NULL;
  AVStream*						input_video_stream1		= NULL;
  AVStream*						input_video_stream2		= NULL;
  AVStream*						output_video_stream		= NULL;
  int									input_video_index1;
  int									input_video_index2;
  int									got_frame = 0;
  int ret;
  
  ret = mml_format_open(original_path1, 
                        &input_fmt_ctx1);
  if (ret != MML_SUCCESS)
    goto RELEASE;
 
  ret = mml_format_open(original_path2, 
                        &input_fmt_ctx2);
  
  if (ret != MML_SUCCESS)
    goto RELEASE;
	
  /*
  ret = mml_enc_init(output_path, 
                     AV_CODEC_ID_H264, 
                     &output_fmt_ctx, 
                     &enc_ctx, 
                     &enc);
                     */
  ret = avformat_alloc_output_context2(&output_fmt_ctx, NULL, "mp4", output_path);
  
  if (ret != MML_SUCCESS)
    goto RELEASE;
  
  for (int i = 0; i < 2; i++)
  {
    AVFormatContext* input_fmt_ctx = (i == 0) ? input_fmt_ctx1 : input_fmt_ctx2;
		for (int j = 0; j < input_fmt_ctx->nb_streams; j++)
    {
			AVStream* out_stream;
      AVStream* in_stream = input_fmt_ctx->streams[j];
      AVCodecParameters *in_codecpar = in_stream->codecpar;

      out_stream = avformat_new_stream(output_fmt_ctx, NULL);
      if (!out_stream) 
      {
        ret = MML_ERROR_STREAM_NOT_CREATED;
        sprintf(err_msg, "failed to create stream");
        goto RELEASE;
      }

    	ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
      out_stream->codecpar->frame_size = 5;
      if (ret < 0) 
      {
        ret = MML_ERROR_CODEC_NOT_COPIED;
        sprintf(err_msg, "failed to copy codec parameters");
        goto RELEASE;
      }
      out_stream->codecpar->codec_tag = 0;
      /*!
      ** time base change is working.
      **
      ** sample:
      **		ffprobe -v error -show_entries stream=index,codec_type,time_base ../../data/V1V2.mp4
      */
      /*
      if (out_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
      {
      	out_stream->time_base = (AVRational){1, 44100};
      }
      */
    } // input_fmt_ctx->nb_streams
  }
  
  if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&output_fmt_ctx->pb, output_path, AVIO_FLAG_WRITE) < 0) 
    {
      ret = MML_ERROR_FILE_OPEN_FAILED;
      sprintf(err_msg, "failed to open file '%s'", output_path);
      goto RELEASE;
    }
  }
  
  ret = avformat_write_header(output_fmt_ctx, NULL);
  if (ret < 0) 
  {
    ret = MML_ERROR_STREAM_WRITE_FAILED;
    sprintf(err_msg, "failed to write header to '%s'", output_path);
    goto RELEASE;
  }
  
  int64_t prev_audio_pts = 0;
  int64_t prev_audio_dts = 0;
  int64_t prev_audio_dur = 0;
  
  int64_t offset_audio_pts = 0;
  int64_t offset_audio_dts = 0;
  
  int64_t prev_video_pts = 0;
  int64_t prev_video_dts = 0;
  int64_t prev_video_dur = 0;
  
  int64_t offset_video_pts = 0;
  int64_t offset_video_dts = 0;
  
  packet = av_packet_alloc();
  if (!packet) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate input packet");
    goto RELEASE;
  }
  
  for (int i = 0; i < 2; i++) 
  {
    AVFormatContext* input_fmt_ctx = (i == 0) ? input_fmt_ctx1 : input_fmt_ctx2;
    while (av_read_frame(input_fmt_ctx, packet) >= 0) 
    {
      AVStream* in_stream = input_fmt_ctx->streams[packet->stream_index];
      AVStream* out_stream = output_fmt_ctx->streams[packet->stream_index];
      if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) 
      {
        mml_stream_remux(packet, 
                         in_stream->time_base, 
                         out_stream->time_base,
                         output_fmt_ctx,
                         &prev_audio_dts, 
                         &prev_audio_pts, 
                         &prev_audio_dur, 
                         &offset_audio_dts, 
                         &offset_audio_pts);
      }
      else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
      {
        if (packet->pts > packet->dts)
        {
          packet->pts = packet->dts;
        }
        mml_stream_remux(packet, 
                         in_stream->time_base, 
                         out_stream->time_base,
                         output_fmt_ctx,
                         &prev_video_dts, 
                         &prev_video_pts, 
                         &prev_video_dur, 
                         &offset_video_dts, 
                         &offset_video_pts);
      }
      av_packet_unref(packet);
    }
  }
  av_write_trailer(output_fmt_ctx);
  
  if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
    avio_closep(&output_fmt_ctx->pb);
  
RELEASE:
  
  if (dec_ctx1 != NULL)
  	avcodec_free_context(&dec_ctx1);
  if (dec_ctx2 != NULL)
  	avcodec_free_context(&dec_ctx2);
  if (enc_ctx != NULL)
  	avcodec_free_context(&enc_ctx);
  if (input_fmt_ctx1 != NULL)
  	avformat_close_input(&input_fmt_ctx1);
  if (input_fmt_ctx2 != NULL)
  	avformat_close_input(&input_fmt_ctx2);
  if (output_fmt_ctx != NULL)
  	avformat_free_context(output_fmt_ctx);
  if (frame != NULL)
  	av_frame_free(&frame);
  if (packet != NULL)
  	av_packet_free(&packet);
  
	return ret;
}

/*
********************************************************************************
**
** mml_video_cut
**
********************************************************************************
*/
int
mml_video_cut(const char* original_path, 
              double start_time,
              double end_time,
              const char* output_path)
{
  int                 ret                   = MML_SUCCESS;
  AVFormatContext* 		input_fmt_ctx 				= NULL;
  AVFormatContext* 		output_fmt_ctx 				= NULL;
  AVCodecContext* 		dec_ctx 							= NULL;
  AVCodecContext* 		enc_ctx 							= NULL;
  AVCodec*				 		enc			 							= NULL;
  AVPacket* 					packet                = NULL;
  AVFrame* 						frame 								= NULL;
  AVStream*						input_video_stream		= NULL;
  AVStream*						output_video_stream		= NULL;
  int									got_frame = 0;
  
  ret = mml_format_open(original_path, 
                        &input_fmt_ctx);
  if (ret != MML_SUCCESS)
    goto RELEASE;

  ret = avformat_alloc_output_context2(&output_fmt_ctx, NULL, "mp4", output_path);
  
  if (ret != MML_SUCCESS)
    goto RELEASE;
  
  for (int j = 0; j < input_fmt_ctx->nb_streams; j++)
  {
    AVStream* out_stream;
    AVStream* in_stream = input_fmt_ctx->streams[j];
    AVCodecParameters *in_codecpar = in_stream->codecpar;

    out_stream = avformat_new_stream(output_fmt_ctx, NULL);
    if (!out_stream) 
    {
      ret = MML_ERROR_STREAM_NOT_CREATED;
      sprintf(err_msg, "failed to create stream");
      goto RELEASE;
    }

    ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
    out_stream->codecpar->frame_size = 5;
    if (ret < 0) 
    {
      ret = MML_ERROR_CODEC_NOT_COPIED;
      sprintf(err_msg, "failed to copy codec parameters");
      goto RELEASE;
    }
    out_stream->codecpar->codec_tag = 0;
  }
  
  if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&output_fmt_ctx->pb, output_path, AVIO_FLAG_WRITE) < 0) 
    {
      ret = MML_ERROR_FILE_OPEN_FAILED;
      sprintf(err_msg, "failed to open file '%s'", output_path);
      goto RELEASE;
    }
  }
  
  ret = avformat_write_header(output_fmt_ctx, NULL);
  if (ret < 0) 
  {
    ret = MML_ERROR_STREAM_WRITE_FAILED;
    sprintf(err_msg, "failed to write header to '%s'", output_path);
    goto RELEASE;
  }
  
  int64_t prev_audio_pts = 0;
  int64_t prev_audio_dts = 0;
  int64_t prev_audio_dur = 0;
  
  int64_t offset_audio_pts = 0;
  int64_t offset_audio_dts = 0;
  
  int64_t prev_video_pts = 0;
  int64_t prev_video_dts = 0;
  int64_t prev_video_dur = 0;
  
  int64_t offset_video_pts = 0;
  int64_t offset_video_dts = 0;
  
  
  packet = av_packet_alloc();
  if (!packet) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate input packet");
    goto RELEASE;
  }
  
  int start = 0;
  int stop = 0;
  int keyframe = 0;
  int64_t start_audio_pts = -1;
  int64_t start_video_pts = -1;
  while (av_read_frame(input_fmt_ctx, packet) >= 0) 
  {
    AVStream* in_stream = input_fmt_ctx->streams[packet->stream_index];
    AVStream* out_stream = output_fmt_ctx->streams[packet->stream_index];
    double duration_seconds = packet->pts * av_q2d(in_stream->time_base);

    /*!
    ** 音频，可以粗略地用时间判断起始。
    */
    if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && duration_seconds < start_time) 
    {
      // 音频未开始
			av_packet_unref(packet);
      continue;
    }
    
    /*!
    ** 关键帧标识位
    */
    if (packet->flags & AV_PKT_FLAG_KEY) 
      keyframe = 1;
    else 
      keyframe = 0;
    /*!
    ** 视频，只允许关键帧作为起始帧。
    */
    if ((start_time - duration_seconds) < 0.05 && keyframe)
    {
      start = 1;
    }
    if (!start && in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
			av_packet_unref(packet);
      continue;
    }
    /*!
    ** 视频，必须用关键帧结束。
    */
    if (duration_seconds - end_time >= 0.05 && keyframe)
      stop = 1;
    av_packet_rescale_ts(packet, in_stream->time_base, out_stream->time_base);
    packet->pos = -1;
    if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) 
    {
      if (start_audio_pts == -1) 
        start_audio_pts = packet->pts;
      packet->pts -= start_audio_pts;
      packet->dts -= start_audio_pts;
      mml_stream_remux(packet,
                       in_stream->time_base, 
                       out_stream->time_base,
                       output_fmt_ctx,
                       &prev_audio_dts, 
                       &prev_audio_pts, 
                       &prev_audio_dur, 
                       &offset_audio_dts, 
                       &offset_audio_pts);
    }
    else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      if (start_video_pts == -1) 
        start_video_pts = packet->pts;
      packet->pts -= start_video_pts;
      packet->dts -= start_video_pts;
      av_interleaved_write_frame(output_fmt_ctx, packet);
    }
    av_packet_unref(packet);
    /*!
    ** 结束处理
    */
    if (stop)
      break;
  }
  av_write_trailer(output_fmt_ctx);
  
  if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
    avio_closep(&output_fmt_ctx->pb);
  
RELEASE:
  
  if (dec_ctx != NULL)
  	avcodec_free_context(&dec_ctx);
  if (enc_ctx != NULL)
  	avcodec_free_context(&enc_ctx);
  if (input_fmt_ctx != NULL)
  	avformat_close_input(&input_fmt_ctx);
  if (output_fmt_ctx != NULL)
  	avformat_free_context(output_fmt_ctx);
  if (frame != NULL)
  	av_frame_free(&frame);
  if (packet != NULL)
  	av_packet_free(&packet);
  
	return ret;
}

/*!
**
*/
int
mml_video_save_images(const char* original_path, 
                      double start_time, 
                      double end_time, 
                      const char* output_path,
                      int image_index)
{
  int                 ret                   = MML_SUCCESS;
  AVFormatContext* 		input_fmt_ctx 				= NULL;
  AVCodecContext* 		dec_ctx 							= NULL;
  AVCodecContext* 		enc_ctx 							= NULL;
  AVCodec*				 		enc			 							= NULL;
  AVPacket* 					packet                = NULL;
  AVFrame* 						frame 								= NULL;
  AVFrame* 						rgb_frame 						= NULL;
  AVStream*						input_video_stream		= NULL;
  AVStream*						output_video_stream		= NULL;
  int									got_frame             = 0;
  
  ret = mml_format_open(original_path, &input_fmt_ctx);
  if (ret != MML_SUCCESS)
    goto RELEASE;
  
  int video_stream_index = -1;
  for (unsigned int i = 0; i < input_fmt_ctx->nb_streams; i++) {
    if (input_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_index = i;
      break;
    }
  }
  
  AVCodecParameters *codecpar = input_fmt_ctx->streams[video_stream_index]->codecpar;
  const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
  
  dec_ctx = avcodec_alloc_context3(decoder);
  avcodec_parameters_to_context(dec_ctx, input_fmt_ctx->streams[video_stream_index]->codecpar);
  avcodec_open2(dec_ctx, decoder, NULL);
  
  int64_t prev_audio_pts = 0;
  int64_t prev_audio_dts = 0;
  int64_t prev_audio_dur = 0;
  
  int64_t offset_audio_pts = 0;
  int64_t offset_audio_dts = 0;
  
  int64_t prev_video_pts = 0;
  int64_t prev_video_dts = 0;
  int64_t prev_video_dur = 0;
  
  int64_t offset_video_pts = 0;
  int64_t offset_video_dts = 0;
  
  
  packet = av_packet_alloc();
  frame = av_frame_alloc();
  rgb_frame = av_frame_alloc();
  
  if (!packet) 
  {
    ret = MML_ERROR_FRAME_NOT_CREATED;
    sprintf(err_msg, "failed to allocate input packet");
    goto RELEASE;
  }

  mml_encoder_p encoder;
  mml_encoder_init(&encoder, AV_CODEC_ID_PNG);

  int start = 0;
  int stop = 0;
  int keyframe = 0;
  int64_t start_audio_pts = -1;
  int64_t start_video_pts = -1;
  while (av_read_frame(input_fmt_ctx, packet) >= 0) 
  {
    AVStream* in_stream = input_fmt_ctx->streams[packet->stream_index];
    double duration_seconds = packet->pts * av_q2d(in_stream->time_base);
    
    /*!
    ** 音频，可以粗略地用时间判断起始。
    */
    if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && duration_seconds < start_time) 
    {
      // 音频未开始
      av_packet_unref(packet);
      continue;
    }
    
    /*!
    ** 关键帧标识位
    */
    if (packet->flags & AV_PKT_FLAG_KEY) 
      keyframe = 1;
    else 
      keyframe = 0;
    /*!
    ** 视频，只允许关键帧作为起始帧。
    */
    if ((start_time - duration_seconds) < 0.05 && keyframe)
    {
      start = 1;
    }
    if (!start && in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      av_packet_unref(packet);
      continue;
    }
    /*!
    ** 视频，必须用关键帧结束。
    */
    if (duration_seconds - end_time >= 0.05 && keyframe)
      stop = 1;
    packet->pos = -1;
    if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      if (start_video_pts == -1) 
        start_video_pts = packet->pts;
      packet->pts -= start_video_pts;
      packet->dts -= start_video_pts;
      // 在包中只导出一帧
      ret = avcodec_send_packet(dec_ctx, packet);
      if (ret < 0) break;
      if (avcodec_receive_frame(dec_ctx, frame) >= 0) 
      {
        char filepath[4096];
        sprintf(filepath, "%s/%08d.png", output_path, image_index);
        mml_frame_save_image(encoder, frame, rgb_frame, filepath);
        image_index++;
        av_frame_unref(frame);
      }
    }
    av_packet_unref(packet);
    /*!
    ** 结束处理
    */
    if (stop)
      break;
  }
  
RELEASE:
  
  if (dec_ctx != NULL)
    avcodec_free_context(&dec_ctx);
  if (enc_ctx != NULL)
    avcodec_free_context(&enc_ctx);
  if (input_fmt_ctx != NULL)
    avformat_close_input(&input_fmt_ctx);
  if (frame != NULL)
    av_frame_free(&frame);
  if (rgb_frame != NULL)
    av_frame_free(&rgb_frame);
  if (packet != NULL)
    av_packet_free(&packet);
  
  if (encoder != NULL)
    mml_encoder_free(encoder);
  return ret;
}