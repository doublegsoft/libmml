/**
 * FFmpeg C API: Blur Specific Area (Box Blur)
 * 
 * Logic:
 * 1. Decode video.
 * 2. Convert to YUV420P (Safe format).
 * 3. Apply Box Blur algorithm to a specific rectangle (Luma + Chroma).
 * 4. Encode to output.
 *
 * Compile:
 * gcc video_blur.c -o video_blur -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-frame.h"
#include "libmml-video.h"

#define OUT_FILE "blurred_output.mp4"


// --- Blur Configuration ---
#define BLUR_X      300    // Top-Left X
#define BLUR_Y      200    // Top-Left Y
#define BLUR_W      400    // Width of area
#define BLUR_H      300    // Height of area
#define BLUR_RADIUS 10     // Strength of blur (Higher = Blurrier, Slower)

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  const char* video_path = "../../data/V3.10.mp4";

  // 1. Setup Input
  AVFormatContext* in_fmt = NULL;
  int vid_idx = -1;
  AVCodecContext* dec_ctx = NULL;

  // 2. Setup Output
  AVFormatContext* out_fmt = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_st = NULL;

  mml_video_load(video_path, &in_fmt, &dec_ctx, &vid_idx, OUT_FILE, &out_fmt, &enc_ctx, &out_st);
  avformat_write_header(out_fmt, NULL);

  // 3. Buffers
  AVPacket* pkt = av_packet_alloc();
  AVFrame* dec_frame = av_frame_alloc();
  
  // Work Frame (Safe YUV420P)
  AVFrame* work_frame = av_frame_alloc();
  work_frame->format = AV_PIX_FMT_YUV420P;
  work_frame->width = dec_ctx->width;
  work_frame->height = dec_ctx->height;
  av_frame_get_buffer(work_frame, 32);

  struct SwsContext* sws = NULL;
  int64_t pts = 0;

  printf("Blurring Area (%d,%d %dx%d)... Output: %s\n", 
        BLUR_X, BLUR_Y, BLUR_W, BLUR_H, OUT_FILE);

  while (av_read_frame(in_fmt, pkt) >= 0) {
    if (pkt->stream_index == vid_idx) {
      avcodec_send_packet(dec_ctx, pkt);
      while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
        
        // Lazy Init Scaler (Any format -> YUV420P)
        if (!sws) {
          sws = sws_getContext(
              dec_frame->width, dec_frame->height, dec_frame->format,
              dec_frame->width, dec_frame->height, AV_PIX_FMT_YUV420P,
              SWS_BILINEAR, NULL, NULL, NULL);
        }

        // 1. Convert to Safe YUV420P
        sws_scale(sws, (const uint8_t* const*)dec_frame->data, dec_frame->linesize,
                  0, dec_frame->height, work_frame->data, work_frame->linesize);

        // 2. Apply Blur
        // (Ensure work_frame is writable)
        av_frame_make_writable(work_frame);
        mml_frame_blur(work_frame, BLUR_X, BLUR_Y, BLUR_W, BLUR_H, BLUR_RADIUS);

        // 3. Encode
        work_frame->pts = pts++;
        mml_frame_write(enc_ctx, out_fmt, out_st, work_frame);
        
        if (pts % 30 == 0) printf("Processed: %ld\r", pts);
      }
    }
    av_packet_unref(pkt);
  }

  // Flush
  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);

  // Cleanup
  if (sws) sws_freeContext(sws);
  av_frame_free(&work_frame);
  av_frame_free(&dec_frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&in_fmt);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone.\n");
  return 0;
}