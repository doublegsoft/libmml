/**
 * FFmpeg C API: In-Place Magnifying Glass (局部放大镜)
 * 
 * Logic:
 * 1. Calculate focus point (cx, cy).
 * 2. Crop a small area around (cx, cy).
 * 3. Scale it up (Zoom).
 * 4. Overlay it back exactly centered at (cx, cy).
 * 5. Draw a border to highlight the box.
 * 
 * Compile:
 * gcc zoom_inplace.c -o zoom_inplace -lavformat -lavcodec -lavutil -lswscale -lm
 */

#include <stdio.h>
#include <math.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-frame.h"

#define OUT_FILE "zoom_inplace.mp4"

// --- Magnifier Config ---
#define ZOOM_FACTOR  2.0f   // Magnification (2.0x)
#define BOX_SIZE     240    // Size of the magnifier window on screen (Square)

// Calculate Crop Size based on Zoom
// Example: Display 240px, Zoom 2x -> We need to crop 120px from source
#define CROP_SIZE    (int)(BOX_SIZE / ZOOM_FACTOR)

// Border Config (Bright Yellow YUV)
#define BORDER_W     4
#define Y_COLOR      226
#define U_COLOR      100
#define V_COLOR      160

// -----------------------------------------------------------------------------
// Helper: Draw Border (To distinguish zoomed area from background)
// -----------------------------------------------------------------------------
void draw_border_rect(AVFrame* frame, int x, int y, int w, int h) {
  // Top & Bottom lines
  for (int i = -BORDER_W; i < w + BORDER_W; i++) {
    for (int t = 0; t < BORDER_W; t++) {
      int bx = x + i;
      int by_top = y - BORDER_W + t;
      int by_bot = y + h + t;
      
      if (bx >= 0 && bx < frame->width) {
        if (by_top >= 0) {
          frame->data[0][by_top * frame->linesize[0] + bx] = Y_COLOR;
          // Apply Chroma only on even coordinates (YUV420P subsampling)
          if (bx % 2 == 0 && by_top % 2 == 0) {
            int uv_idx = (by_top / 2) * frame->linesize[1] + (bx / 2);
            frame->data[1][uv_idx] = U_COLOR;
            frame->data[2][uv_idx] = V_COLOR;
          }
        }
        if (by_bot < frame->height) {
          frame->data[0][by_bot * frame->linesize[0] + bx] = Y_COLOR;
          if (bx % 2 == 0 && by_bot % 2 == 0) {
            int uv_idx = (by_bot / 2) * frame->linesize[1] + (bx / 2);
            frame->data[1][uv_idx] = U_COLOR;
            frame->data[2][uv_idx] = V_COLOR;
          }
        }
      }
    }
  }
  // Left & Right lines
  for (int i = -BORDER_W; i < h + BORDER_W; i++) {
    for (int t = 0; t < BORDER_W; t++) {
      int by = y + i;
      int bx_left = x - BORDER_W + t;
      int bx_right = x + w + t;

      if (by >= 0 && by < frame->height) {
        if (bx_left >= 0) {
          frame->data[0][by * frame->linesize[0] + bx_left] = Y_COLOR;
          if (bx_left % 2 == 0 && by % 2 == 0) {
            int uv_idx = (by / 2) * frame->linesize[1] + (bx_left / 2);
            frame->data[1][uv_idx] = U_COLOR;
            frame->data[2][uv_idx] = V_COLOR;
          }
        }
        if (bx_right < frame->width) {
          frame->data[0][by * frame->linesize[0] + bx_right] = Y_COLOR;
          if (bx_right % 2 == 0 && by % 2 == 0) {
            int uv_idx = (by / 2) * frame->linesize[1] + (bx_right / 2);
            frame->data[1][uv_idx] = U_COLOR;
            frame->data[2][uv_idx] = V_COLOR;
          }
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Helper: Overlay Image (Copy Pixels)
// -----------------------------------------------------------------------------
void overlay_copy(AVFrame* dst, AVFrame* src, int dst_x, int dst_y) {
  // Copy Y Plane
  for (int y = 0; y < src->height; y++) {
    uint8_t* p_dst = dst->data[0] + ((dst_y + y) * dst->linesize[0]) + dst_x;
    uint8_t* p_src = src->data[0] + (y * src->linesize[0]);
    memcpy(p_dst, p_src, src->width);
  }
  // Copy U/V Planes
  int uv_dst_x = dst_x / 2;
  int uv_dst_y = dst_y / 2;
  int uv_w = src->width / 2;
  int uv_h = src->height / 2;
  for (int i = 1; i < 3; i++) {
    for (int y = 0; y < uv_h; y++) {
      uint8_t* p_dst = dst->data[i] + ((uv_dst_y + y) * dst->linesize[i]) + uv_dst_x;
      uint8_t* p_src = src->data[i] + (y * src->linesize[i]);
      memcpy(p_dst, p_src, uv_w);
    }
  }
}

// -----------------------------------------------------------------------------
// Helper: Encoder
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
    printf("Usage: %s <input>\n", argv[0]); 
    return 1; 
  }

  // 1. Input Setup
  AVFormatContext* in_fmt = NULL;
  avformat_open_input(&in_fmt, argv[1], NULL, NULL);
  avformat_find_stream_info(in_fmt, NULL);
  int vid_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVCodecContext* dec_ctx = avcodec_alloc_context3(NULL);
  avcodec_parameters_to_context(dec_ctx, in_fmt->streams[vid_idx]->codecpar);
  const AVCodec* dec = avcodec_find_decoder(dec_ctx->codec_id);
  avcodec_open2(dec_ctx, dec, NULL);

  // 2. Output Setup
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
  avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
  avformat_write_header(out_fmt, NULL);

  // 3. Resources
  AVPacket* pkt = av_packet_alloc();
  AVFrame* dec_frame = av_frame_alloc();
  
  // Working Frames
  AVFrame* canvas = NULL;       // Full size YUV420P canvas
  AVFrame* zoom_frame = NULL;   // The small magnified image
  
  struct SwsContext* main_sws = NULL; // Input -> YUV420P
  struct SwsContext* zoom_sws = NULL; // Crop -> Zoom

  int64_t pts = 0;
  int frame_cnt = 0;

  printf("Generating Magnifying Glass... Output: %s\n", OUT_FILE);

  while (av_read_frame(in_fmt, pkt) >= 0) {
    if (pkt->stream_index == vid_idx) {
      avcodec_send_packet(dec_ctx, pkt);
      while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
        
        // --- Lazy Initialization ---
        if (!main_sws) {
          // Prepare Canvas
          canvas = av_frame_alloc();
          canvas->format = AV_PIX_FMT_YUV420P;
          canvas->width = dec_frame->width;
          canvas->height = dec_frame->height;
          av_frame_get_buffer(canvas, 32);

          // Prepare Zoom Frame
          zoom_frame = av_frame_alloc();
          zoom_frame->format = AV_PIX_FMT_YUV420P;
          zoom_frame->width = BOX_SIZE;
          zoom_frame->height = BOX_SIZE;
          av_frame_get_buffer(zoom_frame, 32);

          // Scaler 1: Input -> YUV420P
          main_sws = sws_getContext(
              dec_frame->width, dec_frame->height, dec_frame->format,
              dec_frame->width, dec_frame->height, AV_PIX_FMT_YUV420P,
              SWS_BILINEAR, NULL, NULL, NULL);

          // Scaler 2: Crop (Small) -> Zoom (Large)
          zoom_sws = sws_getContext(
              CROP_SIZE, CROP_SIZE, AV_PIX_FMT_YUV420P,
              BOX_SIZE, BOX_SIZE, AV_PIX_FMT_YUV420P,
              SWS_BICUBIC, NULL, NULL, NULL); // Bicubic for better upscaling
        }

        // 1. Convert Input Frame to Clean Canvas
        sws_scale(main_sws, (const uint8_t* const*)dec_frame->data, dec_frame->linesize,
                  0, dec_frame->height, canvas->data, canvas->linesize);

        // --- 2. Calculate Focus Point (Animation) ---
        // Circular motion
        float angle = frame_cnt * 0.05f;
        int center_x = canvas->width / 2;
        int center_y = canvas->height / 2;
        int radius = 200;
        
        int cx = center_x + (int)(cos(angle) * radius);
        int cy = center_y + (int)(sin(angle) * radius);

        // --- 3. Calculate Crop Coordinates ---
        int crop_x = cx - (CROP_SIZE / 2);
        int crop_y = cy - (CROP_SIZE / 2);
        
        // Bounds Checking
        if (crop_x < 0) crop_x = 0;
        if (crop_y < 0) crop_y = 0;
        if (crop_x + CROP_SIZE > canvas->width) crop_x = canvas->width - CROP_SIZE;
        if (crop_y + CROP_SIZE > canvas->height) crop_y = canvas->height - CROP_SIZE;
        
        // Align to even numbers for YUV safety
        crop_x &= ~1;
        crop_y &= ~1;

        // --- 4. The Pointer Trick (Virtual Crop) ---
        const uint8_t* src_ptrs[4];
        int src_lines[4];
        src_lines[0] = canvas->linesize[0];
        src_lines[1] = canvas->linesize[1];
        src_lines[2] = canvas->linesize[2];

        // Offset Y Pointer
        src_ptrs[0] = canvas->data[0] + (crop_y * canvas->linesize[0]) + crop_x;
        
        // Offset UV Pointers
        int uv_off = (crop_y / 2) * canvas->linesize[1] + (crop_x / 2);
        src_ptrs[1] = canvas->data[1] + uv_off;
        src_ptrs[2] = canvas->data[2] + uv_off;

        // --- 5. Scale (Zoom) ---
        sws_scale(zoom_sws, src_ptrs, src_lines, 
                  0, CROP_SIZE, // Input Height
                  zoom_frame->data, zoom_frame->linesize);

        // --- 6. In-Place Overlay ---
        // Center the zoomed box over the original crop center
        int dest_x = crop_x - ((BOX_SIZE - CROP_SIZE) / 2);
        int dest_y = crop_y - ((BOX_SIZE - CROP_SIZE) / 2);

        // Bounds Checking for Destination
        if (dest_x < 0) dest_x = 0;
        if (dest_y < 0) dest_y = 0;
        if (dest_x + BOX_SIZE > canvas->width) dest_x = canvas->width - BOX_SIZE;
        if (dest_y + BOX_SIZE > canvas->height) dest_y = canvas->height - BOX_SIZE;

        // Draw Border & Overlay
        mml_frame_circle(canvas, dest_x + BOX_SIZE / 2, dest_y + BOX_SIZE / 2, BOX_SIZE / 2, BORDER_W, Y_COLOR, U_COLOR, V_COLOR);
        mml_frame_overlay(canvas, zoom_frame, dest_x, dest_y); 

        // 7. Encode
        encode_write(enc_ctx, out_fmt, out_st, canvas, &pts);
        frame_cnt++;
        if (frame_cnt % 30 == 0) printf("Frame: %d\r", frame_cnt);
      }
    }
    av_packet_unref(pkt);
  }

  encode_write(enc_ctx, out_fmt, out_st, NULL, &pts);
  av_write_trailer(out_fmt);

  // Cleanup
  if (main_sws) sws_freeContext(main_sws);
  if (zoom_sws) sws_freeContext(zoom_sws);
  if (canvas) av_frame_free(&canvas);
  if (zoom_frame) av_frame_free(&zoom_frame);
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