/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/

#include "libmml-error.h"
#include "libmml-audio.h"

int
mml_audio_load(const char*          filepath,
               AVFormatContext**    ifmt_ctx,
               AVCodecContext**     dec_ctx,
               int*                 audio_idx,
               AVFormatContext*     ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream)
{
  avformat_open_input(ifmt_ctx, filepath, NULL, NULL);
  avformat_find_stream_info(*ifmt_ctx, NULL);
  *audio_idx = av_find_best_stream(*ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

  const AVCodec* a_dec = avcodec_find_decoder((*ifmt_ctx)->streams[*audio_idx]->codecpar->codec_id);
  *dec_ctx = avcodec_alloc_context3(a_dec);
  avcodec_parameters_to_context(*dec_ctx, (*ifmt_ctx)->streams[*audio_idx]->codecpar);
  avcodec_open2(*dec_ctx, a_dec, NULL);

  if (ofmt_ctx == NULL)
    return MML_SUCCESS;

  const AVCodec* a_enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
  *enc_ctx = avcodec_alloc_context3(a_enc);
  (*enc_ctx)->sample_rate = 44100;
  (*enc_ctx)->sample_fmt = AV_SAMPLE_FMT_FLTP;
  (*enc_ctx)->bit_rate = 128000;
  av_channel_layout_default(&(*enc_ctx)->ch_layout, 2);
  (*enc_ctx)->time_base = (AVRational){1, (*enc_ctx)->sample_rate};
  if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) (*enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  avcodec_open2((*enc_ctx), a_enc, NULL);
  *out_stream = avformat_new_stream(ofmt_ctx, NULL);
  avcodec_parameters_from_context((*out_stream)->codecpar, (*enc_ctx));
  (*out_stream)->time_base = (*enc_ctx)->time_base;  
  return MML_SUCCESS;
}