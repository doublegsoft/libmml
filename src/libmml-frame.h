/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_FRAME_H__
#define __LIBMML_FRAME_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

/*!
** @brief Encodes a single AVFrame into a JPEG image and saves it to a file.
**
** This function creates a temporary MJPEG encoder, pushes the raw frame into it,
** retrieves the encoded packet, and writes the bytes to disk.
**
** @note The input frame->format MUST support JPEG encoding (usually AV_PIX_FMT_YUVJ420P).
**       If your video is standard AV_PIX_FMT_YUV420P, the encoder usually handles it,
**       but strictly speaking, JPEGs use the full 0-255 color range (YUVJ).
**
** @param frame    Pointer to the raw (decoded) AVFrame to be saved.
** @param filename Target file path (e.g., "snapshot.jpg").
** @return int     0 on success, or a negative AVERROR code on failure.
*/
 int 
 mml_frame_save(AVFrame* frame, const char* filename);

/*!
** 将原始 AVFrame 编码并写入输出文件
** 
** @param enc_ctx    编码器上下文 (存储编码参数)
** @param ofmt_ctx   输出格式上下文 (管理输出文件)
** @param out_stream 输出流 (用于获取时间基和索引)
** @param frame      要编码的原始帧 (如果为 NULL，则表示刷新编码器，输出剩余缓存数据)
** @return 0 表示成功，负数表示错误
*/
int 
mml_frame_write(AVCodecContext* enc_ctx, 
                AVFormatContext* ofmt_ctx, 
                AVStream* out_stream,
                AVFrame* frame);

#ifdef __cplusplus
}
#endif                 

#endif // __LIBMML_FRAME_H__                 