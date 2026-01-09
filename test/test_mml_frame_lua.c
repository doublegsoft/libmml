/**
 * FFmpeg C API: Pixelate Area (Censorship Box)
 * 
 * Logic:
 * 1. Decode video.
 * 2. Calculate a moving box coordinates.
 * 3. Apply pixelation ONLY inside that box.
 * 4. Encode.
 * 
 * Compile:
 * gcc pixelate_area.c -o pixelate_area -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-video.h"
#include "libmml-image.h"
#include "libmml-frame.h"

#define OUT_FILE "rotate_by_lua.mp4"
#define BOX_W 300
#define BOX_H 200
#define BLOCK_SIZE 16

#define DURATION_SEC 5
#define FPS 30
#define TOTAL_FRAMES (DURATION_SEC * FPS)

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
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

  AVPacket* pkt = av_packet_alloc();
  // Safe work frame (YUV420P)
  AVFrame* work_frame = av_frame_alloc();
  work_frame->format = AV_PIX_FMT_YUV420P;
  work_frame->width = base_frame->width;
  work_frame->height = base_frame->height;
  av_frame_get_buffer(work_frame, 32);

  struct SwsContext* sws = NULL;
  int64_t pts = 0;
  int initialized = 0;
  char* error = NULL;
  for (int i = 0; i < TOTAL_FRAMES; i++) {
    float progress = (float)i / TOTAL_FRAMES;
    float angle = progress * 360.0f;
    mml_frame_lua(base_frame, work_frame, 0, i, "../../script/mml_frame_rotate.lua", &error);
    if (error != NULL) 
    {
      fprintf(stderr, "%s\n", error);
      free(error);
      error = NULL;
    }
    work_frame->pts = i;
    mml_frame_write(enc_ctx, out_fmt, out_st, work_frame);
    // if (i % 30 == 0) printf("Frame %d (Angle %.1f)\r", i, angle);
  }

  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);

  // Cleanup
  if (sws) sws_freeContext(sws);
  av_frame_free(&work_frame);
  av_frame_free(&base_frame);
  av_packet_free(&pkt);
  // avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  // avformat_close_input(&in_fmt);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone.\n");
  return 0;
}