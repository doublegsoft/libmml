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

void 
mml_util_rgb2yuv(uint8_t r, 
                 uint8_t g, 
                 uint8_t b, 
                 uint8_t* y, 
                 uint8_t* u, 
                 uint8_t* v);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_UTIL_H__