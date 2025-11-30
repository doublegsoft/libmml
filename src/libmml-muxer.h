/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_MUXER_H__
#define __LIBMML_MUXER_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "libmml-error.h"

typedef struct mml_muxerctx_s mml_muxerctx_t;

/*!
** 初始化封装器，加入mp3文件的音频。
*/
int
mml_muxer_mp3(mml_muxerctx_t* ctx, const char* filepath);

/*!
** 初始化封装器，新建一个视频处理的流。
*/
int 
mml_muxer_mp4(mml_muxerctx_t* ctx, int width, int height);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_MUXER_H__