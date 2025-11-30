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