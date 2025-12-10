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
int main(int argc, char* argv[]) {
  const char* image_path = "../../data/1.jpg";

  AVFrame* base_frame = NULL;
  mml_image_frame(image_path, &base_frame);

  AVFormatContext* out_fmt = NULL;
  avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
  
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
  enc_ctx->width = base_frame->width;
  enc_ctx->height = base_frame->height;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = (AVRational){1, FPS};
  enc_ctx->framerate = (AVRational){FPS, 1};
  
  if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) 
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  
  avcodec_open2(enc_ctx, enc, NULL);

  AVStream* out_st = avformat_new_stream(out_fmt, NULL);
  avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
  out_st->time_base = enc_ctx->time_base;

  avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
  avformat_write_header(out_fmt, NULL);

  // 3. Allocate Working Frame (Canvas)
  AVFrame* work_frame = NULL;

  // 4. Animation Loop
  printf("Rendering %d frames...\n", TOTAL_FRAMES);
  
  for (int i = 0; i < TOTAL_FRAMES; i++) {
    // Calculate Angle (0 to 360)
    float progress = (float)i / TOTAL_FRAMES;
    float angle = progress * 360.0f;

    // Apply Rotation (Base -> Work)
    // We read from the static base_frame and write to the changing work_frame
    mml_frame_rotate(base_frame, &work_frame, angle);

    // // Encode
    work_frame->pts = i;
    mml_frame_write(enc_ctx, out_fmt, out_st, work_frame);
    
    if (i % 30 == 0) printf("Frame %d (Angle %.1f)\r", i, angle);
  }

  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);

  // Cleanup
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