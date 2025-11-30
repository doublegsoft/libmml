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

#include "libmml-shape.h"
#include "libmml-pixel.h"


void 
mml_shape_ellipse_draw(AVFrame* frame, 
                       int cx, 
                       int cy, 
                       int rx, 
                       int ry, 
                       int thickness, 
                       uint8_t y_val, 
                       uint8_t u_val, 
                       uint8_t v_val) 
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
        mml_pixel_yuv(frame, x, y, y_val, u_val, v_val);
      }
    }
  }
}

void 
mml_shape_ellipse_move(AVFrame* frame, 
                       int start_cx, 
                       int start_cy,
                       int end_cx, 
                       int end_cy,  
                       int rx, 
                       int ry, 
                       int thickness, 
                       uint8_t y_val, 
                       uint8_t u_val, 
                       uint8_t v_val,
                       double seconds)
{
  // cx = cx + (int)(frame->width * sin(seconds * 1.5));
  // cy = cy + (int)(frame->height * cos(seconds * 2.0));
  // mml_shape_ellipse_draw(frame, cx, cy, rx, ry, thickness, y_val, u_val, v_val);
}