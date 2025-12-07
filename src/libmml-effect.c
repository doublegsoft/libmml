/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/

#include "libmml-effect.h"

#include <math.h>

#define CLAMP(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

void 
mml_effect_rotate(uint8_t* out, 
                  uint8_t* img_start, 
                  uint8_t* img_end, 
                  int w, 
                  int h, 
                  float t) {
  float angle = t * 2.0f * M_PI; // 旋转 0 到 360度 (2 PI)
  float cx = w / 2.0f;
  float cy = h / 2.0f;
  
  // 预计算三角函数
  float cos_a = cos(angle);
  float sin_a = sin(angle);

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // 1. 计算相对于中心的坐标
      float dx = x - cx;
      float dy = y - cy;

      // 2. 逆向旋转矩阵 (Inverse Rotation) 找原图坐标
      // x_src = dx * cos(-a) - dy * sin(-a) + cx
      // y_src = dx * sin(-a) + dy * cos(-a) + cy
      // 注意：cos(-a) = cos(a), sin(-a) = -sin(a)
      int src_x = (int)(dx * cos_a + dy * sin_a + cx);
      int src_y = (int)(-dx * sin_a + dy * cos_a + cy);

      // 3. 获取像素索引
      int out_idx = (y * w + x) * 3;
      
      // 检查旋转后的坐标是否在画面内
      int in_bounds = (src_x >= 0 && src_x < w && src_y >= 0 && src_y < h);

      if (in_bounds) {
        int src_idx = (src_y * w + src_x) * 3;
        
        // 4. 混合颜色 (Cross Fade)
        // Pixel = A * (1-t) + B * t
        // 这是一个简单的线性混合，随旋转同时进行
        float alpha = t;
        
        out[out_idx + 0] = (uint8_t)(img_start[src_idx + 0] * (1.0f - alpha) + img_end[src_idx + 0] * alpha); // R
        out[out_idx + 1] = (uint8_t)(img_start[src_idx + 1] * (1.0f - alpha) + img_end[src_idx + 1] * alpha); // G
        out[out_idx + 2] = (uint8_t)(img_start[src_idx + 2] * (1.0f - alpha) + img_end[src_idx + 2] * alpha); // B
      } else {
        // 超出边界显示黑色
        out[out_idx + 0] = 0;
        out[out_idx + 1] = 0;
        out[out_idx + 2] = 0;
      }
    }
  }
}

void 
mml_effect_fade(uint8_t* out, 
                uint8_t* img_start, 
                uint8_t* img_end, 
                int w, 
                int h, 
                float t) 
{
  float alpha = t; \
  float inv_alpha = 1.0f - t;

  int total_bytes = w * h * 3;
  for (int i = 0; i < total_bytes; i++) 
  {
    out[i] = (uint8_t)(img_start[i] * inv_alpha + img_end[i] * alpha);
  }
}

void 
mml_effect_wipe_right(uint8_t* out, uint8_t* img_start, uint8_t* img_end, int w, int h, float t) {
  // 定义羽化宽度 (例如占屏幕宽度的 20%)
  float soft_edge = 0.2f; 
  
  // 计算当前的分割线位置 (范围从 -soft_edge 到 1.0)
  float threshold = t * (1.0f + soft_edge) - soft_edge;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // 归一化 x 坐标 (0.0 ~ 1.0)
      float u = (float)x / w;
      
      // 计算混合因子 alpha
      // 当 u 远小于 threshold 时，alpha = 1 (显示 B)
      // 当 u 远大于 threshold 时，alpha = 0 (显示 A)
      // 在中间区域，alpha 平滑过渡
      float alpha = (threshold + soft_edge - u) / soft_edge;
      
      // 限制 alpha 在 0.0 ~ 1.0 之间
      if (alpha < 0.0f) alpha = 0.0f;
      if (alpha > 1.0f) alpha = 1.0f;

      int idx = (y * w + x) * 3;
      
      // 混合: 0 是 A，1 是 B
      out[idx+0] = (uint8_t)(img_start[idx+0] * (1.0f - alpha) + img_end[idx+0] * alpha);
      out[idx+1] = (uint8_t)(img_start[idx+1] * (1.0f - alpha) + img_end[idx+1] * alpha);
      out[idx+2] = (uint8_t)(img_start[idx+2] * (1.0f - alpha) + img_end[idx+2] * alpha);
    }
  }
}

void 
mml_effect_circle_open(uint8_t* out, 
                       uint8_t* img_start, 
                       uint8_t* img_end, int w, int h, float t) {
  float cx = w / 2.0f;
  float cy = h / 2.0f;
  
  // 最大半径 (勾股定理计算对角线的一半，确保能覆盖全屏)
  float max_radius = sqrt(cx * cx + cy * cy);
  
  // 当前圆的半径 (随 t 变大)
  float current_r = t * max_radius;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // 计算当前像素距离中心的距离
      float dist = sqrt(pow(x - cx, 2) + pow(y - cy, 2));
      
      int idx = (y * w + x) * 3;

      if (dist < current_r) {
        // 在圆圈内：显示 B
        out[idx+0] = img_end[idx+0];
        out[idx+1] = img_end[idx+1];
        out[idx+2] = img_end[idx+2];
      } else {
        // 在圆圈外：显示 A
        out[idx+0] = img_start[idx+0];
        out[idx+1] = img_start[idx+1];
        out[idx+2] = img_start[idx+2];
      }
      // 进阶：你也可以在这里加羽化逻辑，原理同上
    }
  }
}

void 
mml_effect_flash(uint8_t* out, uint8_t* img_start, uint8_t* img_end, int w, int h, float t) 
{
  int total_pixels = w * h;
  
  for (int i = 0; i < total_pixels; i++) {
    int idx = i * 3;
    uint8_t r, g, b;

    if (t < 0.5f) {
      // 前半段 (0.0 ~ 0.5): 图片 A -> 纯白
      // 进度 map 到 0.0 ~ 1.0
      float progress = t * 2.0f; 
      
      // 算法：插值混合 A 和 白色(255)
      r = (uint8_t)(img_start[idx+0] * (1.0f - progress) + 255 * progress);
      g = (uint8_t)(img_start[idx+1] * (1.0f - progress) + 255 * progress);
      b = (uint8_t)(img_start[idx+2] * (1.0f - progress) + 255 * progress);
    } else {
      // 后半段 (0.5 ~ 1.0): 纯白 -> 图片 B
      // 进度 map 到 0.0 ~ 1.0
      float progress = (t - 0.5f) * 2.0f;
      
      // 算法：插值混合 白色(255) 和 B
      r = (uint8_t)(255 * (1.0f - progress) + img_end[idx+0] * progress);
      g = (uint8_t)(255 * (1.0f - progress) + img_end[idx+1] * progress);
      b = (uint8_t)(255 * (1.0f - progress) + img_end[idx+2] * progress);
    }

    out[idx+0] = r;
    out[idx+1] = g;
    out[idx+2] = b;
  }
}

void 
mml_effect_pixelate(uint8_t* out, uint8_t* img_start, uint8_t* img_end, int w, int h, float t) 
{
  // 1. 计算当前的块大小 (block size)
  // t=0时块大小为1(原图)，t=0.5时块最大(如50像素)，t=1时块大小回为1
  float max_block_size = 50.0f;
  float current_block_size;

  if (t < 0.5f) {
    // A 阶段：块变大
    current_block_size = 1.0f + (t * 2.0f) * max_block_size;
  } else {
    // B 阶段：块变小
    current_block_size = 1.0f + ((1.0f - t) * 2.0f) * max_block_size;
  }
  
  int block = (int)current_block_size;
  if (block < 1) block = 1;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // 找到当前像素所属的 "马赛克块" 的左上角坐标
      int bx = (x / block) * block;
      int by = (y / block) * block;
      
      int idx = (y * w + x) * 3;
      int b_idx = (by * w + bx) * 3; // 采样块的第一个像素颜色

      // 如果 t < 0.5 显示 A 的马赛克，否则显示 B 的马赛克
      if (t < 0.5f) {
        out[idx+0] = img_start[b_idx+0];
        out[idx+1] = img_start[b_idx+1];
        out[idx+2] = img_start[b_idx+2];
      } else {
        out[idx+0] = img_end[b_idx+0];
        out[idx+1] = img_end[b_idx+1];
        out[idx+2] = img_end[b_idx+2];
      }
    }
  }
}