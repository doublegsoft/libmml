/**
 * FFmpeg C API: Image Pixelation (Mosaic) Effect
 * 
 * Logic:
 * 1. Load Image.
 * 2. Loop frames.
 * 3. Copy Original -> Work Frame.
 * 4. Apply Pixelate In-Place on Work Frame.
 *    (Animation: Block size goes from Large -> 1).
 * 
 * Compile:
 * gcc pixelate.c -o pixelate -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-image.h"
#include "libmml-frame.h"

#define OUT_FILE "pixelate_output.mp4"
#define FPS 30
#define DURATION 4 // Seconds

int main(int argc, char* argv[]) {
  const char* image_path = "../../data/1.jpg";
  AVFrame* base_frame = NULL;

  AVFormatContext* out_fmt = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_st = NULL;
  
  mml_image_load(image_path, &base_frame, OUT_FILE, &out_fmt, &enc_ctx, &out_st);

  AVFrame* work_frame = NULL;

  int total_frames = FPS * DURATION;
  int max_block_size = 60; // Start with 60px blocks

  printf("Generating Pixelation... Output: %s\n", OUT_FILE);

  for (int i = 0; i < total_frames; i++) {
    
    float progress = (float)i / total_frames; 
    
    // Ease-out curve (fast clear at start, slow polish at end)
    int current_size = (int)(max_block_size * (1.0f - progress));

    mml_frame_pixelate(base_frame, &work_frame, current_size);

    work_frame->pts = i;
    mml_frame_write(enc_ctx, out_fmt, out_st, work_frame);
    if (i % 15 == 0) printf("Frame %d | Block Size: %d\n", i, current_size);
  }

  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);
  av_frame_free(&base_frame);
  av_frame_free(&work_frame);
  avcodec_free_context(&enc_ctx);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("Done.\n");
  return 0;
}