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

  // 1. Load Source Image
  AVFrame* src_frame = NULL;
  mml_image_frame(image_path, &src_frame);

  // 2. Setup Output
  AVFormatContext* out_fmt = NULL;
  avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
  
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
  enc_ctx->width = src_frame->width;
  enc_ctx->height = src_frame->height;
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

  AVFrame* dst_frame = NULL;

  // 4. Animation Loop
  int total_frames = DURATION_SEC * FPS;
  printf("Rendering Wipe Effect (%d frames)...\n", total_frames);

  for (int i = 0; i < total_frames; i++) {
    
    // Calculate progress (0.0 to 1.0)
    float progress = (float)i / (total_frames - 1); // -1 ensures we hit exactly 1.0 at end

    // Apply the Wipe
    mml_frame_wipe(src_frame, &dst_frame, progress);

    // Encode
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