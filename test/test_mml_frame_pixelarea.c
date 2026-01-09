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

#define OUT_FILE "pixelate_area.mp4"
#define BOX_W 300
#define BOX_H 200
#define BLOCK_SIZE 16

// -----------------------------------------------------------------------------
// Helper: Pixelate a specific rectangular region of a plane
// -----------------------------------------------------------------------------
static void 
pixelate_plane_area(uint8_t* data, int linesize, 
                    int x, int y, int w, int h, 
                    int block_size) 
{
  if (block_size <= 1) return;

  // Iterate only within the bounding box
  // We align the start y/x to the block grid relative to the image 
  // (or relative to box, depending on desired look. Relative to image is more stable).
  
  // Start loop at 'y', but align it if needed. 
  // Here we just loop from y to y+h.
  for (int cur_y = y; cur_y < y + h; cur_y += block_size) {
    for (int cur_x = x; cur_x < x + w; cur_x += block_size) {
      
      // Calculate actual block dimensions (clip to the box area)
      int bw = block_size;
      int bh = block_size;
      
      if (cur_x + bw > x + w) bw = (x + w) - cur_x;
      if (cur_y + bh > y + h) bh = (y + h) - cur_y;

      // Calculate Average
      unsigned int sum = 0;
      for (int by = 0; by < bh; by++) {
        uint8_t* row = data + ((cur_y + by) * linesize);
        for (int bx = 0; bx < bw; bx++) {
          sum += row[cur_x + bx];
        }
      }
      uint8_t avg = (uint8_t)(sum / (bw * bh));

      // Fill Block
      for (int by = 0; by < bh; by++) {
        uint8_t* row = data + ((cur_y + by) * linesize);
        memset(row + cur_x, avg, bw);
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Core: Frame Pixelate Area Wrapper
// -----------------------------------------------------------------------------
/**
* @brief Pixelates a rectangular region of a YUV420P frame.
*/
void 
mml_frame_pixelate_area(AVFrame* frame, int x, int y, int w, int h, int block_size) 
{
  // 1. Boundary Checks
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > frame->width) w = frame->width - x;
  if (y + h > frame->height) h = frame->height - y;
  
  if (w <= 0 || h <= 0) return;
  if (block_size < 2) return;
  if (block_size % 2 != 0) block_size--; // Force even

  // 2. Process Y Plane
  pixelate_plane_area(frame->data[0], frame->linesize[0], 
                      x, y, w, h, block_size);

  // 3. Process U/V Planes (Subsampled)
  int uv_x = x / 2;
  int uv_y = y / 2;
  int uv_w = w / 2;
  int uv_h = h / 2;
  int uv_block = block_size / 2; // Block size also scales down

  pixelate_plane_area(frame->data[1], frame->linesize[1], uv_x, uv_y, uv_w, uv_h, uv_block);
  pixelate_plane_area(frame->data[2], frame->linesize[2], uv_x, uv_y, uv_w, uv_h, uv_block);
}

// -----------------------------------------------------------------------------
// Boilerplate: Encoding Helper
// -----------------------------------------------------------------------------
int encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame, int64_t* pts) {
  if (frame) {
    if (av_frame_make_writable(frame) < 0) return -1;
    frame->pts = (*pts)++;
    frame->pkt_dts = AV_NOPTS_VALUE;
  }
  avcodec_send_frame(enc, frame);
  AVPacket* pkt = av_packet_alloc();
  while (avcodec_receive_packet(enc, pkt) >= 0) {
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

  AVFormatContext* in_fmt = NULL;
  AVCodecContext* dec_ctx = NULL;
  int vid_idx = 0;
  AVFormatContext* out_fmt = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_st = NULL;
   
  mml_video_load(argv[1], &in_fmt, &dec_ctx, &vid_idx, OUT_FILE, &out_fmt, &enc_ctx, &out_st);
  avformat_write_header(out_fmt, NULL);

  AVPacket* pkt = av_packet_alloc();
  AVFrame* src_frame = av_frame_alloc();
  // Safe work frame (YUV420P)
  AVFrame* work_frame = av_frame_alloc();
  struct SwsContext* sws = NULL;
  int64_t pts = 0;
  int initialized = 0;

  printf("Pixelating Moving Area... Output: %s\n", OUT_FILE);

  while (av_read_frame(in_fmt, pkt) >= 0) {
    if (pkt->stream_index == vid_idx) {
      if (avcodec_send_packet(dec_ctx, pkt) == 0) {
        while (avcodec_receive_frame(dec_ctx, src_frame) == 0) {
          
          if (!initialized) {
            sws = sws_getContext(
              src_frame->width, src_frame->height, src_frame->format,
              src_frame->width, src_frame->height, AV_PIX_FMT_YUV420P,
              SWS_BILINEAR, NULL, NULL, NULL);
            
            work_frame->format = AV_PIX_FMT_YUV420P;
            work_frame->width = src_frame->width;
            work_frame->height = src_frame->height;
            av_frame_get_buffer(work_frame, 32);
            initialized = 1;
          }

          // Convert to YUV420P Canvas
          sws_scale(sws, (const uint8_t* const*)src_frame->data, src_frame->linesize,
                    0, src_frame->height, work_frame->data, work_frame->linesize);

          // Ensure Writable
          av_frame_make_writable(work_frame);

          // --- ANIMATION: Moving Box ---
          // Simple bouncing logic
          int speed = 10;
          int max_x = work_frame->width - BOX_W;
          int box_x = (pts * speed) % (max_x * 2); 
          if (box_x > max_x) box_x = max_x * 2 - box_x; // Bounce back
          
          int box_y = 200; // Fixed height

          // Apply Area Pixelation
          mml_frame_pixelate_area(work_frame, box_x, box_y, BOX_W, BOX_H, BLOCK_SIZE);

          encode_write(enc_ctx, out_fmt, out_st, work_frame, &pts);
          if (pts % 30 == 0) printf("Frame: %ld\r", pts);
        }
      }
    }
    av_packet_unref(pkt);
  }

  encode_write(enc_ctx, out_fmt, out_st, NULL, &pts);
  av_write_trailer(out_fmt);

  // Cleanup
  if (sws) sws_freeContext(sws);
  av_frame_free(&work_frame);
  av_frame_free(&src_frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&in_fmt);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone.\n");
  return 0;
}