/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/

/******************************************************************************
 *
 * 把原始视频转换成慢动作视频。
 * 
 * 文件:        test_mml_video_slow.c
 * 作者:        Christian Gann (guo.guo.gan@gmail.com)
 * 创建日期:    2025-11-30
 * 最后修改:    2025-11-30
 * 
 * 说明: 
 *    采用帧赋值的算法来实现慢动作的播放，缺点在于会产生更多的帧，视频占用空间大。
 * 
 *****************************************************************************/
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>

#include "libmml-video.h"
#include "libmml-frame.h"
#include "libmml-error.h"

// Configuration
#define SLOW_FACTOR 5 
#define OUT_FILENAME "output_slow.mp4"
 
int main(int argc, char* argv[]) {
  const char* in_filename = "../../data/V3.10.mp4";

  AVFormatContext* ifmt_ctx = NULL;
  AVFormatContext* ofmt_ctx = NULL;
  AVCodecContext* dec_ctx = NULL;
  AVCodecContext* enc_ctx = NULL;
  
  AVStream* out_stream = NULL;
  
  int video_idx = -1;
  double fps = 0.0;
  int ret = 0;

  int rc = mml_video_load(in_filename, 
                          &ifmt_ctx,
                          &dec_ctx,
                          &video_idx,
                          OUT_FILENAME,
                          &ofmt_ctx,
                          &enc_ctx,
                          &out_stream);
  fps = av_q2d(enc_ctx->framerate);    
 
  // --- 3. PROCESSING LOOP ---
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  int64_t next_pts = 0; 
 
  printf("Processing... Slow Factor: %dx\n", SLOW_FACTOR);
 
  while (av_read_frame(ifmt_ctx, pkt) >= 0)
  {
    if (pkt->stream_index != video_idx) 
      continue;
    if (avcodec_send_packet(dec_ctx, pkt) == 0) 
    {
      while (avcodec_receive_frame(dec_ctx, frame) == 0) 
      {
        av_frame_make_writable(frame);
        for (int i = 0; i < SLOW_FACTOR; i++) 
        {
          frame->pts = next_pts++;       
          if (mml_frame_write(enc_ctx, ofmt_ctx, out_stream, frame) < 0) {
            fprintf(stderr, "error encoding: %s\n", mml_error_msg());
            goto end;
          }
        }
      }
    }
    av_packet_unref(pkt);
  } // while (av_read_frame(ifmt_ctx, pkt) >= 0)
 
  avcodec_send_packet(dec_ctx, NULL);
  while (avcodec_receive_frame(dec_ctx, frame) == 0) 
  {
    av_frame_make_writable(frame);
    for (int i = 0; i < SLOW_FACTOR; i++) 
    {
      frame->pts = next_pts++;
      mml_frame_write(enc_ctx, ofmt_ctx, out_stream, frame);
    }
  }

  mml_frame_write(enc_ctx, ofmt_ctx, out_stream, NULL);

  av_write_trailer(ofmt_ctx);
  printf("Done. Saved to %s\n", OUT_FILENAME);
 
 end:
  av_packet_free(&pkt);
  av_frame_free(&frame);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&ifmt_ctx);
  if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);
 
  return 0;
 }