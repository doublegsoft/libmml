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

#include <libavutil/frame.h>

#include "libmml.h"

struct mml_encoder_s 
{
  AVPacket*             pkt;
  AVCodec*              enc;
  AVCodecContext*       ctx;
  AVFormatContext*      fmt;
};

/*
********************************************************************************
** INTERNAL FRAME FUNCTIONS
********************************************************************************
*/

/*!
** Saves a frame as an image under the given output path.
**
** @param frame
**        the frame
**
** @param output_path
**        the output image path
**
** @return success or error code    
*/
int
mml_frame_save_image(const mml_encoder_p    encoder, 
                     const AVFrame*         orig_frame,
                     AVFrame*               rgb_frame,
                     const char*            output_path);

/*!
** Encodes a frame into a packet.
**
** @param encoder
**        the encoder
*/
int
mml_frame_encode(const mml_encoder_p encoder, 
                 const AVFormatContext* format, 
                 int stream_index, 
                 AVFrame* frame); 

#ifdef __cplusplus
}
#endif                 

#endif