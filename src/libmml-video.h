/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_VIDEO_H__
#define __LIBMML_VIDEO_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

int
mml_video_load(const char*          filepath,
               AVFormatContext**    ifmt_ctx,
               AVCodecContext**     dec_ctx,
               const char*          outpath,
               AVFormatContext**    ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream,
               int*                 video_idx,
               double*              fps);

#ifdef __cplusplus
}
#endif

#endif