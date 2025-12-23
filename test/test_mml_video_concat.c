/**
 * FFmpeg C API: Concat Transcoder (No Structs)
 * 
 * Logic:
 * 1. Setup Output (Encoder + Muxer) in main.
 * 2. Loop through Input Files.
 * 3. Pass Output Pointers to processing function.
 * 4. Decode -> Scale -> Re-timestamp -> Encode.
 * 
 * Compile:
 * gcc concat_flat.c -o concat_flat -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-frame.h"
#include "libmml-video.h"

#define OUT_FILE "output_flat.mp4"
#define OUT_W    1920
#define OUT_H    1080
#define OUT_FPS  30

// -----------------------------------------------------------------------------
// Main Application
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Usage: %s <input1> <input2> ...\n", argv[0]);
    return 1;
  }

  // --- 1. Setup Output Variables (Locally) ---
  AVFormatContext* out_fmt_ctx = NULL;
  AVCodecContext* out_enc_ctx = NULL;
  AVStream* out_stream = NULL;
  int64_t global_pts = 0; // Monotonic PTS counter

  // --- 2. Initialize Output ---
  avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, OUT_FILE);
  if (!out_fmt_ctx) return 1;

  const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  out_enc_ctx = avcodec_alloc_context3(encoder);

  // Config
  out_enc_ctx->height = OUT_H;
  out_enc_ctx->width  = OUT_W;
  out_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  out_enc_ctx->time_base = (AVRational){1, OUT_FPS};
  out_enc_ctx->framerate = (AVRational){OUT_FPS, 1};
  
  // Container flags
  if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    out_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  // Speed optimization
  av_opt_set(out_enc_ctx->priv_data, "preset", "fast", 0);

  if (avcodec_open2(out_enc_ctx, encoder, NULL) < 0) return 1;

  // Create Stream
  out_stream = avformat_new_stream(out_fmt_ctx, NULL);
  avcodec_parameters_from_context(out_stream->codecpar, out_enc_ctx);
  out_stream->time_base = out_enc_ctx->time_base;

  // Open File
  if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&out_fmt_ctx->pb, OUT_FILE, AVIO_FLAG_WRITE) < 0) return 1;
  }
  avformat_write_header(out_fmt_ctx, NULL);

  // --- 3. Loop Inputs ---
  for (int i = 1; i < argc; i++) {
    // Pass the individual pointers directly
    mml_video_concat(argv[i], out_fmt_ctx, out_enc_ctx, out_stream, &global_pts);
  }

  // --- 4. Flush Encoder & Cleanup ---
  mml_frame_write(out_enc_ctx, out_fmt_ctx, out_stream, NULL);
  av_write_trailer(out_fmt_ctx);

  if (out_fmt_ctx && !(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_fmt_ctx->pb);
  
  avcodec_free_context(&out_enc_ctx);
  avformat_free_context(out_fmt_ctx);

  printf("Done. Saved to %s\n", OUT_FILE);
  return 0;
}