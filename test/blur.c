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

#define OUT_FILE "blurred_output.mp4"


// --- Blur Configuration ---
#define BLUR_X      300    // Top-Left X
#define BLUR_Y      200    // Top-Left Y
#define BLUR_W      400    // Width of area
#define BLUR_H      300    // Height of area
#define BLUR_RADIUS 10     // Strength of blur (Higher = Blurrier, Slower)

// -----------------------------------------------------------------------------
// Helper: Apply Box Blur to a single plane (Y, U, or V)
// -----------------------------------------------------------------------------
void blur_plane(uint8_t* data, int linesize, int x_off, int y_off, int w, int h, int radius) {
  // 1. Allocate a temp buffer for this region
  // We need this to read "original" pixels while writing "new" pixels.
  // Using a flat array for simplicity.
  uint8_t* temp = (uint8_t*)malloc(w * h);
  if (!temp) return;

  // Copy original pixels to temp
  for (int y = 0; y < h; y++) {
    memcpy(temp + (y * w), 
          data + ((y_off + y) * linesize) + x_off, 
          w);
  }

  // 2. Perform Blur (Average of neighbors)
  // Iterate over every pixel in the ROI
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      
      int sum = 0;
      int count = 0;

      // Accumulate neighbors (Kernel Loop)
      for (int ky = -radius; ky <= radius; ky++) {
        for (int kx = -radius; kx <= radius; kx++) {
          
          int nx = x + kx;
          int ny = y + ky;

          // Boundary checks (clamp to ROI)
          if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
            sum += temp[ny * w + nx];
            count++;
          }
        }
      }

      // Write averaged value back to the main frame
      uint8_t avg = (uint8_t)(sum / count);
      data[((y_off + y) * linesize) + (x_off + x)] = avg;
    }
  }

  free(temp);
}

// -----------------------------------------------------------------------------
// Helper: Blur Area Wrapper (Handles YUV420P Logic)
// -----------------------------------------------------------------------------
void apply_blur(AVFrame* frame) {
  // 1. Blur Y Plane (Luma) - Full Resolution
  // Check bounds
  int bx = BLUR_X;
  int by = BLUR_Y;
  int bw = BLUR_W;
  int bh = BLUR_H;

  // Clamp to frame dimensions
  if (bx < 0) bx = 0;
  if (by < 0) by = 0;
  if (bx + bw > frame->width) bw = frame->width - bx;
  if (by + bh > frame->height) bh = frame->height - by;

  // Process Y
  blur_plane(frame->data[0], frame->linesize[0], bx, by, bw, bh, BLUR_RADIUS);

  // 2. Blur U and V Planes (Chroma) - Half Resolution
  // In YUV420P, chroma is subsampled by 2.
  // We must divide all coordinates/dimensions by 2.
  int uv_x = bx / 2;
  int uv_y = by / 2;
  int uv_w = bw / 2;
  int uv_h = bh / 2;
  // Radius also scales down slightly for efficiency, or keep same for smoothness
  int uv_rad = BLUR_RADIUS / 2; 
  if (uv_rad < 1) uv_rad = 1;

  blur_plane(frame->data[1], frame->linesize[1], uv_x, uv_y, uv_w, uv_h, uv_rad);
  blur_plane(frame->data[2], frame->linesize[2], uv_x, uv_y, uv_w, uv_h, uv_rad);
}

// -----------------------------------------------------------------------------
// Helper: Encode and Write
// -----------------------------------------------------------------------------
int encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame, int64_t* pts) {
  if (frame) {
    if (av_frame_make_writable(frame) < 0) return -1;
    frame->pts = (*pts)++;
    frame->pkt_dts = AV_NOPTS_VALUE;
  }

  int ret = avcodec_send_frame(enc, frame);
  if (ret < 0) return ret;
  
  AVPacket* pkt = av_packet_alloc();
  while (ret >= 0) {
    ret = avcodec_receive_packet(enc, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      av_packet_free(&pkt);
      return 0;
    }
    av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
    pkt->stream_index = st->index;
    av_interleaved_write_frame(fmt, pkt);
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  return 0;
}

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
  if (avformat_open_input(&in_fmt, argv[1], NULL, NULL) < 0) return 1;
  avformat_find_stream_info(in_fmt, NULL);
  int vid_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVCodecContext* dec_ctx = avcodec_alloc_context3(NULL);
  avcodec_parameters_to_context(dec_ctx, in_fmt->streams[vid_idx]->codecpar);
  const AVCodec* dec = avcodec_find_decoder(dec_ctx->codec_id);
  avcodec_open2(dec_ctx, dec, NULL);

  // 2. Setup Output
  AVFormatContext* out_fmt = NULL;
  avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
  enc_ctx->width = dec_ctx->width;
  enc_ctx->height = dec_ctx->height;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = (AVRational){1, 30};
  enc_ctx->framerate = (AVRational){30, 1};
  if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) 
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  avcodec_open2(enc_ctx, enc, NULL);
  AVStream* out_st = avformat_new_stream(out_fmt, NULL);
  avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
  out_st->time_base = enc_ctx->time_base;
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
    avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
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
        encode_write(enc_ctx, out_fmt, out_st, work_frame, &pts);
        
        if (pts % 30 == 0) printf("Processed: %ld\r", pts);
      }
    }
    av_packet_unref(pkt);
  }

  // Flush
  encode_write(enc_ctx, out_fmt, out_st, NULL, &pts);
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