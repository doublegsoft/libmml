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

/*!
** @brief Parses a time string "HH:MM:SS[.msec]" into total seconds.
** 
** Examples:
** "00:01:30"     -> 90.0
** "01:00:00.5"   -> 3600.5
** "45"           -> 45.0 (Fallback)
**
** @param time_str String containing the time.
** @return double  Total seconds, or -1.0 on failure.
*/
double 
mml_time_parse(const char* time_str);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_UTIL_H__