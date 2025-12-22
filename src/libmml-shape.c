/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <math.h>

#include <libavutil/frame.h>

#include "libmml-util.h"
#include "libmml-shape.h"
#include "libmml-pixel.h"

static float 
dist_sq_to_segment(int px, int py, int vx, int vy, int wx, int wy) 
{
  float l2 = (float)((vx - wx) * (vx - wx) + (vy - wy) * (vy - wy));
  
  if (l2 == 0.0f) {
    return (float)((px - vx) * (px - vx) + (py - vy) * (py - vy));
  }

  float t = ((px - vx) * (wx - vx) + (py - vy) * (wy - vy)) / l2;

  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  float proj_x = vx + t * (wx - vx);
  float proj_y = vy + t * (wy - vy);

  return ((px - proj_x) * (px - proj_x) + (py - proj_y) * (py - proj_y));
}

static int 
is_point_in_quad(int px, int py, int* vx, int* vy) 
{
  int inside = 0;
  int j = 3; // Edge from vertex 3 to 0

  for (int i = 0; i < 4; i++) {
    // Check if ray crosses edge (vx[i], vy[i]) -> (vx[j], vy[j])
    if (((vy[i] > py) != (vy[j] > py)) &&
        (px < (vx[j] - vx[i]) * (float)(py - vy[i]) / (vy[j] - vy[i]) + vx[i])) 
    {
      inside = !inside;
    }
    j = i;
  }
  return inside;
}

void 
mml_shape_ellipse(AVFrame* frame, 
                  int cx, 
                  int cy, 
                  int rx, 
                  int ry, 
                  int thickness, 
                  uint8_t y_val, 
                  uint8_t u_val, 
                  uint8_t v_val,
                  float alpha) 
{
  int w = frame->width;
  int h = frame->height;

  // 1. 确定包围盒 (Bounding Box) 以减少循环次数
  int start_x = cx - rx;
  int end_x   = cx + rx;
  int start_y = cy - ry;
  int end_y   = cy + ry;

  // 2. 边界裁剪 (防止内存越界)
  if (start_x < 0) start_x = 0;
  if (end_x >= w)  end_x = w - 1;
  if (start_y < 0) start_y = 0;
  if (end_y >= h)  end_y = h - 1;

  // 3. 预计算内半径 (防止负数)
  float inner_rx = (float)(rx - thickness);
  float inner_ry = (float)(ry - thickness);
  if (inner_rx < 0) inner_rx = 0;
  if (inner_ry < 0) inner_ry = 0;

  // 预计算平方值，避免循环内重复乘法
  float rx2 = (float)rx * rx;
  float ry2 = (float)ry * ry;
  float irx2 = inner_rx * inner_rx;
  float iry2 = inner_ry * inner_ry;

  // 4. 遍历包围盒内的像素
  for (int y = start_y; y <= end_y; y++) {
    for (int x = start_x; x <= end_x; x++) {
      
      // 将坐标平移到圆心
      float dx = (float)(x - cx);
      float dy = (float)(y - cy);
      float dx2 = dx * dx;
      float dy2 = dy * dy;

      // 判断 1: 是否在外椭圆内?
      // Equation: x^2/a^2 + y^2/b^2 <= 1
      float outer_dist = (dx2 / rx2) + (dy2 / ry2);

      if (outer_dist <= 1.0f) {
        
        // 判断 2: 是否在内椭圆外? (或者内半径为0表示实心)
        // 如果 inner_rx <= 0，说明厚度超过了半径，画实心
        int is_border = 0;
        
        if (inner_rx > 0 && inner_ry > 0) {
           float inner_dist = (dx2 / irx2) + (dy2 / iry2);
           if (inner_dist > 1.0f) {
             is_border = 1;
           }
        } else {
           // 实心模式
           is_border = 1; 
        }

        // 如果是边框区域，则着色
        if (is_border) {
          // 设置 Y (亮度)
          frame->data[0][y * frame->linesize[0] + x] = y_val;

          // 设置 U, V (色度) - 仅在偶数坐标设置
          if (y % 2 == 0 && x % 2 == 0) {
            int uv_x = x / 2;
            int uv_y = y / 2;
            frame->data[1][uv_y * frame->linesize[1] + uv_x] = u_val;
            frame->data[2][uv_y * frame->linesize[2] + uv_x] = v_val;
          }
        }
      }
    }
  }
  int min_x = FFMAX(0, cx - rx);
  int max_x = FFMIN(frame->width, cx + rx);
  int min_y = FFMAX(0, cy - ry);
  int max_y = FFMIN(frame->height, cy + ry);

  for (int y = min_y; y < max_y; y++) {
    for (int x = min_x; x < max_x; x++) {
      // Ellipse equation check
      double val = pow((double)(x - cx) / rx, 2) + pow((double)(y - cy) / ry, 2);
      if (val <= 1.0) {
        mml_pixel_yuv(frame, x, y, y_val, u_val, v_val, alpha);
      }
    }
  }
}

void 
mml_shape_quad(AVFrame* frame, 
               int x1, int y1, 
               int x2, int y2, 
               int x3, int y3, 
               int x4, int y4,
               int thickness, 
               uint8_t y_val, 
               uint8_t u_val, 
               uint8_t v_val,
               float alpha) 
{
  int fill_mode = (thickness < 0);
  int pad = fill_mode ? 0 : (thickness + 2); // Expand bounds for thick borders

  int vx[4] = {x1, x2, x3, x4};
  int vy[4] = {y1, y2, y3, y4};

  int min_x = x1, max_x = x1;
  int min_y = y1, max_y = y1;

  for (int i = 1; i < 4; i++) {
    if (vx[i] < min_x) min_x = vx[i];
    if (vx[i] > max_x) max_x = vx[i];
    if (vy[i] < min_y) min_y = vy[i];
    if (vy[i] > max_y) max_y = vy[i];
  }

  // Expand and Clamp
  min_x -= pad; if (min_x < 0) min_x = 0;
  min_y -= pad; if (min_y < 0) min_y = 0;
  max_x += pad; if (max_x >= frame->width) max_x = frame->width;
  max_y += pad; if (max_y >= frame->height) max_y = frame->height;

  // Pre-calc radius for outline mode
  float radius_sq = 0;
  if (!fill_mode) {
    radius_sq = (thickness * 0.5f) * (thickness * 0.5f);
  }

  for (int y = min_y; y < max_y; y++) {
    for (int x = min_x; x < max_x; x++) {
      
      int draw = 0;
      float final_alpha = alpha;

      // --- LOGIC SWITCH ---
      if (fill_mode) {
        // [Fill Mode] Ray Casting Algorithm
        if (is_point_in_quad(x, y, vx, vy)) {
          draw = 1;
        }
      } 
      else {
        // [Outline Mode] Distance Field Algorithm
        float min_dist_sq = 999999.0f;
        for (int i = 0; i < 4; i++) {
          int next = (i + 1) % 4;
          float d = dist_sq_to_segment(x, y, vx[i], vy[i], vx[next], vy[next]);
          if (d < min_dist_sq) min_dist_sq = d;
        }

        if (min_dist_sq <= radius_sq) {
          draw = 1;
          // Simple Anti-aliasing for outline
          if (min_dist_sq > radius_sq - 2.0f) final_alpha *= 0.5f;
        }
      }

      if (draw) {
        mml_pixel_yuv(frame, x, y, y_val, u_val, v_val, final_alpha);
      }
    }
  }
}

/**
 * @brief 绘制带光晕效果的椭圆环 (仿足球转播战术板效果)。
 *
 * @param frame      目标帧。
 * @param cx, cy     中心坐标 (球员脚下)。
 * @param rx, ry     X轴和Y轴半径。通常 ry = 0.3 * rx 以符合透视。
 * @param thickness  光晕宽度控制 (建议 0.1 - 0.3，值越大越粗/越虚)。
 * @param y, u, v    颜色。
 * @param max_alpha  最大不透明度 (0.0 - 1.0)。建议 0.8。
 */
void 
mml_shape_ring(AVFrame* frame, 
               int cx, int cy, 
               int rx, int ry, 
               float thickness, 
               uint8_t col_y, uint8_t col_u, uint8_t col_v, 
               float max_alpha) 
{
  if (rx <= 0 || ry <= 0) return;

  // 1. 预计算倒数平方，避免循环中做除法
  float rx2_inv = 1.0f / (float)(rx * rx);
  float ry2_inv = 1.0f / (float)(ry * ry);

  // 2. 确定包围盒 (Bounding Box)
  // 稍微扩大范围以容纳光晕的边缘
  int margin = (int)(rx * thickness * 2.0f) + 5; 
  
  int min_x = cx - rx - margin;
  int max_x = cx + rx + margin;
  int min_y = cy - ry - margin;
  int max_y = cy + ry + margin;

  // 边界裁剪
  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  if (max_x >= frame->width) max_x = frame->width;
  if (max_y >= frame->height) max_y = frame->height;

  // 3. 扫描并绘制
  for (int y = min_y; y < max_y; y++) {
    
    // 优化：预计算 Y 部分
    float dy = (float)(y - cy);
    float term_y = (dy * dy) * ry2_inv;

    for (int x = min_x; x < max_x; x++) {
      float dx = (float)(x - cx);
      
      // 椭圆方程值: V = x^2/a^2 + y^2/b^2
      // V = 1.0 时在线上; V < 1 在内部; V > 1 在外部
      float val = (dx * dx) * rx2_inv + term_y;

      // 计算距离 "1.0" (轮廓线) 的偏差
      // 我们希望在 1.0 附近产生高 Alpha 值
      float diff = fabsf(val - 1.0f);

      // 阈值判断：如果离轮廓太远，就不画，节省性能
      if (diff > thickness) continue;

      // --- 核心：光晕计算 ---
      // 使用高斯分布公式或简单的线性衰减
      // 这里使用余弦衰减，能够产生非常平滑的类似激光的边缘
      // 将 diff 映射到 0..PI/2
      float ratio = diff / thickness; // 0.0 (中心) -> 1.0 (边缘)
      
      // 衰减函数: cos(ratio * PI/2) ^ 2
      // 这种曲线比线性 (1-ratio) 看起来更有“光感”
      float glow = cosf(ratio * 1.570796f);
      glow = glow * glow; 

      float final_alpha = max_alpha * glow;

      // 绘制像素
      mml_pixel_yuv(frame, x, y, col_y, col_u, col_v, final_alpha);
    }
  }
} 

void 
mml_shape_line(AVFrame* frame, 
               int x1, int y1, int x2, int y2, 
               int thickness, 
               uint8_t y_val, uint8_t u_val, uint8_t v_val, 
               float alpha) 
{
  // 1. Determine Bounding Box (Optimization)
  int min_x = (x1 < x2 ? x1 : x2) - thickness;
  int max_x = (x1 > x2 ? x1 : x2) + thickness;
  int min_y = (y1 < y2 ? y1 : y2) - thickness;
  int max_y = (y1 > y2 ? y1 : y2) + thickness;

  // Clamp
  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  if (max_x >= frame->width) max_x = frame->width;
  if (max_y >= frame->height) max_y = frame->height;

  float radius_sq = (thickness * 0.5f) * (thickness * 0.5f);

  // 2. Iterate pixels
  for (int y = min_y; y < max_y; y++) {
    for (int x = min_x; x < max_x; x++) {
      
      // Calculate distance to the line segment
      float d_sq = mml_segment_dist(x, y, x1, y1, x2, y2);

      if (d_sq <= radius_sq) {
        float draw_alpha = alpha;

        // Anti-aliasing (Soft Edges)
        // If distance is within the last 1.0 pixel of the radius, fade out.
        if (d_sq > radius_sq - 2.0f) {
          draw_alpha *= 0.5f; 
        }

        mml_pixel_yuv(frame, x, y, y_val, u_val, v_val, draw_alpha);
      }
    }
  }
}