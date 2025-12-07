/**
 * FFmpeg C API: Animated "Shattered Glass" Effect
 * 
 * Logic:
 * 1. Frame 0-N: Crack radius expands from 0 to Max.
 * 2. Deterministic Geometry: We use 'srand(CONSTANT_SEED)' every frame.
 *    This ensures the fractal shape remains exactly the same, 
 *    but we simply draw "more of it" as the radius limit increases.
 * 
 * Compile:
 * gcc crack_anim.c -o crack_anim -lavformat -lavcodec -lavutil -lswscale -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#define OUT_FILE "crack_anim.mp4"

// --- Visual Config ---
#define CRACK_Y_SHADOW    10   // Dark
#define CRACK_Y_LIGHT     240  // Bright
#define CRACK_U           128
#define CRACK_V           128

// --- Animation Config ---
#define ANIM_DURATION     45   // Frames for crack to fully spread
#define MAX_RADIUS        500.0f
#define START_DELAY       10   // Wait 10 frames before cracking

// --- Complexity Config ---
#define NUM_MAIN_BRANCHES 10
#define MAX_JITTER        12.0f
#define MIN_SEG_LEN       3.0f
#define SPLIT_CHANCE      20   // % chance to fork

// -----------------------------------------------------------------------------
// Pixel Setter
// -----------------------------------------------------------------------------
void set_pixel_safe(AVFrame* frame, int x, int y, uint8_t y_val, uint8_t u_val, uint8_t v_val) {
  if (x < 0 || x >= frame->width || y < 0 || y >= frame->height) return;
  if (!frame->data[0]) return;

  frame->data[0][y * frame->linesize[0] + x] = y_val;

  if (x % 2 == 0 && y % 2 == 0) {
    int uv_idx = (y / 2) * frame->linesize[1] + (x / 2);
    frame->data[1][uv_idx] = u_val;
    frame->data[2][uv_idx] = v_val;
  }
}

// -----------------------------------------------------------------------------
// Clipped Line Drawer (The Animation Engine)
// -----------------------------------------------------------------------------
// Only draws the pixel if it is within 'limit_radius_sq' from (cx, cy)
void draw_line_clipped(AVFrame* frame, float x1, float y1, float x2, float y2, 
                      int cx, int cy, float limit_radius_sq) 
{
  float dx = x2 - x1;
  float dy = y2 - y1;
  float len = sqrtf(dx*dx + dy*dy);
  if (len < 1.0f) return;

  float step_x = dx / len;
  float step_y = dy / len;

  for (int i = 0; i <= (int)len; i++) {
    int px = (int)(x1 + step_x * i);
    int py = (int)(y1 + step_y * i);

    // --- ANIMATION CHECK ---
    // Calculate distance from center of impact
    float dist_sq = (float)((px - cx)*(px - cx) + (py - cy)*(py - cy));
    
    // If this part of the crack hasn't happened yet, skip it
    if (dist_sq > limit_radius_sq) continue; 
    // -----------------------

    // Shadow (Thicker)
    set_pixel_safe(frame, px, py, CRACK_Y_SHADOW, CRACK_U, CRACK_V);
    set_pixel_safe(frame, px+1, py, CRACK_Y_SHADOW, CRACK_U, CRACK_V);
    
    // Highlight (Thin, Offset)
    set_pixel_safe(frame, px-1, py-1, CRACK_Y_LIGHT, CRACK_U, CRACK_V);
  }
}

// -----------------------------------------------------------------------------
// Recursive Fractal Logic (Passes limit_radius down)
// -----------------------------------------------------------------------------
void draw_fractal_anim(AVFrame* frame, 
                      float x1, float y1, float x2, float y2, 
                      float jitter, int depth,
                      int cx, int cy, float limit_sq) 
{
  float dx = x2 - x1;
  float dy = y2 - y1;
  float len_sq = dx*dx + dy*dy;

  // Base Case
  if (len_sq < (MIN_SEG_LEN * MIN_SEG_LEN)) {
    draw_line_clipped(frame, x1, y1, x2, y2, cx, cy, limit_sq);
    return;
  }

  float mid_x = (x1 + x2) / 2.0f;
  float mid_y = (y1 + y2) / 2.0f;

  // Jitter
  float off_x = ((float)(rand() % 100) / 50.0f - 1.0f) * jitter;
  float off_y = ((float)(rand() % 100) / 50.0f - 1.0f) * jitter;
  mid_x += off_x;
  mid_y += off_y;

  // Branching
  if (depth > 0 && (rand() % 100 < SPLIT_CHANCE)) {
    float branch_len = sqrtf(len_sq) * 0.4f;
    float angle = atan2f(dy, dx) + (M_PI / 2.0f) + (((rand()%100)/100.0f) - 0.5f);
    float fx = mid_x + cosf(angle) * branch_len;
    float fy = mid_y + sinf(angle) * branch_len;
    
    draw_fractal_anim(frame, mid_x, mid_y, fx, fy, jitter * 0.5f, depth - 1, cx, cy, limit_sq);
  }

  // Recurse
  draw_fractal_anim(frame, x1, y1, mid_x, mid_y, jitter * 0.6f, depth, cx, cy, limit_sq);
  draw_fractal_anim(frame, mid_x, mid_y, x2, y2, jitter * 0.6f, depth, cx, cy, limit_sq);
}

// -----------------------------------------------------------------------------
// Main Draw Controller
// -----------------------------------------------------------------------------
void mml_animate_crack(AVFrame* frame, int cx, int cy, float current_radius, unsigned int seed) {
  // 1. RESET SEED
  // This is the magic. By resetting the seed every frame, the fractal 
  // generates the EXACT SAME lines, so they don't move. 
  // We just draw "more" of them based on current_radius.
  srand(seed);

  float limit_sq = current_radius * current_radius;
  float tips_x[NUM_MAIN_BRANCHES];
  float tips_y[NUM_MAIN_BRANCHES];

  // Draw Main Branches
  for (int i = 0; i < NUM_MAIN_BRANCHES; i++) {
    float angle = ((float)i / NUM_MAIN_BRANCHES) * 2.0f * M_PI;
    angle += ((float)(rand() % 100) / 100.0f - 0.5f) * 1.0f; // Randomize spacing

    // Length of this specific crack branch
    float r = MAX_RADIUS * (0.8f + ((float)(rand() % 40) / 100.0f));
    
    float ex = cx + cosf(angle) * r;
    float ey = cy + sinf(angle) * r;

    tips_x[i] = ex;
    tips_y[i] = ey;

    draw_fractal_anim(frame, (float)cx, (float)cy, ex, ey, MAX_JITTER, 4, cx, cy, limit_sq);
  }

  // Draw Webbing
  for (int i = 0; i < NUM_MAIN_BRANCHES; i++) {
    int next = (i + 1) % NUM_MAIN_BRANCHES;
    int webs = 1 + (rand() % 3);
    for (int k = 0; k < webs; k++) {
      float t1 = (float)(20 + rand() % 80) / 100.0f;
      float t2 = (float)(20 + rand() % 80) / 100.0f;

      float p1x = cx + (tips_x[i] - cx) * t1;
      float p1y = cy + (tips_y[i] - cy) * t1;
      float p2x = cx + (tips_x[next] - cx) * t2;
      float p2y = cy + (tips_y[next] - cy) * t2;

      // Note: Webbing calculates distance from 'cx,cy' in the draw_line function, 
      // so webs appear naturally as the radius expands.
      draw_fractal_anim(frame, p1x, p1y, p2x, p2y, MAX_JITTER * 0.5f, 1, cx, cy, limit_sq);
    }
  }
}

// -----------------------------------------------------------------------------
// Encoder
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
  if (argc < 2) { printf("Usage: %s <input>\n", argv[0]); return 1; }

  // 1. Input
  AVFormatContext* in_fmt = NULL;
  avformat_open_input(&in_fmt, argv[1], NULL, NULL);
  avformat_find_stream_info(in_fmt, NULL);
  int vid_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVCodecContext* dec_ctx = avcodec_alloc_context3(NULL);
  avcodec_parameters_to_context(dec_ctx, in_fmt->streams[vid_idx]->codecpar);
  const AVCodec* dec = avcodec_find_decoder(dec_ctx->codec_id);
  avcodec_open2(dec_ctx, dec, NULL);

  // 2. Output
  AVFormatContext* out_fmt = NULL;
  avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
  enc_ctx->width = dec_ctx->width;
  enc_ctx->height = dec_ctx->height;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = (AVRational){1, 30};
  enc_ctx->framerate = (AVRational){30, 1};
  if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  avcodec_open2(enc_ctx, enc, NULL);
  AVStream* out_st = avformat_new_stream(out_fmt, NULL);
  avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
  out_st->time_base = enc_ctx->time_base;
  avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
  avformat_write_header(out_fmt, NULL);

  // 3. Loop
  AVPacket* pkt = av_packet_alloc();
  AVFrame* dec_frame = av_frame_alloc();
  AVFrame* work_frame = av_frame_alloc();
  struct SwsContext* sws = NULL;
  int64_t pts = 0;
  int frame_cnt = 0;
  int init = 0;

  printf("Animating Cracks... Output: %s\n", OUT_FILE);

  while (av_read_frame(in_fmt, pkt) >= 0) {
    if (pkt->stream_index == vid_idx) {
      avcodec_send_packet(dec_ctx, pkt);
      while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
        
        if (!init) {
          sws = sws_getContext(dec_frame->width, dec_frame->height, dec_frame->format,
                              dec_frame->width, dec_frame->height, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, NULL, NULL, NULL);
          work_frame->format = AV_PIX_FMT_YUV420P;
          work_frame->width = dec_frame->width;
          work_frame->height = dec_frame->height;
          av_frame_get_buffer(work_frame, 32);
          init = 1;
        }

        // Convert to canvas
        sws_scale(sws, (const uint8_t* const*)dec_frame->data, dec_frame->linesize,
                  0, dec_frame->height, work_frame->data, work_frame->linesize);

        av_frame_make_writable(work_frame);

        // --- ANIMATION CALCULATION ---
        float radius = 0.0f;
        
        if (frame_cnt > START_DELAY) {
          int anim_frame = frame_cnt - START_DELAY;
          
          if (anim_frame < ANIM_DURATION) {
            // Easing: Fast start, slow end
            float progress = (float)anim_frame / ANIM_DURATION;
            progress = 1.0f - powf(1.0f - progress, 3.0f); // Cubic Ease Out
            radius = MAX_RADIUS * progress;
          } else {
            radius = MAX_RADIUS;
          }
          
          // Draw with dynamic radius, but FIXED SEED (999)
          mml_animate_crack(work_frame, work_frame->width/2, work_frame->height/2, radius, 999);
        }

        encode_write(enc_ctx, out_fmt, out_st, work_frame, &pts);
        frame_cnt++;
        if (pts % 30 == 0) printf("Frame: %ld\r", pts);
      }
    }
    av_packet_unref(pkt);
  }

  encode_write(enc_ctx, out_fmt, out_st, NULL, &pts);
  av_write_trailer(out_fmt);

  // Cleanup...
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