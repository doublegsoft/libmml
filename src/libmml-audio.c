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

#include "libmml-audio.h"
#include "libmml-error.h"

struct mml_muxerctx_s
{
  AVFormatContext* fmt_ctx;
  
  // Video Part
  AVCodecContext* v_enc_ctx;
  AVStream* v_stream;
  int64_t next_video_pts; // Next video PTS (in Video TimeBase)
  
  // Audio Part (Input)
  AVFormatContext* a_in_fmt_ctx;
  int a_in_stream_idx;
  AVStream* a_out_stream; // Audio stream in the output file
  int64_t next_audio_pts; // Next audio PTS (in Audio TimeBase)
  int audio_finished;     // Flag: audio EOF reached
};

int mml_audio_video(MuxerContext* ctx, int width, int height) 
{
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  ctx->v_stream = avformat_new_stream(ctx->fmt_ctx, NULL);
  ctx->v_enc_ctx = avcodec_alloc_context3(codec);

  ctx->v_enc_ctx->width = VIDEO_WIDTH;
  ctx->v_enc_ctx->height = VIDEO_HEIGHT;
  ctx->v_enc_ctx->time_base = (AVRational){1, VIDEO_FPS};
  ctx->v_enc_ctx->framerate = (AVRational){VIDEO_FPS, 1};
  ctx->v_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  ctx->v_enc_ctx->gop_size = 12;
  ctx->v_enc_ctx->bit_rate = 400000;

  if (ctx->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    ctx->v_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  if (avcodec_open2(ctx->v_enc_ctx, codec, NULL) < 0) return -1;
  avcodec_parameters_from_context(ctx->v_stream->codecpar, ctx->v_enc_ctx);
  ctx->v_stream->time_base = ctx->v_enc_ctx->time_base; // 1/25
  
  ctx->next_video_pts = 0;
  return MML_SUCCESS;
}

// ------------------------------------------------------------------
// 2. Initialize Audio Input (Open MP3)
// ------------------------------------------------------------------
int mml_audio_mp3(MuxerContext* ctx, const char* mp3_filename) {
  if (avformat_open_input(&ctx->a_in_fmt_ctx, mp3_filename, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open audio file: %s\n", mp3_filename);
    return -1;
  }
  if (avformat_find_stream_info(ctx->a_in_fmt_ctx, NULL) < 0) return -1;

  // Find audio stream
  ctx->a_in_stream_idx = -1;
  for (int i = 0; i < ctx->a_in_fmt_ctx->nb_streams; i++) {
    if (ctx->a_in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      ctx->a_in_stream_idx = i;
      break;
    }
  }
  if (ctx->a_in_stream_idx == -1) return -1;

  // Create audio stream in output file
  ctx->a_out_stream = avformat_new_stream(ctx->fmt_ctx, NULL);
  AVStream* in_stream = ctx->a_in_fmt_ctx->streams[ctx->a_in_stream_idx];

  // Stream Copy parameters (Copy directly, no transcoding)
  avcodec_parameters_copy(ctx->a_out_stream->codecpar, in_stream->codecpar);
  ctx->a_out_stream->time_base = in_stream->time_base;
  
  ctx->next_audio_pts = 0;
  ctx->audio_finished = 0;
  return MML_SUCCESS;
}

// ------------------------------------------------------------------
// 3. Generate and Write one Video Frame
// ------------------------------------------------------------------
int write_video_frame(MuxerContext* ctx, AVFrame* frame) {
  // Generate simple visual pattern (color changing)
  int ret = av_frame_make_writable(frame);
  if (ret < 0) return ret;

  int i = ctx->next_video_pts;
  // Y Plane
  for (int y = 0; y < VIDEO_HEIGHT; y++) {
    for (int x = 0; x < VIDEO_WIDTH; x++) {
      frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
    }
  }
  // UV Plane (Simple gray)
  memset(frame->data[1], 128, frame->linesize[1] * VIDEO_HEIGHT / 2);
  memset(frame->data[2], 128, frame->linesize[2] * VIDEO_HEIGHT / 2);

  frame->pts = ctx->next_video_pts++;

  // Send to encoder
  ret = avcodec_send_frame(ctx->v_enc_ctx, frame);
  if (ret < 0) return ret;

  while (ret >= 0) 
  {
    AVPacket* pkt = av_packet_alloc();
    ret = avcodec_receive_packet(ctx->v_enc_ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      av_packet_free(&pkt);
      break;
    } else if (ret < 0) return ret;

    // Rescale timestamps (Encoder -> Output Stream)
    av_packet_rescale_ts(pkt, ctx->v_enc_ctx->time_base, ctx->v_stream->time_base);
    pkt->stream_index = ctx->v_stream->index;
    
    printf("[Video] Write PTS: %lld\n", pkt->pts);
    
    // Interleaved write ensures correct ordering
    av_interleaved_write_frame(ctx->fmt_ctx, pkt);
    av_packet_free(&pkt);
  }
  return 0;
}

// ------------------------------------------------------------------
// 4. Read and Write one Audio Packet
// ------------------------------------------------------------------
int write_audio_packet(MuxerContext* ctx) 
{
  AVPacket* pkt = av_packet_alloc();
  int ret = av_read_frame(ctx->a_in_fmt_ctx, pkt);
  
  if (ret < 0) {
    // Audio EOF
    av_packet_free(&pkt);
    ctx->audio_finished = 1;
    printf("Audio finished early.\n");
    return 0;
  }

  // Check if this is the audio stream we want
  if (pkt->stream_index == ctx->a_in_stream_idx) {
    AVStream* in_stream = ctx->a_in_fmt_ctx->streams[ctx->a_in_stream_idx];
    
    // Rescale timestamps (Input Stream -> Output Stream)
    // Note: Since we are doing Stream Copy, Input TB often equals Output TB,
    // but this step is safe and recommended.
    av_packet_rescale_ts(pkt, in_stream->time_base, ctx->a_out_stream->time_base);
    
    pkt->stream_index = ctx->a_out_stream->index;
    ctx->next_audio_pts = pkt->pts + pkt->duration; // Update current audio progress

    // printf("[Audio] Write PTS: %lld\n", pkt->pts);
    av_interleaved_write_frame(ctx->fmt_ctx, pkt);
  }

  av_packet_free(&pkt);
  return 0;
}