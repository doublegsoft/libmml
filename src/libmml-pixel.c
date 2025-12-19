/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include "libmml-pixel.h"

void 
mml_pixel_yuv(AVFrame* frame, 
              int x, 
              int y, 
              uint8_t y_val, 
              uint8_t u_val, 
              uint8_t v_val,
              float alpha)
  {
  if (x < 0 || x >= frame->width || y < 0 || y >= frame->height) return;

  int a_scale = (int)(alpha * 256);
  
  // Optimization: If fully transparent, do nothing
  if (a_scale <= 0) return;
  // Safety clamp
  if (a_scale > 256) a_scale = 256;

  int inv_scale = 256 - a_scale;

  // Read background -> Calc Blend -> Write back
  int y_idx = y * frame->linesize[0] + x;
  uint8_t bg_y = frame->data[0][y_idx];
  
  // Formula: (Fg * alpha + Bg * (1-alpha)) / 256
  // Shift right 8 is equivalent to dividing by 256
  frame->data[0][y_idx] = (uint8_t)((y_val * a_scale + bg_y * inv_scale) >> 8);

  // Only update on even coordinates (YUV420 Subsampling)
  if (x % 2 == 0 && y % 2 == 0) 
  {
    int uv_idx = (y / 2) * frame->linesize[1] + (x / 2);
    
    // U Plane
    uint8_t bg_u = frame->data[1][uv_idx];
    frame->data[1][uv_idx] = (uint8_t)((u_val * a_scale + bg_u * inv_scale) >> 8);

    // V Plane
    uint8_t bg_v = frame->data[2][uv_idx];
    frame->data[2][uv_idx] = (uint8_t)((v_val * a_scale + bg_v * inv_scale) >> 8);
  }
}