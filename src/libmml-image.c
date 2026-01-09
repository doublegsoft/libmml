/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "libmml-image.h"
#include "libmml-error.h"

// uint8_t* 
// mml_image_load(const char* filename, int target_w, int target_h) 
// {
//   int w, h, c;
//   unsigned char* raw_data = stbi_load(filename, &w, &h, &c, 3);
//   if (!raw_data) return NULL;

//   // 分配目标内存 (RGB24 = 3 bytes per pixel)
//   uint8_t* resized_data = (uint8_t*)malloc(target_w * target_h * 3);

//   // 使用 sws_scale 进行简单的缩放 (RGB -> RGB)
//   struct SwsContext* sws = sws_getContext(w, h, AV_PIX_FMT_RGB24,
//                                           target_w, target_h, AV_PIX_FMT_RGB24,
//                                           SWS_BILINEAR, NULL, NULL, NULL);
  
//   const uint8_t* srcSlice[] = { raw_data };
//   int srcStride[] = { w * 3 };
//   uint8_t* dstSlice[] = { resized_data };
//   int dstStride[] = { target_w * 3 };

//   sws_scale(sws, srcSlice, srcStride, 0, h, dstSlice, dstStride);

//   sws_freeContext(sws);
//   stbi_image_free(raw_data);
//   return resized_data;
// }

int 
mml_image_frame(const char*  filename, 
                AVFrame**    out_frame)
{            
  AVFormatContext* fmt_ctx = NULL;
  AVCodecContext* dec_ctx = NULL;
  const AVCodec* dec = NULL;
  AVPacket* pkt = NULL;
  AVFrame* frame = NULL;
  int ret = -1;
  int stream_idx = -1;

  if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open file: %s\n", filename);
    return -1;
  }

  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    fprintf(stderr, "Could not find stream info\n");
    goto cleanup;
  }

  stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
  if (stream_idx < 0) {
    fprintf(stderr, "No video stream found in the image file\n");
    goto cleanup;
  }

  dec_ctx = avcodec_alloc_context3(dec);
  if (!dec_ctx) goto cleanup;

  avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[stream_idx]->codecpar);

  // Open the decoder
  if (avcodec_open2(dec_ctx, dec, NULL) < 0) {
    fprintf(stderr, "Failed to open decoder\n");
    goto cleanup;
  }

  pkt = av_packet_alloc();
  frame = av_frame_alloc();

  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == stream_idx) {
      
      // Send raw compressed data (Packet) to the decoder
      ret = avcodec_send_packet(dec_ctx, pkt);
      if (ret < 0) {
        fprintf(stderr, "Error sending packet to decoder\n");
        break;
      }

      // Receive raw pixel data (Frame) from the decoder
      ret = avcodec_receive_frame(dec_ctx, frame);
      if (ret == 0) {
        *out_frame = av_frame_alloc();
        (*out_frame)->format = AV_PIX_FMT_YUV420P;
        (*out_frame)->width = frame->width;
        (*out_frame)->height = frame->height;
        av_frame_get_buffer(*out_frame, 32);

        struct SwsContext* sws = sws_getContext(
          frame->width, frame->height, frame->format,
          frame->width, frame->height, AV_PIX_FMT_YUV420P,
          SWS_BILINEAR, NULL, NULL, NULL
        );
        
        sws_scale(sws, (const uint8_t* const*)frame->data, frame->linesize,
                  0, frame->height, (*out_frame)->data, (*out_frame)->linesize);
        
        sws_freeContext(sws);
        ret = 0;
        break;
      } else if (ret == AVERROR(EAGAIN)) {
        continue;
      } else {
        fprintf(stderr, "Error receiving frame\n");
        goto cleanup_packet;
      }
    }
    av_packet_unref(pkt);
  }

cleanup_packet:
  av_packet_unref(pkt);

cleanup:
  if (pkt) av_packet_free(&pkt);
  if (frame) av_frame_free(&frame); 
  
  if (dec_ctx) avcodec_free_context(&dec_ctx);
  if (fmt_ctx) avformat_close_input(&fmt_ctx);

  return ret;
}  


int 
mml_image_load(const char*          filename, 
               AVFrame**            out_frame,
               const char*          out_path,
               AVFormatContext**    ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream)
{
  int ret = MML_SUCCESS;
  mml_image_frame(filename, out_frame);

  avformat_alloc_output_context2(ofmt_ctx, NULL, NULL, out_path);
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  *enc_ctx = avcodec_alloc_context3(enc);
  (*enc_ctx)->width = (*out_frame)->width;
  (*enc_ctx)->height = (*out_frame)->height;
  (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;
  (*enc_ctx)->time_base = (AVRational){1, 30};
  (*enc_ctx)->framerate = (AVRational){30, 1};
  if ((*ofmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER) 
    (*enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  avcodec_open2(*enc_ctx, enc, NULL);
  *out_stream = avformat_new_stream(*ofmt_ctx, NULL);
  avcodec_parameters_from_context((*out_stream)->codecpar, *enc_ctx);
  (*out_stream)->time_base = (*enc_ctx)->time_base;
  avio_open(&(*ofmt_ctx)->pb, out_path, AVIO_FLAG_WRITE);
  // avformat_write_header(*ofmt_ctx, NULL);
  return ret;
}
