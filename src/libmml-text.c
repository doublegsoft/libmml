/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "libmml-text.h"
#include "libmml-error.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

struct mml_fontctx_s
{
  unsigned char* ttf_buffer;
  stbtt_fontinfo font_info;
};

mml_fontctx_t* 
mml_font_init(const char* filename) 
{
  mml_fontctx_t* ret = (mml_fontctx_t*)malloc(sizeof(mml_fontctx_t));
  FILE* f = fopen(filename, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  ret->ttf_buffer = (unsigned char*)malloc(size);
  fread(ret->ttf_buffer, 1, size, f);
  fclose(f);
  int rc = stbtt_InitFont(&ret->font_info, ret->ttf_buffer, stbtt_GetFontOffsetForIndex(ret->ttf_buffer, 0));
  if (!rc) 
  {
    free(ret);
    return NULL;
  }
  return ret;
}

void 
mml_font_free(mml_fontctx_t* ctx) 
{
  if (ctx->ttf_buffer) free(ctx->ttf_buffer);
}

int 
mml_text_ltr(AVFrame* frame, 
             mml_fontctx_t* ctx, 
             const char* text, 
             int start_x, 
             int start_y, 
             float 
             font_size, 
             uint8_t r, 
             uint8_t g, 
             uint8_t b) 
{
  float scale = stbtt_ScaleForPixelHeight(&ctx->font_info, font_size);

  int ascent, descent, lineGap;
  stbtt_GetFontVMetrics(&ctx->font_info, &ascent, &descent, &lineGap);
  int baseline = (int)(ascent * scale);

  int x = start_x;
  int y = start_y + baseline; 

  int Y_text =  0.257 * r + 0.504 * g + 0.098 * b + 16;
  int U_text = -0.148 * r - 0.291 * g + 0.439 * b + 128;
  int V_text =  0.439 * r - 0.368 * g - 0.071 * b + 128;

  // 4. Loop through each character
  for (int i = 0; i < strlen(text); ++i) {
    int c_width, c_height, xoff, yoff;
    
    // Get the bitmap for the current character (alpha mask)
    // This returns a malloc'd buffer that we must free later
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&ctx->font_info, 0, scale, text[i], 
                                                     &c_width, &c_height, &xoff, &yoff);
    
    if (bitmap) {
      // Iterate over the character bitmap
      for (int row = 0; row < c_height; ++row) {
        for (int col = 0; col < c_width; ++col) {
          // Calculate position in the video frame
          int draw_x = x + xoff + col;
          int draw_y = y + yoff + row;

          // Boundary check
          if (draw_x < 0 || draw_x >= frame->width || draw_y < 0 || draw_y >= frame->height)
            continue;

          // Alpha value (0-255) from the font bitmap
          uint8_t alpha_val = bitmap[row * c_width + col];
          
          if (alpha_val > 0) {
            float alpha = alpha_val / 255.0f;
            float inv_alpha = 1.0f - alpha;

            // --- BLEND Y (Luma) ---
            int y_idx = draw_y * frame->linesize[0] + draw_x;
            uint8_t bg_y = frame->data[0][y_idx];
            frame->data[0][y_idx] = (uint8_t)(bg_y * inv_alpha + Y_text * alpha);

            // --- BLEND U/V (Chroma) ---
            // YUV420P only has chroma for every 2x2 block.
            // We only write to U/V if we are on an even coordinate.
            if (draw_y % 2 == 0 && draw_x % 2 == 0) {
              int uv_x = draw_x / 2;
              int uv_y = draw_y / 2;
              
              int u_idx = uv_y * frame->linesize[1] + uv_x;
              int v_idx = uv_y * frame->linesize[2] + uv_x;

              uint8_t bg_u = frame->data[1][u_idx];
              uint8_t bg_v = frame->data[2][v_idx];

              frame->data[1][u_idx] = (uint8_t)(bg_u * inv_alpha + U_text * alpha);
              frame->data[2][v_idx] = (uint8_t)(bg_v * inv_alpha + V_text * alpha);
            }
          }
        }
      }
      stbtt_FreeBitmap(bitmap, NULL);
    }

    // Advance x position for the next character
    int advanceWidth, leftSideBearing;
    stbtt_GetCodepointHMetrics(&ctx->font_info, text[i], &advanceWidth, &leftSideBearing);
    x += (int)(advanceWidth * scale);
    
    // Simple kerning (optional)
    if (text[i+1]) {
      int kern = stbtt_GetCodepointKernAdvance(&ctx->font_info, text[i], text[i+1]);
      x += (int)(kern * scale);
    }
  }
  return MML_SUCCESS;
}