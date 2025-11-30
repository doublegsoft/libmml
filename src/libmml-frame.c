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

int 
mml_frame_save(AVFrame* frame, const char* filename) 
{
  int ret = MML_SUCCESS;
  
  // 1. Find the MJPEG Encoder
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
  if (!codec) 
  {
    mml_error_set(-1, "mjpeg codec not found");
    return -1;
  }

  AVCodecContext* c = avcodec_alloc_context3(codec);
  if (!c) return -1;

  c->width = frame->width;
  c->height = frame->height;
  c->pix_fmt = AV_PIX_FMT_YUVJ420P; // "J" means JPEG color range (0-255)
  c->time_base = (AVRational){1, 25}; // Arbitrary for a single image

  if ((ret = avcodec_open2(c, codec, NULL)) < 0) 
  {
    mml_error_set(ret,  "could not open codec");
    avcodec_free_context(&c);
    return ret;
  }

  AVPacket* pkt = av_packet_alloc();
  
  ret = avcodec_send_frame(c, frame);
  if (ret < 0) 
  {
    mml_error_set(ret,  "error sending frame to encoder");
    goto cleanup;
  }

  // Receive the compressed packet (the JPEG data)
  ret = avcodec_receive_packet(c, pkt);
  if (ret == 0) {
    // 5. Write to file
    FILE* f = fopen(filename, "wb");
    if (f) 
    {
      fwrite(pkt->data, 1, pkt->size, f);
      fclose(f);
    } 
    else 
      mml_error_set(ret, "could not open %s for writing", filename);
    av_packet_unref(pkt);
  } 
  else 
    mml_error_set(ret, "error encoding frame", filename);

cleanup:
  av_packet_free(&pkt);
  avcodec_free_context(&c);
  return ret;
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