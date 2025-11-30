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

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_UTIL_H__