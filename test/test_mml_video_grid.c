/**
 * FFmpeg C API: 2x2 Video Grid (No Custom Structs)
 * 
 * Compile:
 * gcc grid_flat.c -o grid_flat -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-video.h"
#include "libmml-frame.h"

#define OUT_W 1618
#define OUT_H 1080
#define OUT_FILE "grid_output_flat.mp4"

// Quadrant Size (960x540)
#define QUAD_W (OUT_W / 2)
#define QUAD_H (OUT_H / 2)

// -----------------------------------------------------------------------------
// Helper: Copy pixels from a small frame to a quadrant of the large canvas
// -----------------------------------------------------------------------------
void copy_pixels_to_quadrant(AVFrame* dst, AVFrame* src, int idx) {
  // idx 0=TL, 1=TR, 2=BL, 3=BR
  int off_x = (idx % 2) * QUAD_W;
  int off_y = (idx / 2) * QUAD_H;

  // 1. Y Plane
  for (int y = 0; y < src->height; y++) {
    uint8_t* p_dst = dst->data[0] + ((off_y + y) * dst->linesize[0]) + off_x;
    uint8_t* p_src = src->data[0] + (y * src->linesize[0]);
    memcpy(p_dst, p_src, src->width);
  }

  // 2. U and V Planes (Half size for YUV420P)
  int uv_off_x = off_x / 2;
  int uv_off_y = off_y / 2;
  int uv_w = src->width / 2;
  int uv_h = src->height / 2;

  for (int i = 1; i < 3; i++) {
    for (int y = 0; y < uv_h; y++) {
      uint8_t* p_dst = dst->data[i] + ((uv_off_y + y) * dst->linesize[i]) + uv_off_x;
      uint8_t* p_src = src->data[i] + (y * src->linesize[i]);
      memcpy(p_dst, p_src, uv_w);
    }
  }
}

// -----------------------------------------------------------------------------
// Helper: Get Next Decoded Frame
// Reads packets until a video frame is decoded or EOF is reached.
// -----------------------------------------------------------------------------
int get_next_frame(AVFormatContext* fmt, 
                  AVCodecContext* dec, 
                  int stream_idx, 
                  AVFrame* frame) {
  AVPacket* pkt = av_packet_alloc();
  int ret = -1; // Default to error/EOF

  while (av_read_frame(fmt, pkt) >= 0) {
    if (pkt->stream_index == stream_idx) {
      if (avcodec_send_packet(dec, pkt) == 0) {
        if (avcodec_receive_frame(dec, frame) == 0) {
          ret = 0; // Success
          av_packet_unref(pkt);
          break; 
        }
      }
    }
    av_packet_unref(pkt);
  }
  
  av_packet_free(&pkt);
  return ret;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {

  // --- ARRAYS FOR INPUT STATE (Replacing Struct) ---
  AVFormatContext* in_fmts[4] = {0};
  AVCodecContext*  in_decs[4] = {0};
  int              in_idxs[4] = {0};
  AVFrame*         in_raw_frames[4] = {0};   // Full res input
  AVFrame*         in_small_frames[4] = {0}; // 960x540 input
  struct SwsContext* in_scalers[4] = {0};
  int              in_finished[4] = {0};

  AVFormatContext* out_fmt = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_st = NULL;

  // 1. Initialize Inputs
  for (int i = 0; i < 4; i++) {
    in_raw_frames[i] = av_frame_alloc();

    in_small_frames[i] = av_frame_alloc();
    in_small_frames[i]->format = AV_PIX_FMT_YUV420P;
    in_small_frames[i]->width  = QUAD_W;
    in_small_frames[i]->height = QUAD_H;
    av_frame_get_buffer(in_small_frames[i], 32);
  }

  mml_video_load("../../data/V3.10.mp4", &in_fmts[0], &in_decs[0], &in_idxs[0], OUT_FILE, &out_fmt, &enc_ctx, &out_st);
  mml_video_load("../../data/V3.10.mp4", &in_fmts[1], &in_decs[1], &in_idxs[1], NULL, NULL, NULL, NULL);
  mml_video_load("../../data/V3.10.mp4", &in_fmts[2], &in_decs[2], &in_idxs[2], NULL, NULL, NULL, NULL);
  mml_video_load("../../data/V3.10.mp4", &in_fmts[3], &in_decs[3], &in_idxs[3], NULL, NULL, NULL, NULL);

  printf("############# %lld\n", enc_ctx->width);
  // 3. Canvas Frame
  AVFrame* canvas = av_frame_alloc();
  canvas->format = AV_PIX_FMT_YUV420P;
  canvas->width = OUT_W;
  canvas->height = OUT_H;
  av_frame_get_buffer(canvas, 32);

  // Initialize canvas to black/grey
  memset(canvas->data[0], 0, canvas->height * canvas->linesize[0]);
  memset(canvas->data[1], 128, (canvas->height/2) * canvas->linesize[1]);
  memset(canvas->data[2], 128, (canvas->height/2) * canvas->linesize[2]);

  int64_t pts = 0;
  int all_done = 0;

  printf("Processing Grid... Output: %s\n", OUT_FILE);

  // 4. Main Processing Loop
  while (!all_done) {
    int active_inputs = 0;

    // Loop through all 4 inputs
    for (int i = 0; i < 4; i++) {
      if (!in_finished[i]) {
        if (get_next_frame(in_fmts[i], in_decs[i], in_idxs[i], in_raw_frames[i]) == 0) {
          if (!in_scalers[i]) {
            in_scalers[i] = sws_getContext(
                in_decs[i]->width, in_decs[i]->height, in_decs[i]->pix_fmt,
                QUAD_W, QUAD_H, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, NULL, NULL, NULL);
          }

          // Scale: Raw -> Small
          sws_scale(in_scalers[i], 
                    (const uint8_t* const*)in_raw_frames[i]->data, 
                    in_raw_frames[i]->linesize,
                    0, 
                    in_raw_frames[i]->height,
                    in_small_frames[i]->data, 
                    in_small_frames[i]->linesize);
          
          copy_pixels_to_quadrant(canvas, in_small_frames[i], i);
          
          active_inputs++;
        } else {
          // Mark this input as finished
          in_finished[i] = 1;
        }
      }
    }

    if (active_inputs == 0) {
      all_done = 1;
      break;
    }

    av_frame_make_writable(canvas);

    // Encode
    canvas->pts = pts++;
    mml_frame_write(enc_ctx, out_fmt, out_st, canvas);
    if (pts % 30 == 0) printf("Frames: %lld\r", pts);
  }

  // 5. Flush & Cleanup
  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);

  // Clean Inputs
  for (int i = 0; i < 4; i++) {
    if (in_fmts[i]) avformat_close_input(&in_fmts[i]);
    if (in_decs[i]) avcodec_free_context(&in_decs[i]);
    if (in_raw_frames[i]) av_frame_free(&in_raw_frames[i]);
    if (in_small_frames[i]) av_frame_free(&in_small_frames[i]);
    if (in_scalers[i]) sws_freeContext(in_scalers[i]);
  }

  // Clean Output
  if (out_fmt && !(out_fmt->oformat->flags & AVFMT_NOFILE))
    avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);
  avcodec_free_context(&enc_ctx);
  av_frame_free(&canvas);

  printf("\nDone.\n");
  return 0;
}