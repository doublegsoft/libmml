/**
 * FFmpeg C API: Image Spin Animation
 * 
 * Logic:
 * 1. Load Image -> 'Base Frame' (YUV420P).
 * 2. Allocate 'Work Frame' (Canvas).
 * 3. Loop:
 *    - Calculate Angle (0 -> 360).
 *    - Apply Inverse Rotation mapping (Base -> Work).
 *    - Encode 'Work Frame'.
 */

#include <stdio.h>
#include <math.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-image.h"
#include "libmml-frame.h"
#include "libmml-video.h"

#define OUT_FILE "spin_output.mp4"
#define DURATION_SEC 5
#define FPS 30
#define TOTAL_FRAMES (DURATION_SEC * FPS)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) 
{
  const char* image_path = "../../data/1.jpg";

  AVFrame* base_frame = NULL;
  AVFormatContext* out_fmt = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_st = NULL;

  mml_image_load(image_path, 
                 &base_frame,
                 OUT_FILE,
                 &out_fmt,
                 &enc_ctx,
                 &out_st);

  avformat_write_header(out_fmt, NULL);

  AVFrame* work_frame = NULL;
  printf("Rendering %d frames...\n", TOTAL_FRAMES);
  
  for (int i = 0; i < TOTAL_FRAMES; i++) {
    float progress = (float)i / TOTAL_FRAMES;
    float angle = progress * 360.0f;
    mml_frame_rotate(base_frame, &work_frame, angle);
    work_frame->pts = i;
    mml_frame_write(enc_ctx, out_fmt, out_st, work_frame);
    if (i % 30 == 0) printf("Frame %d (Angle %.1f)\r", i, angle);
  }

  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);

  av_frame_free(&base_frame);
  av_frame_free(&work_frame);
  avcodec_free_context(&enc_ctx);
  if (out_fmt) {
    avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
  }

  printf("\nDone. Saved to %s\n", OUT_FILE);
  return 0;
}