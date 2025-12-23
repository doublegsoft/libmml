#include <stdio.h>
#include <math.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-shape.h"
#include "libmml-frame.h"
#include "libmml-video.h"

#define OUT_FILE "arrow_final_fixed.mp4"

// --- Visual Config ---
#define DRAW_Y 160
#define DRAW_U 60
#define DRAW_V 200
#define LINE_THICKNESS 5.0f   // 线条加粗
#define DASH_LENGTH    40.0f  // 实线部分加长
#define GAP_LENGTH     25.0f  // 间隙加长 (必须远大于线宽)
#define FLOW_SPEED     8.0f   // 流动速度 (像素/帧)
#define ARROW_HEAD_LEN 30.0
#define ARROW_HEAD_ANGLE (M_PI / 6.0)

// --- Animation Config ---
#define EXTEND_FRAMES 45
#define BLINK_INTERVAL 10
#define BLINK_COUNT    2
const int TOTAL_ANIM_FRAMES = (EXTEND_FRAMES * 2) + (BLINK_COUNT * 2 * BLINK_INTERVAL);

// -----------------------------------------------------------------------------
// Drawing Helpers (Bounds Checked)
// -----------------------------------------------------------------------------
void set_pixel_safe(AVFrame* frame, int x, int y, uint8_t yv, uint8_t uv, uint8_t vv, float alpha) {
  if (x < 0 || x >= frame->width || y < 0 || y >= frame->height) return;
  if (!frame->data[0] || !frame->data[1] || !frame->data[2]) return;

  int y_idx = y * frame->linesize[0] + x;
  uint8_t bg = frame->data[0][y_idx];
  frame->data[0][y_idx] = (uint8_t)(yv * alpha + bg * (1.0f - alpha));

  if (x % 2 == 0 && y % 2 == 0) {
    int uv_idx = (y / 2) * frame->linesize[1] + (x / 2);
    if (alpha > 0.5f) {
      frame->data[1][uv_idx] = uv;
      frame->data[2][uv_idx] = vv;
    }
  }
}

void draw_segment(AVFrame* frame, float x1, float y1, float x2, float y2) {
  int min_x = (int)fmin(x1, x2) - LINE_THICKNESS - 2;
  int min_y = (int)fmin(y1, y2) - LINE_THICKNESS - 2;
  int max_x = (int)fmax(x1, x2) + LINE_THICKNESS + 2;
  int max_y = (int)fmax(y1, y2) + LINE_THICKNESS + 2;

  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  if (max_x >= frame->width) max_x = frame->width - 1;
  if (max_y >= frame->height) max_y = frame->height - 1;

  float dx = x2 - x1; float dy = y2 - y1; float len_sq = dx*dx + dy*dy;
  if (len_sq < 0.001) return;

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      float t = ((x - x1) * dx + (y - y1) * dy) / len_sq;
      if (t < 0) t = 0; if (t > 1) t = 1;
      float cx = x1 + t * dx; float cy = y1 + t * dy;
      float dist = sqrtf(pow(x - cx, 2) + pow(y - cy, 2));
      
      if (dist < LINE_THICKNESS) {
        float alpha = 1.0f;
        if (dist > LINE_THICKNESS - 1.0f) alpha = LINE_THICKNESS - dist;
        set_pixel_safe(frame, x, y, DRAW_Y, DRAW_U, DRAW_V, alpha);
      }
    }
  }
}

void draw_dashed_segment_flow(AVFrame* frame, float x1, float y1, float x2, float y2, float phase) {
  // 1. 计算包围盒 (Bounding Box)
  int min_x = (int)fmin(x1, x2) - LINE_THICKNESS - 2;
  int min_y = (int)fmin(y1, y2) - LINE_THICKNESS - 2;
  int max_x = (int)fmax(x1, x2) + LINE_THICKNESS + 2;
  int max_y = (int)fmax(y1, y2) + LINE_THICKNESS + 2;

  // 安全边界检查
  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  if (max_x >= frame->width) max_x = frame->width - 1;
  if (max_y >= frame->height) max_y = frame->height - 1;

  float dx = x2 - x1;
  float dy = y2 - y1;
  float total_len_sq = dx * dx + dy * dy;
  float total_len = sqrtf(total_len_sq);
  
  if (total_len < 0.1f) return;

  // 单位向量
  float unit_dx = dx / total_len;
  float unit_dy = dy / total_len;

  float period = DASH_LENGTH + GAP_LENGTH;

  // 2. 遍历像素
  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      
      // 计算点到直线的投影 t (0.0 - 1.0)
      float t = ((x - x1) * dx + (y - y1) * dy) / total_len_sq;
      
      // 超出线段两端的不画
      if (t < 0 || t > 1) continue;

      // 计算点到直线的垂直距离
      float close_x = x1 + t * dx;
      float close_y = y1 + t * dy;
      float dist_to_line = sqrtf(pow(x - close_x, 2) + pow(y - close_y, 2));

      // 如果超出线宽，直接跳过
      if (dist_to_line > LINE_THICKNESS) continue;

      // --- 虚线核心逻辑 ---
      
      // 1. 计算沿线的距离
      float dist_along_line = (x - x1) * unit_dx + (y - y1) * unit_dy;
      
      // 2. 加上相位偏移 (实现流动) - 这里减去 phase 让箭头向前跑
      float flow_dist = dist_along_line - phase;

      // 3. 取模运算，得到在 [0, period] 周期内的位置
      // 使用 fmodf 时要注意负数处理，确保结果为正
      float cycle_pos = fmodf(flow_dist, period);
      if (cycle_pos < 0) cycle_pos += period;

      // 4. 判断 Gap
      if (cycle_pos > DASH_LENGTH) continue; // 这里是 Gap，直接不画

      // --- 抗锯齿绘制 ---
      
      float alpha = 1.0f;

      // 边缘羽化 (垂直方向)
      if (dist_to_line > LINE_THICKNESS - 1.5f) {
        alpha = LINE_THICKNESS - dist_to_line;
      }

      // (可选) 虚线头尾稍微羽化一点点，避免锯齿，但不要太糊
      // 靠近 Dash 开始 或 靠近 Dash 结束 的 1 像素范围内
      /*
      float dist_to_dash_edge = fmin(cycle_pos, DASH_LENGTH - cycle_pos);
      if (dist_to_dash_edge < 1.0f) {
          alpha *= dist_to_dash_edge;
      }
      */

      set_pixel_safe(frame, x, y, DRAW_Y, DRAW_U, DRAW_V, alpha);
    }
  }
}

void draw_arrow_head(AVFrame* frame, float tip_x, float tip_y, float angle) {
  float x1 = tip_x - ARROW_HEAD_LEN * cosf(angle - ARROW_HEAD_ANGLE);
  float y1 = tip_y - ARROW_HEAD_LEN * sinf(angle - ARROW_HEAD_ANGLE);
  float x2 = tip_x - ARROW_HEAD_LEN * cosf(angle + ARROW_HEAD_ANGLE);
  float y2 = tip_y - ARROW_HEAD_LEN * sinf(angle + ARROW_HEAD_ANGLE);
  draw_segment(frame, tip_x, tip_y, x1, y1);
  draw_segment(frame, tip_x, tip_y, x2, y2);
}

// -----------------------------------------------------------------------------
// Animation Logic
// -----------------------------------------------------------------------------
void draw_anim(AVFrame* frame, int idx) {
  float sx = 200, sy = frame->height - 200;
  float ex = frame->width - 200, ey = 200;
  float p = 0.0f;
  int visible = 0;

  int p1 = EXTEND_FRAMES;
  int p2 = EXTEND_FRAMES * 2;
  int p3 = p2 + (BLINK_COUNT * 2 * BLINK_INTERVAL);

  if (idx < p1) {
    p = (float)idx / EXTEND_FRAMES; 
    visible = 1;
  } else if (idx < p2) {
    p = (float)(idx - p1) / EXTEND_FRAMES; 
    visible = 1;
  } else if (idx < p3) {
    p = 1.0f;
    int bf = idx - p2;
    if ((bf / BLINK_INTERVAL) % 2 == 0) visible = 1;
  }

  if (visible) {
    p = p * (2.0f - p);
    float cx = sx + (ex - sx) * p;
    float cy = sy + (ey - sy) * p;
    draw_segment(frame, sx, sy, cx, cy);
    if (p > 0.05f) draw_arrow_head(frame, cx, cy, atan2f(ey - sy, ex - sx));
  }
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {

  // 1. Input
  AVFormatContext* in_fmt = NULL;
  int vid_idx = -1;
  AVCodecContext* dec_ctx = NULL;

  // 2. Output
  AVFormatContext* out_fmt = NULL;
  const AVCodec* enc = NULL;
  AVCodecContext* enc_ctx = NULL;
  AVStream* out_st = NULL;

  mml_video_load("../../data/V3.10.mp4", &in_fmt, &dec_ctx, &vid_idx, OUT_FILE, &out_fmt, &enc_ctx, &out_st);
  avformat_write_header(out_fmt, NULL);

  // 3. Runtime
  AVPacket* pkt = av_packet_alloc();
  AVFrame* dec_frame = av_frame_alloc();
  struct SwsContext* sws = NULL;
  AVFrame* bg_frame = NULL;
  AVFrame* canvas = NULL;
  int64_t g_pts = 0;
  int intro_done = 0;

  printf("Processing... Output: %s\n", OUT_FILE);

  while (av_read_frame(in_fmt, pkt) >= 0) {
    if (pkt->stream_index == vid_idx) {
      avcodec_send_packet(dec_ctx, pkt);
      while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
        
        // Lazy Init
        if (!sws) {
          sws = sws_getContext(
            dec_frame->width, dec_frame->height, dec_frame->format,
            dec_frame->width, dec_frame->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, NULL, NULL, NULL);

          // canvas = av_frame_alloc();
          // canvas->format = AV_PIX_FMT_YUV420P;
          // canvas->width = dec_frame->width;
          // canvas->height = dec_frame->height;
          // av_frame_get_buffer(canvas, 32);
        }
        mml_frame_capture(sws, dec_frame, &canvas);
        // Intro (Frozen Frame Animation)
        if (!intro_done) {
          mml_frame_capture(sws, dec_frame, &bg_frame);
          for (int i = 0; i < TOTAL_ANIM_FRAMES; i++) {
            av_frame_copy(canvas, bg_frame);
            av_frame_make_writable(canvas);
            draw_anim(canvas, i);
            canvas->pts = g_pts++;
            mml_frame_write(enc_ctx, out_fmt, out_st, canvas);
          }
          intro_done = 1;
        }
        // sws_scale(sws, (const uint8_t* const*)dec_frame->data, dec_frame->linesize,
        //           0, dec_frame->height, canvas->data, canvas->linesize);
        canvas->pts = g_pts++;          
        mml_frame_write(enc_ctx, out_fmt, out_st, canvas);
      }
    }
    av_packet_unref(pkt);
  }

  // Flush (Pass NULL frame)
  avcodec_send_frame(enc_ctx, NULL);
  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);

  // Cleanup
  if (sws) sws_freeContext(sws);
  if (bg_frame) av_frame_free(&bg_frame);
  if (canvas) av_frame_free(&canvas);
  av_frame_free(&dec_frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&in_fmt);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("Done.\n");
  return 0;
}