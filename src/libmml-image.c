/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "libmml-image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

uint8_t* 
mml_image_load(const char* filename, int target_w, int target_h) 
{
  int w, h, c;
  unsigned char* raw_data = stbi_load(filename, &w, &h, &c, 3);
  if (!raw_data) return NULL;

  // 分配目标内存 (RGB24 = 3 bytes per pixel)
  uint8_t* resized_data = (uint8_t*)malloc(target_w * target_h * 3);

  // 使用 sws_scale 进行简单的缩放 (RGB -> RGB)
  struct SwsContext* sws = sws_getContext(w, h, AV_PIX_FMT_RGB24,
                                          target_w, target_h, AV_PIX_FMT_RGB24,
                                          SWS_BILINEAR, NULL, NULL, NULL);
  
  const uint8_t* srcSlice[] = { raw_data };
  int srcStride[] = { w * 3 };
  uint8_t* dstSlice[] = { resized_data };
  int dstStride[] = { target_w * 3 };

  sws_scale(sws, srcSlice, srcStride, 0, h, dstSlice, dstStride);

  sws_freeContext(sws);
  stbi_image_free(raw_data);
  return resized_data;
}