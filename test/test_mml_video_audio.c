/**
 * FFmpeg C API: Transcode + Merge + Reset PTS
 * Logic: 
 * - Frame-by-Frame Video Transcoding.
 * - Audio Loops if shorter than video.
 * - Audio Cuts off if longer than video.
 * 
 * Compile:
 * gcc merge_loop_cut.c -o merge_loop_cut -lavformat -lavcodec -lavutil -lswscale -lswresample
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include "libmml-frame.h"
#include "libmml-video.h"
#include "libmml-audio.h"

#define OUT_FILE "output_smart_merge.mp4"
#define VIDEO_FPS 30

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  if (argc < 3) {
    printf("Usage: %s <video_in> <audio_in>\n", argv[0]);
    return 1;
  }

  AVFormatContext* in_v_fmt = NULL;
  AVFormatContext* in_a_fmt = NULL;
  int in_v_idx = -1;
  int in_a_idx = -1;
  AVCodecContext* v_dec_ctx = NULL;
  AVCodecContext* a_dec_ctx = NULL;

  AVFormatContext* out_fmt = NULL;
  AVCodecContext* v_enc_ctx = NULL;
  AVCodecContext* a_enc_ctx = NULL;
  AVStream* out_v_stream = NULL;
  AVStream* out_a_stream = NULL;

  mml_video_load(argv[1], &in_v_fmt, &v_dec_ctx, &in_v_idx, OUT_FILE, &out_fmt, &v_enc_ctx, &out_v_stream);
  mml_audio_load(argv[2], &in_a_fmt, &a_dec_ctx, &in_a_idx, out_fmt, &a_enc_ctx, &out_a_stream);

  avformat_write_header(out_fmt, NULL);

  mml_video_audio(in_v_fmt, v_dec_ctx, in_v_idx,
                  in_a_fmt, a_dec_ctx, in_a_idx,
                  out_fmt,
                  v_enc_ctx, out_v_stream,
                  a_enc_ctx, out_a_stream);

  mml_frame_write(v_enc_ctx, out_fmt, out_v_stream, NULL);
  mml_frame_write(a_enc_ctx, out_fmt, out_a_stream, NULL);

  av_write_trailer(out_fmt);
  
  avcodec_free_context(&v_dec_ctx); avcodec_free_context(&v_enc_ctx);
  avcodec_free_context(&a_dec_ctx); avcodec_free_context(&a_enc_ctx);
  
  avformat_close_input(&in_v_fmt);
  avformat_close_input(&in_a_fmt);
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone.\n");
  return 0;
}