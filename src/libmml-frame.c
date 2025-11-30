/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>

#include "libmml-internal.h"
#include "libmml-error.h"
#include "libmml-frame.h"

/*!
** Saves a frame as an image under the given output path.
*/
int
mml_frame_save_image(const mml_encoder_p    encoder, 
                     const AVFrame*         orig_frame,
                     AVFrame*               rgb_frame,
                     const char*            output_path)
{
  const AVCodec* codec = encoder->enc;
  AVCodecContext* c = encoder->ctx;
  AVPacket* pkt = encoder->pkt;
  
  c->bit_rate = 400000;
  c->width = orig_frame->width;
  c->height = orig_frame->height;
  c->pix_fmt = AV_PIX_FMT_RGB24;
  c->time_base = (AVRational){1, 25};

  if (avcodec_open2(c, codec, NULL) < 0)
    return MML_ERROR_CODEC_OPEN_FAILED;
  
  rgb_frame->format = AV_PIX_FMT_RGB24;
  rgb_frame->width = c->width;
  rgb_frame->height = c->height;
  av_frame_get_buffer(rgb_frame, 0);
  
  struct SwsContext* sws = sws_getContext(
    orig_frame->width, orig_frame->height, orig_frame->format,
    c->width, c->height, AV_PIX_FMT_RGB24,
    SWS_BILINEAR, NULL, NULL, NULL
  );
  sws_scale(sws,
    (const uint8_t * const *)orig_frame->data, orig_frame->linesize,
    0, orig_frame->height,
    rgb_frame->data, rgb_frame->linesize);
  sws_freeContext(sws);
  
  // Send frame to encoder
  if (avcodec_send_frame(c, rgb_frame) < 0) return -1;
  if (avcodec_receive_packet(c, pkt) < 0) return -1;
  
  FILE* f = fopen(output_path, "wb");
  if (!f) 
    return MML_ERROR_FILE_OPEN_FAILED;
  fwrite(pkt->data, 1, pkt->size, f);
  fclose(f);
  
  return MML_SUCCESS;
}

// int
// mml_frame_encode(const mml_encoder_p encoder, 
//                  const AVFormatContext* format, 
//                  int stream_index,
//                  AVFrame* frame)
// {
//   frame->pict_type = AV_PICTURE_TYPE_NONE;
//   int ret = avcodec_send_frame(encoder->ctx, frame);
//   while(ret >= 0) 
//   {
//     av_packet_unref(encoder->pkt);
//     ret = avcodec_receive_packet(encoder->ctx, encoder->pkt);

//     if(ret == AVERROR(EAGAIN)) 
//       break;
//     else if (ret == AVERROR_EOF) 
//       break;
//     else if(ret < 0) 
//       return -1;

//     encoder->pkt->stream_index = stream_index;
//     av_packet_rescale_ts(encoder->pkt, 
//                          encoder->ctx->time_base, 
//                          format->streams[stream_index]->time_base);

//     if (av_interleaved_write_frame((AVFormatContext*)format, encoder->pkt) != 0)
//       return -1;
//   }
//   return MML_SUCCESS;
// }

int 
mml_frame_encode(AVCodecContext* enc_ctx, 
                 AVFormatContext* fmt_ctx, 
                 AVStream* stream, 
                 AVFrame* frame, 
                 AVPacket* pkt) 
{
  int ret = avcodec_send_frame(enc_ctx, frame);
  if (ret < 0) 
    return ret;

  while (ret >= 0) 
  {
    ret = avcodec_receive_packet(enc_ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) 
      return MML_SUCCESS;
    if (ret < 0)
      return ret;

    av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
    pkt->stream_index = stream->index;
    av_interleaved_write_frame(fmt_ctx, pkt);
    av_packet_unref(pkt);
  }
  return MML_SUCCESS;
}

int 
mml_frame_write(AVCodecContext* enc_ctx, 
                AVFormatContext* ofmt_ctx, 
                AVStream* out_stream,
                AVFrame* frame)
{
  int ret;

  ret = avcodec_send_frame(enc_ctx, frame);
  if (ret < 0) return ret;

  while (ret >= 0) 
  {
    AVPacket* pkt = av_packet_alloc();
    ret = avcodec_receive_packet(enc_ctx, pkt);
    
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      av_packet_free(&pkt);
      break;
    } else if (ret < 0) {
      av_packet_free(&pkt);
      return ret;
    }

    // rescale timestamps for output (encoder time base -> stream time base)
    av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
    pkt->stream_index = out_stream->index;

    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    av_packet_free(&pkt);
    
    if (ret < 0) return ret;
  }
  return MML_SUCCESS;
}