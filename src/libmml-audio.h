/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/

#ifndef __LIBMML_AUDIO_H__
#define __LIBMML_AUDIO_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

/*!
** @brief Initializes the Audio Pipeline (Demuxer -> Decoder -> Encoder -> Muxer).
**
** This function opens an input audio file, sets up the decoder, and optionally 
** configures an AAC encoder if an output context is provided.
**
** @param filepath   [In] Path to the source audio file.
** @param ifmt_ctx   [Out] Pointer to the Input Format Context (Demuxer).
** @param dec_ctx    [Out] Pointer to the Decoder Context.
** @param audio_idx  [Out] Index of the selected audio stream in the input.
** @param ofmt_ctx   [In] Output Format Context (Muxer). If NULL, only the decoder is set up.
** @param enc_ctx    [Out] Pointer to the Encoder Context.
** @param out_stream [Out] Pointer to the Output Stream.
** @return int       MML_SUCCESS (0) on success.
*/
int
mml_audio_load(const char*          filepath,
               AVFormatContext**    ifmt_ctx,
               AVCodecContext**     dec_ctx,
               int*                 video_idx,
               AVFormatContext*     ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_AUDIO_H__