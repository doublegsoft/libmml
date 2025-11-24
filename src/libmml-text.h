/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_TEXT_H__
#define __LIBMML_TEXT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

typedef struct mml_fontctx_s mml_fontctx_t;

mml_fontctx_t*  
mml_font_init(const char* filename);

void
mml_font_free(mml_fontctx_t* ctx);

/*!
** 使用自定义 TTF 字体在 YUV420P 视频帧上绘制文本。
** 支持抗锯齿 (Alpha Blending) 以获得平滑的边缘效果。
**
** @param frame      目标视频帧 (必须是 YUV420P 格式)
** @param ctx        包含已加载字体信息的上下文
** @param text       要绘制的字符串内容
** @param start_x    文本起始 X 坐标
** @param start_y    文本起始 Y 坐标 (文本的顶部边界)
** @param font_size  字体大小 (像素高度)
** @param r, g, b    文本颜色 (RGB 0-255)
*/
int mml_text_ltr(AVFrame* frame, 
                 mml_fontctx_t* ctx, 
                 const char* text, 
                 int start_x, 
                 int start_y, 
                 float font_size, 
                 uint8_t r, 
                 uint8_t g, 
                 uint8_t b);

#ifdef __cplusplus
}
#endif  

#endif // __LIBMML_TEXT_H__