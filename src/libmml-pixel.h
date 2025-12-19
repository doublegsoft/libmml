/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_PIXEL_H__
#define __LIBMML_PIXEL_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavutil/frame.h>

/*!
** Sets a specific pixel at (x, y) to the provided YUV values in an AVFrame.
**
** @note This function assumes the AVFrame format is YUV420P (planar YUV 4:2:0).
**       In this format, U and V planes are half the width and half the height 
**       of the Y plane.
**
** @param frame  The target FFmpeg AVFrame.
** @param x      The x-coordinate of the pixel.
** @param y      The y-coordinate of the pixel.
** @param y_val  Luma value (brightness).
** @param u_val  Chroma U value (blue projection).
** @param v_val  Chroma V value (red projection).
*/
void 
mml_pixel_yuv(AVFrame* frame, 
              int x, 
              int y, 
              uint8_t y_val, 
              uint8_t u_val, 
              uint8_t v_val,
              float alpha);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_PIXEL_H__