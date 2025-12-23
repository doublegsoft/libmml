/**
 * FFmpeg C API: Transcode Segment (Process Every Frame)
 * 
 * Logic:
 * 1. Seek to start time (Keyframe).
 * 2. Decode frames.
 * 3. Drop frames that are before the exact start time.
 * 4. Process frames (e.g., modify pixels).
 * 5. Encode frames to output.
 * 6. Stop at end time.
 * 
 * Compile:
 * gcc video_process_seg.c -o video_process_seg -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>

#include "libmml-util.h"
#include "libmml-video.h"
#include "libmml-frame.h"

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  if (argc < 5) {
    printf("Usage: %s <in> <out> <start> <end>\n", argv[0]);
    return 1;
  }

  const char* in_file = argv[1];
  const char* out_file = argv[2];
  double start_sec = mml_time_parse(argv[3]);
  double end_sec   = mml_time_parse(argv[4]);

  AVFormatContext* ifmt_ctx = NULL;
  int vid_idx = -1;
  AVCodecContext* dec_ctx = NULL;
  AVFormatContext* ofmt_ctx = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_stream = NULL;
  int64_t out_pts = 0;
  
  mml_video_load(in_file, &ifmt_ctx, &dec_ctx, &vid_idx, out_file, &ofmt_ctx, &enc_ctx, &out_stream);
  avformat_write_header(ofmt_ctx, NULL);

  mml_video_clip(ifmt_ctx, dec_ctx, vid_idx, ofmt_ctx, enc_ctx, out_stream, argv[3], argv[4], &out_pts);

  mml_frame_write(enc_ctx, ofmt_ctx, out_stream, NULL);
  av_write_trailer(ofmt_ctx);

  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&ifmt_ctx);
  avio_closep(&ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);

  printf("\nDone. Saved to %s\n", out_file);
  return 0;
}