/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>

#include "libmml-video.h"
#include "libmml-frame.h"
#include "libmml-util.h"
#include "libmml-error.h"

int
mml_video_load(const char*          filepath,
               AVFormatContext**    ifmt_ctx,
               AVCodecContext**     dec_ctx,
               int*                 video_idx,
               const char*          outpath,
               AVFormatContext**    ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream)
{
  const AVCodec* decoder = NULL;
  const AVCodec* encoder = NULL;
  AVStream* in_stream = NULL;

  if (avformat_open_input(ifmt_ctx, filepath, NULL, NULL) < 0) 
  {
    mml_error_set(1, "could not open input file '%s'", filepath);
    return 1;
  }

  if (avformat_find_stream_info(*ifmt_ctx, NULL) < 0) 
  {
    mml_error_set(1, "could not find stream info");
    return 1;
  }

  *video_idx = av_find_best_stream(*ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
  if (*video_idx < 0) 
  {
    mml_error_set(1, "no video stream found in '%s'", filepath);
    return 1;
  }
  in_stream = (*ifmt_ctx)->streams[*video_idx];

  *dec_ctx = avcodec_alloc_context3(decoder);
  avcodec_parameters_to_context(*dec_ctx, in_stream->codecpar);
  
  if (avcodec_open2(*dec_ctx, decoder, NULL) < 0) 
  {
    mml_error_set(1, "failed to open decoder");
    return 1;
  }

  if (outpath == NULL)
    return MML_SUCCESS;

  avformat_alloc_output_context2(ofmt_ctx, NULL, NULL, outpath);
  if (!*ofmt_ctx) 
  {
    mml_error_set(1, "Failed to allocate output context");
    return 1;
  }

  encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!encoder) 
  {
    mml_error_set(1, "H.264 encoder not found");
    return 1;
  }

  *out_stream = avformat_new_stream(*ofmt_ctx, NULL);
  *enc_ctx = avcodec_alloc_context3(encoder);

  (*enc_ctx)->height = (*dec_ctx)->height;
  (*enc_ctx)->width = (*dec_ctx)->width;
  (*enc_ctx)->sample_aspect_ratio = (*dec_ctx)->sample_aspect_ratio;
  (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;

  AVRational framerate = in_stream->avg_frame_rate;
  (*enc_ctx)->framerate = framerate;
  (*enc_ctx)->time_base = av_inv_q(framerate); 

  (*out_stream)->time_base = (*enc_ctx)->time_base;

  // Optimization
  if ((*ofmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
    (*enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  
  av_opt_set((*enc_ctx)->priv_data, "preset", "fast", 0);

  if (avcodec_open2(*enc_ctx, encoder, NULL) < 0) {
    mml_error_set(1, "cannot open encoder");
    return 1;
  }
  
  avcodec_parameters_from_context((*out_stream)->codecpar, *enc_ctx);

  if (!((*ofmt_ctx)->oformat->flags & AVFMT_NOFILE)) 
  {
    if (avio_open(&((*ofmt_ctx)->pb), outpath, AVIO_FLAG_WRITE) < 0) 
    {
      mml_error_set(1, "could not open output file '%s'\n", outpath);
      return 1;
    }
  }

  // 如果后续增加音频，会报错
  // if (avformat_write_header(*ofmt_ctx, NULL) < 0) return 1;
  return MML_SUCCESS;
}

int 
mml_video_concat(const char* filename, 
                 AVFormatContext* out_fmt_ctx, 
                 AVCodecContext* out_enc_ctx, 
                 AVStream* out_stream, 
                 int64_t* out_pts) 
{
  AVFormatContext* in_fmt_ctx = NULL;
  AVCodecContext* dec_ctx = NULL;
  const AVCodec* dec = NULL;
  int vid_idx = -1;

  mml_video_load(filename, &in_fmt_ctx, &dec_ctx, &vid_idx, NULL, NULL, NULL, NULL);

  AVPacket* pkt = av_packet_alloc();
  AVFrame* raw_frame = av_frame_alloc();
  AVFrame* scaled_frame = av_frame_alloc();
  struct SwsContext* sws_ctx = NULL;

  // Configure Destination Frame
  scaled_frame->format = AV_PIX_FMT_YUV420P;
  scaled_frame->width  = out_enc_ctx->width;
  scaled_frame->height = out_enc_ctx->height;
  av_frame_get_buffer(scaled_frame, 32);

  while (av_read_frame(in_fmt_ctx, pkt) >= 0) 
  {
    if (pkt->stream_index == vid_idx) 
    {
      if (avcodec_send_packet(dec_ctx, pkt) == 0) 
      {
        while (avcodec_receive_frame(dec_ctx, raw_frame) == 0) 
        {
          if (!sws_ctx) 
          {
            sws_ctx = sws_getContext(raw_frame->width, raw_frame->height, raw_frame->format,
                                     out_enc_ctx->width, out_enc_ctx->height, AV_PIX_FMT_YUV420P,
                                     SWS_BICUBIC, NULL, NULL, NULL);
          }

          sws_scale(sws_ctx, (const uint8_t* const*)raw_frame->data, raw_frame->linesize,
                    0, raw_frame->height,
                    scaled_frame->data, scaled_frame->linesize);

          av_frame_make_writable(scaled_frame);

          scaled_frame->pts = (*out_pts)++;
          mml_frame_write(out_enc_ctx, out_fmt_ctx, out_stream, scaled_frame);
        }
      }
    }
    av_packet_unref(pkt);
  }

  avcodec_send_packet(dec_ctx, NULL);
  while (avcodec_receive_frame(dec_ctx, raw_frame) == 0) 
  {
    if (sws_ctx) 
    {
      sws_scale(sws_ctx, (const uint8_t* const*) raw_frame->data, raw_frame->linesize,
                0, raw_frame->height, scaled_frame->data, scaled_frame->linesize);
                scaled_frame->pts = (*out_pts)++;
      mml_frame_write(out_enc_ctx, out_fmt_ctx, out_stream, scaled_frame);
    }
  }

  sws_freeContext(sws_ctx);
  av_frame_free(&raw_frame);
  av_frame_free(&scaled_frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avformat_close_input(&in_fmt_ctx);

  return MML_SUCCESS;
}


int 
mml_video_slow(AVFormatContext* ifmt_ctx, 
               AVCodecContext* dec_ctx, 
               int vid_idx,
               AVFormatContext* ofmt_ctx, 
               AVCodecContext* enc_ctx, 
               AVStream* out_stream,
               int slow_factor,
               int64_t* next_pts)
{
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
 
  while (av_read_frame(ifmt_ctx, pkt) >= 0)
  {
    if (pkt->stream_index != vid_idx) 
      continue;
    if (avcodec_send_packet(dec_ctx, pkt) == 0) 
    {
      while (avcodec_receive_frame(dec_ctx, frame) == 0) 
      {
        av_frame_make_writable(frame);
        for (int i = 0; i < slow_factor; i++) 
        {
          frame->pts = (*next_pts)++;
          mml_frame_write(enc_ctx, ofmt_ctx, out_stream, frame);
        }
      }
    }
    av_packet_unref(pkt);
  } 
 
  // 这告诉解码器：“我已经没有新的数据给你了，请把你肚子里剩下的所有帧都吐出来。”这是进入 
  // Draining Mode（排水/冲刷模式） 的标准操作。
  avcodec_send_packet(dec_ctx, NULL);
  while (avcodec_receive_frame(dec_ctx, frame) == 0) 
  {
    av_frame_make_writable(frame);
    for (int i = 0; i < slow_factor; i++) 
    {
      frame->pts = (*next_pts)++;
      mml_frame_write(enc_ctx, ofmt_ctx, out_stream, frame);
    }
  }
  av_packet_free(&pkt);
  av_frame_free(&frame);
  return MML_SUCCESS;
}

int 
mml_video_reverse(AVFormatContext* ifmt_ctx, 
                  AVCodecContext* dec_ctx, 
                  int vid_idx,
                  AVFormatContext* ofmt_ctx, 
                  AVCodecContext* enc_ctx, 
                  AVStream* out_stream,
                  int slow_factor,
                  int64_t* next_pts)
{
  AVPacket* pkt = av_packet_alloc();
  AVFrame* temp_frame = av_frame_alloc();
  struct SwsContext* sws = NULL;

  // Dynamic Array to store ALL frames
  AVFrame** frame_list = NULL;
  int frame_count = 0;
  int list_capacity = 0;

  while (av_read_frame(ifmt_ctx, pkt) >= 0) 
  {
    if (pkt->stream_index == vid_idx) 
    {
      if (avcodec_send_packet(dec_ctx, pkt) == 0) 
      {
        while (avcodec_receive_frame(dec_ctx, temp_frame) == 0) 
        {
          AVFrame* stored_frame = mml_frame_deep(temp_frame, &sws);
          if (frame_count >= list_capacity) {
            list_capacity = (list_capacity == 0) ? 128 : list_capacity * 2;
            frame_list = realloc(frame_list, sizeof(AVFrame*) * list_capacity);
          }

          // C. Store
          frame_list[frame_count++] = stored_frame;
        }
      }
    }
    av_packet_unref(pkt);
  }

  // 4. Reverse Processing Loop
  int64_t pts = 0;

  // Iterate Backwards
  for (int i = frame_count - 1; i >= 0; i--) {
    AVFrame* frame = frame_list[i];

    // Ensure frame metadata is clean for the encoder
    frame->pkt_dts = AV_NOPTS_VALUE;
    frame->pict_type = AV_PICTURE_TYPE_NONE;

    // Encode multiple times for Slow Motion
    for (int k = 0; k < slow_factor; k++) {
      
      av_frame_make_writable(frame);
      frame->pts = (*next_pts)++;
      mml_frame_write(enc_ctx, ofmt_ctx, out_stream, frame);
    }
  }

  for (int i = 0; i < frame_count; i++) {
    av_frame_free(&frame_list[i]);
  }
  free(frame_list);

  sws_freeContext(sws);
  av_frame_free(&temp_frame);
  av_packet_free(&pkt);

  return MML_SUCCESS;
}

int 
mml_video_clip(AVFormatContext* ifmt_ctx, 
               AVCodecContext* dec_ctx, 
               int vid_idx,
               AVFormatContext* ofmt_ctx, 
               AVCodecContext* enc_ctx, 
               AVStream* out_stream,
               const char* start_time,
               const char* end_time,
               int64_t* next_pts)
{
  double start_sec = mml_time_parse(start_time);
  double end_sec   = mml_time_parse(end_time);

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  int64_t out_pts = 0;
  int frame_count = 0;

  while (av_read_frame(ifmt_ctx, pkt) >= 0) 
  {
    if (pkt->stream_index == vid_idx) 
    {
      if (avcodec_send_packet(dec_ctx, pkt) == 0) 
      {
        while (avcodec_receive_frame(dec_ctx, frame) == 0) 
        {
        
          double current_time = frame->pts * av_q2d(ifmt_ctx->streams[vid_idx]->time_base);

          if (current_time < start_sec) {
            continue; 
          }

          if (current_time > end_sec) {
            goto end_loop; // Break out of nested loops
          }

          frame->pts = (*next_pts)++;
          mml_frame_write(enc_ctx, ofmt_ctx, out_stream, frame);
        }
      }
    }
    av_packet_unref(pkt);
  }

end_loop:  
  av_packet_free(&pkt);
  av_frame_free(&frame);
  return MML_SUCCESS;
}