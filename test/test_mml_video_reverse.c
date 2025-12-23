/**
 * FFmpeg C API: Manual Reverse + Slow Motion (No Filters)
 * 
 * Logic:
 * 1. Decode entire video -> Store frames in a dynamic array (RAM).
 * 2. Iterate array backwards (Reverse).
 * 3. Encode each frame N times (Slow Motion).
 * 
 * Compile:
 * gcc reverse_slow_manual.c -o reverse_slow_manual -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-frame.h"
#include "libmml-video.h"

#define OUT_FILE "reverse_slow_manual.mp4"

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Usage: %s <input_video>\n", argv[0]);
    return 1;
  }

  // 1. Setup Input
  AVFormatContext* in_fmt = NULL;
  AVCodecContext* dec_ctx = NULL;
  AVFormatContext* out_fmt = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_st = NULL;
  int vid_idx = -1;
  int64_t pts = 0;
  
  mml_video_load(argv[1], &in_fmt, &dec_ctx, &vid_idx, OUT_FILE, &out_fmt, &enc_ctx, &out_st);
  avformat_write_header(out_fmt, NULL);

  mml_video_reverse(in_fmt, dec_ctx, vid_idx, out_fmt, enc_ctx, out_st, 4, &pts);

  // 5. Cleanup
  mml_frame_write(enc_ctx, out_fmt, out_st, NULL); // Flush
  av_write_trailer(out_fmt);

  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&in_fmt);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone. Output: %s\n", OUT_FILE);
  return 0;
}