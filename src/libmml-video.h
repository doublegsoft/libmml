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

/*!
** @brief Initializes the full transcoding pipeline: Input -> Decoder -> Encoder -> Output.
**
** This function opens an input video file, sets up the decoder, and optionally
** sets up an H.264 encoder and output file context if an output path is provided.
**
** @param filepath    Path to the source video file.
** @param ifmt_ctx    [Out] Pointer to the Input Format Context (Container).
** @param dec_ctx     [Out] Pointer to the Decoder Context.
** @param outpath     Path to the destination file (pass NULL to only load input).
** @param ofmt_ctx    [Out] Pointer to the Output Format Context (Muxer).
** @param enc_ctx     [Out] Pointer to the Encoder Context.
** @param out_stream  [Out] Pointer to the Output Stream.
** @param video_idx   [Out] Index of the best video stream in the input file.
**
** @return int        MML_SUCCESS (0) on success, or 1 on failure.
*/
int
mml_video_load(const char*          filepath,
               AVFormatContext**    ifmt_ctx,
               AVCodecContext**     dec_ctx,
               int*                 video_idx,
               const char*          outpath,
               AVFormatContext**    ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream);

#ifdef __cplusplus
}
#endif

#endif