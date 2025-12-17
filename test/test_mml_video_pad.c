/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
// #include <stdio.h>
// #include "libmml.h"

// int main(int argc, char* argv[])
// {
//   const char* video_path = "../../data/V1.mp4";
//   const char* output_path = "V1P_1920x1080.mp4";
//   int rc = mml_video_pad(video_path, output_path, 1920, 1080);
//   if (rc != MML_SUCCESS)
//     printf("error: %s\n", mml_error());
// 	return 0;
// }

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-frame.h"
#include "libmml-video.h"

#define OUT_FILE "resize_aspect.mp4"
#define TARGET_W 1920
#define TARGET_H 1080

// -----------------------------------------------------------------------------
// Helper: Fill YUV420P Frame with Black
// -----------------------------------------------------------------------------
void fill_frame_black(AVFrame* frame) {
  // Y Plane (Luma) -> 0 (Black)
  for (int y = 0; y < frame->height; y++) {
    memset(frame->data[0] + y * frame->linesize[0], 0, frame->width);
  }

  // U and V Planes (Chroma) -> 128 (Neutral Gray)
  // Note: In YUV, 0 is green. 128 is "no color". 
  // Combined with Y=0, this produces visual black.
  for (int i = 1; i < 3; i++) {
    int h_chroma = frame->height / 2;
    int w_chroma = frame->width / 2;
    for (int y = 0; y < h_chroma; y++) {
      memset(frame->data[i] + y * frame->linesize[i], 128, w_chroma);
    }
  }
}

// -----------------------------------------------------------------------------
// Helper: Calculate Aspect Ratio Corrected Dimensions
// -----------------------------------------------------------------------------
void get_aspect_correct_dims(int src_w, int src_h, int dst_w, int dst_h, 
                             int* out_w, int* out_h, int* out_x, int* out_y) 
{
  double ratio_src = (double)src_w / src_h;
  double ratio_dst = (double)dst_w / dst_h;

  int new_w, new_h;

  if (ratio_src > ratio_dst) {
    // Source is wider (Fit to Width, Letterbox Top/Bottom)
    new_w = dst_w;
    new_h = (int)(dst_w / ratio_src);
  } else {
    // Source is taller (Fit to Height, Pillarbox Left/Right)
    new_h = dst_h;
    new_w = (int)(dst_h * ratio_src);
  }

  // Align dimensions to even numbers (Required for YUV420P)
  new_w &= ~1;
  new_h &= ~1;

  *out_w = new_w;
  *out_h = new_h;
  
  // Calculate centering offsets
  *out_x = (dst_w - new_w) / 2;
  *out_y = (dst_h - new_h) / 2;
  
  // Align offsets to even numbers
  *out_x &= ~1;
  *out_y &= ~1;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) 
{
  AVFormatContext* in_fmt = NULL;
  int vid_idx = -1;
  AVCodecContext* dec_ctx = NULL;

  mml_video_load("../../data/V1.mp4", &in_fmt, &dec_ctx, &vid_idx, NULL, NULL, NULL, NULL);

  AVFormatContext* out_fmt = NULL;
  avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
  
  // Fixed Output Resolution
  enc_ctx->width = TARGET_W;
  enc_ctx->height = TARGET_H;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = (AVRational){1, 30};
  enc_ctx->framerate = (AVRational){30, 1};
  
  if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) 
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  avcodec_open2(enc_ctx, enc, NULL);

  AVStream* out_st = avformat_new_stream(out_fmt, NULL);
  avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
  out_st->time_base = enc_ctx->time_base;
  avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
  avformat_write_header(out_fmt, NULL);

  // 3. Resources
  AVPacket* pkt = av_packet_alloc();
  AVFrame* src_frame = av_frame_alloc();
  
  // Destination Canvas
  AVFrame* dst_frame = NULL;

  struct SwsContext* sws = NULL;
  int64_t pts = 0;
  
  // Calc dimensions once (assuming input video doesn't change resolution mid-stream)
  int new_w, new_h, off_x, off_y;
  get_aspect_correct_dims(dec_ctx->width, dec_ctx->height, TARGET_W, TARGET_H,
                          &new_w, &new_h, &off_x, &off_y);

  printf("Input: %dx%d -> Scaled: %dx%d (Offset: %d,%d) -> Output: %dx%d\n",
         dec_ctx->width, dec_ctx->height, new_w, new_h, off_x, off_y, TARGET_W, TARGET_H);

  // Initialize Scaler to scale Input -> New Dimensions
  sws = sws_getContext(
      dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
      new_w, new_h, AV_PIX_FMT_YUV420P,
      SWS_BICUBIC, NULL, NULL, NULL);

  // 4. Loop
  while (av_read_frame(in_fmt, pkt) >= 0) 
  {
    if (pkt->stream_index == vid_idx) {
      if (avcodec_send_packet(dec_ctx, pkt) == 0)
      {
        while (avcodec_receive_frame(dec_ctx, src_frame) == 0) 
        {
          mml_frame_aspect(src_frame, &dst_frame, &sws, 
                           TARGET_W, TARGET_H,
                           new_w, new_h,
                           off_x, off_y);
          dst_frame->pts = pts++;
          mml_frame_write(enc_ctx, out_fmt, out_st, dst_frame);
          if (pts % 30 == 0) printf("Processed: %ld\r", pts);
        }
      }
    }
    av_packet_unref(pkt);
  }

  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);

  if (sws) sws_freeContext(sws);
  av_frame_free(&src_frame);
  av_frame_free(&dst_frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&in_fmt);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone.\n");
  return 0;
}