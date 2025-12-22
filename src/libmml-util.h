/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/

#ifndef __LIBMML_UTIL_H__
#define __LIBMML_UTIL_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdlib.h>
#include <libavcodec/avcodec.h>

void 
mml_color_rgb2yuv(uint8_t r, 
                  uint8_t g, 
                  uint8_t b, 
                  uint8_t* y, 
                  uint8_t* u, 
                  uint8_t* v);

void 
mml_info_timebase(AVRational tb);

/*！
** @brief 计算点 (px, py) 到线段 (x1, y1)-(x2, y2) 的最短距离的平方。
** 
** 使用距离平方是为了避免开根号运算 (sqrt)，从而提高性能。
*/
float 
mml_segment_dist(int px, int py, int x1, int y1, int x2, int y2);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_UTIL_H__