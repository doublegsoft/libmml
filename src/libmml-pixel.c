/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include "libmml-pixel.h"

void mml_pixel_yuv(AVFrame* frame, 
                   int x, 
                   int y, 
                   uint8_t y_val, 
                   uint8_t u_val, 
                   uint8_t v_val)
{
  if (x >= 0 && x < frame->width && y >= 0 && y < frame->height) 
  {
    frame->data[0][y * frame->linesize[0] + x] = y_val;
  }

  // Draw Chroma (U/V) - only update for even coordinates to avoid overdraw
  if (x % 2 == 0 && y % 2 == 0) 
  {
    int uv_x = x / 2;
    int uv_y = y / 2;
    if (uv_x >= 0 && uv_x < frame->width / 2 && uv_y >= 0 && uv_y < frame->height / 2) 
    {
      frame->data[1][uv_y * frame->linesize[1] + uv_x] = u_val;
      frame->data[2][uv_y * frame->linesize[2] + uv_x] = v_val;
    }
  }
}  