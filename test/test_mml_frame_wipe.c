/**
 * FFmpeg C API: Image Wipe Effect (Left to Right)
 * 
 * Compilation:
 * gcc wipe_effect.c -o wipe_effect -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-image.h"
#include "libmml-frame.h"
#include "libmml-video.h"

#define OUT_FILE "wipe_output.mp4"
#define DURATION_SEC 3.0
#define FPS 30

int main(int argc, char* argv[]) 
{
  const char* image_path = "../../data/1.jpg";

  AVFrame* src_frame = NULL;
  AVFormatContext* out_fmt = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_st = NULL;
  mml_image_load(image_path, 
                 &src_frame,
                 OUT_FILE,
                 &out_fmt,
                 &enc_ctx,
                 &out_st);
  avformat_write_header(out_fmt, NULL);

  AVFrame* dst_frame = NULL;

  // 4. Animation Loop
  int total_frames = DURATION_SEC * FPS;
  printf("Rendering Wipe Effect (%d frames)...\n", total_frames);

  for (int i = 0; i < total_frames; i++) {
    
    float progress = (float)i / (total_frames - 1); // -1 ensures we hit exactly 1.0 at end
    mml_frame_wipe(src_frame, &dst_frame, progress);
    dst_frame->pts = i;
    mml_frame_write(enc_ctx, out_fmt, out_st, dst_frame);
    
    if (i % 30 == 0) printf("Frame: %d (%.2f%%)\r", i, progress * 100);
  }

  // Flush
  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);

  // Cleanup
  av_frame_free(&src_frame);
  av_frame_free(&dst_frame);
  avcodec_free_context(&enc_ctx);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone.\n");
  return 0;
}