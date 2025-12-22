/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <stdlib.h>

#include "libmml-util.h"

#define CLAMP(v) ((v) < 0 ? 0 : ((v) > 255 ? 255 : (v)))

/**
 * Converts a single RGB pixel to YUV using integer arithmetic.
 * Style: Pointers left, 2-space indent.
 */
void 
mml_color_rgb2yuv(uint8_t r, 
                  uint8_t g, 
                  uint8_t b, 
                  uint8_t* y, 
                  uint8_t* u, 
                  uint8_t* v) 
{
  // Integer approximation coefficients (scaled by 2^8 = 256)
  int y_tmp = (77 * r + 150 * g + 29 * b + 128) >> 8;
  int u_tmp = (-43 * r - 84 * g + 127 * b + 128) >> 8;
  int v_tmp = (127 * r - 106 * g - 21 * b + 128) >> 8;

  // Offset U and V
  u_tmp += 128;
  v_tmp += 128;

  *y = (uint8_t)CLAMP(y_tmp);
  *u = (uint8_t)CLAMP(u_tmp);
  *v = (uint8_t)CLAMP(v_tmp);
}

void 
mml_info_timebase(AVRational tb) 
{
  printf("Raw Fraction:  %d/%d\n", tb.num, tb.den);
  if (tb.den != 0) {
    printf("Decimal Value: %.10f seconds per tick\n", av_q2d(tb));
    printf("Inverse (Hz):  %.2f Hz\n", 1.0 / av_q2d(tb));
  }
}


float 
mml_segment_dist(int px, int py, int x1, int y1, int x2, int y2) 
{
  float l2 = (float)((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
  
  if (l2 == 0.0f) return (float)((px - x1) * (px - x1) + (py - y1) * (py - y1));

  // 计算投影因子 t (Projection t)
  // 公式推导：t = (向量AP · 向量AB) / |AB|^2
  // t 代表点 P 在直线 AB 上的投影位置：
  //   t = 0 表示投影点在 A
  //   t = 1 表示投影点在 B
  //   0 < t < 1 表示投影点在线段 AB 内部
  float t = ((px - x1) * (x2 - x1) + (py - y1) * (y2 - y1)) / l2;
  
  // 将 t 限制在 [0, 1] 范围内 (Clamp)
  // 如果投影点在线段外侧，则最近的点是端点 A 或 B
  if (t < 0.0f) t = 0.0f;      // 垂足在 A 外侧，最近点是 A
  else if (t > 1.0f) t = 1.0f; // 垂足在 B 外侧，最近点是 B

  // 计算线段上最近点的坐标
  // 坐标 = A + t * (B - A)
  float proj_x = x1 + t * (x2 - x1);
  float proj_y = y1 + t * (y2 - y1);

  // 返回点 P 到该最近点的距离平方
  return ((px - proj_x) * (px - proj_x) + (py - proj_y) * (py - proj_y));
}