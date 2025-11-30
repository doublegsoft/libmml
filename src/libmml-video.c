/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>

#include "libmml-video.h"
#include "libmml-error.h"

int
mml_video_load(const char*          filepath,
               AVFormatContext**    ifmt_ctx,
               AVCodecContext**     dec_ctx,
               const char*          outpath,
               AVFormatContext**    ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream,
               int*                 video_idx,
               double*              fps)
{
  const AVCodec* decoder = NULL;
  const AVCodec* encoder = NULL;
  AVStream* in_stream = NULL;

  if (avformat_open_input(ifmt_ctx, filepath, NULL, NULL) < 0) 
  {
    mml_error_set(1, "could not open input file '%s'", filepath);
    return 1;
  }

  if (avformat_find_stream_info(*ifmt_ctx, NULL) < 0) 
  {
    mml_error_set(1, "could not find stream info");
    return 1;
  }

  *video_idx = av_find_best_stream(*ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
  if (*video_idx < 0) 
  {
    mml_error_set(1, "no video stream found in '%s'", filepath);
    return 1;
  }
  in_stream = (*ifmt_ctx)->streams[*video_idx];

  *dec_ctx = avcodec_alloc_context3(decoder);
  avcodec_parameters_to_context(*dec_ctx, in_stream->codecpar);
  
  if (avcodec_open2(*dec_ctx, decoder, NULL) < 0) 
  {
    mml_error_set(1, "failed to open decoder");
    return 1;
  }

  avformat_alloc_output_context2(ofmt_ctx, NULL, NULL, outpath);
  if (!*ofmt_ctx) 
  {
    mml_error_set(1, "Failed to allocate output context");
    return 1;
  }

  encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!encoder) 
  {
    mml_error_set(1, "H.264 encoder not found");
    return 1;
  }

  *out_stream = avformat_new_stream(*ofmt_ctx, NULL);
  *enc_ctx = avcodec_alloc_context3(encoder);

  (*enc_ctx)->height = (*dec_ctx)->height;
  (*enc_ctx)->width = (*dec_ctx)->width;
  (*enc_ctx)->sample_aspect_ratio = (*dec_ctx)->sample_aspect_ratio;
  (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;
  (*enc_ctx)->time_base = av_inv_q((*dec_ctx)->framerate); 
  (*out_stream)->time_base = (*enc_ctx)->time_base;

  // Optimization
  if ((*ofmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
    (*enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  
  av_opt_set((*enc_ctx)->priv_data, "preset", "fast", 0);

  if (avcodec_open2(*enc_ctx, encoder, NULL) < 0) {
    mml_error_set(1, "cannot open encoder");
    return 1;
  }
  
  avcodec_parameters_from_context((*out_stream)->codecpar, *enc_ctx);

  if (!((*ofmt_ctx)->oformat->flags & AVFMT_NOFILE)) 
  {
    if (avio_open(&((*ofmt_ctx)->pb), outpath, AVIO_FLAG_WRITE) < 0) 
    {
      mml_error_set(1, "could not open output file '%s'\n", outpath);
      return 1;
    }
  }

  if (avformat_write_header(*ofmt_ctx, NULL) < 0) return 1;

  *fps = av_q2d(in_stream->avg_frame_rate);
  return MML_SUCCESS;
}