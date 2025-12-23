/**
 * FFmpeg C API: Transcode + Merge + Reset PTS
 * Logic: 
 * - Frame-by-Frame Video Transcoding.
 * - Audio Loops if shorter than video.
 * - Audio Cuts off if longer than video.
 * 
 * Compile:
 * gcc merge_loop_cut.c -o merge_loop_cut -lavformat -lavcodec -lavutil -lswscale -lswresample
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include "libmml-frame.h"
#include "libmml-video.h"
#include "libmml-audio.h"

#define OUT_FILE "output_smart_merge.mp4"
#define VIDEO_FPS 30

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  if (argc < 3) {
    printf("Usage: %s <video_in> <audio_in>\n", argv[0]);
    return 1;
  }

  AVFormatContext* in_v_fmt = NULL;
  AVFormatContext* in_a_fmt = NULL;
  int in_v_idx = -1;
  int in_a_idx = -1;
  AVCodecContext* v_dec_ctx = NULL;
  AVCodecContext* a_dec_ctx = NULL;

  AVFormatContext* out_fmt = NULL;
  AVCodecContext* v_enc_ctx = NULL;
  AVCodecContext* a_enc_ctx = NULL;
  AVStream* out_v_stream = NULL;
  AVStream* out_a_stream = NULL;

  mml_video_load(argv[1], &in_v_fmt, &v_dec_ctx, &in_v_idx, OUT_FILE, &out_fmt, &v_enc_ctx, &out_v_stream);
  // mml_video_load(argv[1], &in_v_fmt, &v_dec_ctx, &in_v_idx, NULL, NULL, NULL, NULL);

  // Audio Input
  // avformat_open_input(&in_a_fmt, argv[2], NULL, NULL);
  // avformat_find_stream_info(in_a_fmt, NULL);
  // int in_a_idx = av_find_best_stream(in_a_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  // const AVCodec* a_dec = avcodec_find_decoder(in_a_fmt->streams[in_a_idx]->codecpar->codec_id);
  // a_dec_ctx = avcodec_alloc_context3(a_dec);
  // avcodec_parameters_to_context(a_dec_ctx, in_a_fmt->streams[in_a_idx]->codecpar);
  // avcodec_open2(a_dec_ctx, a_dec, NULL);

  // avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
  // const AVCodec* v_enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  // v_enc_ctx = avcodec_alloc_context3(v_enc);
  // v_enc_ctx->width = v_dec_ctx->width;
  // v_enc_ctx->height = v_dec_ctx->height;
  // v_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  // v_enc_ctx->time_base = (AVRational){1, VIDEO_FPS};
  // v_enc_ctx->framerate = (AVRational){VIDEO_FPS, 1};
  // if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) v_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  // avcodec_open2(v_enc_ctx, v_enc, NULL);
  // out_v_stream = avformat_new_stream(out_fmt, NULL);
  // avcodec_parameters_from_context(out_v_stream->codecpar, v_enc_ctx);
  // out_v_stream->time_base = v_enc_ctx->time_base;

  // Audio Encoder (AAC)
  // const AVCodec* a_enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
  // a_enc_ctx = avcodec_alloc_context3(a_enc);
  // a_enc_ctx->sample_rate = 44100;
  // a_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
  // a_enc_ctx->bit_rate = 128000;
  // av_channel_layout_default(&a_enc_ctx->ch_layout, 2);
  // a_enc_ctx->time_base = (AVRational){1, a_enc_ctx->sample_rate};
  // if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) a_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  // avcodec_open2(a_enc_ctx, a_enc, NULL);
  // out_a_stream = avformat_new_stream(out_fmt, NULL);
  // avcodec_parameters_from_context(out_a_stream->codecpar, a_enc_ctx);
  // out_a_stream->time_base = a_enc_ctx->time_base;

  mml_audio_load(argv[2], &in_a_fmt, &a_dec_ctx, &in_a_idx, out_fmt, &a_enc_ctx, &out_a_stream);

  if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
  avformat_write_header(out_fmt, NULL);

  struct SwsContext* sws = NULL;
  SwrContext* swr = NULL;
  swr_alloc_set_opts2(&swr, &a_enc_ctx->ch_layout, a_enc_ctx->sample_fmt, a_enc_ctx->sample_rate,
                      &a_dec_ctx->ch_layout, a_dec_ctx->sample_fmt, a_dec_ctx->sample_rate, 0, NULL);
  swr_init(swr);
  AVAudioFifo* fifo = av_audio_fifo_alloc(a_enc_ctx->sample_fmt, a_enc_ctx->ch_layout.nb_channels, 1);

  AVPacket* pkt = av_packet_alloc();
  AVFrame* v_raw = av_frame_alloc();
  AVFrame* v_out = av_frame_alloc();
  AVFrame* a_raw = av_frame_alloc(); 
  AVFrame* a_out = av_frame_alloc();

  // Configure reusable frames
  v_out->format = AV_PIX_FMT_YUV420P;
  v_out->width = v_enc_ctx->width;
  v_out->height = v_enc_ctx->height;
  av_frame_get_buffer(v_out, 32);

  a_out->nb_samples = a_enc_ctx->frame_size;
  a_out->format = a_enc_ctx->sample_fmt;
  av_channel_layout_copy(&a_out->ch_layout, &a_enc_ctx->ch_layout);
  a_out->sample_rate = a_enc_ctx->sample_rate;
  av_frame_get_buffer(a_out, 0);

  int64_t next_v_pts = 0;
  int64_t next_a_pts = 0;
  int video_finished = 0;

  printf("Processing... (Video Drives the loop)\n");

  while (!video_finished) {
    
    // --- STEP A: Read & Encode One Video Frame ---
    int got_video_frame = 0;
    while (!got_video_frame) {
      int ret = av_read_frame(in_v_fmt, pkt);
      if (ret < 0) {
        video_finished = 1;
        break; 
      }

      if (pkt->stream_index == in_v_idx) {
        if (avcodec_send_packet(v_dec_ctx, pkt) == 0) {
          if (avcodec_receive_frame(v_dec_ctx, v_raw) == 0) {
            
            // Process Video (Scale -> Reset PTS -> Encode)
            if (!sws) {
              sws = sws_getContext(v_raw->width, v_raw->height, v_raw->format,
                                  v_enc_ctx->width, v_enc_ctx->height, AV_PIX_FMT_YUV420P,
                                  SWS_BILINEAR, NULL, NULL, NULL);
            }
            sws_scale(sws, (const uint8_t* const*)v_raw->data, v_raw->linesize,
                      0, v_raw->height, v_out->data, v_out->linesize);

            v_out->pts = next_v_pts++;
            v_out->pkt_dts = AV_NOPTS_VALUE;
            
            mml_frame_write(v_enc_ctx, out_fmt, out_v_stream, v_out);
            got_video_frame = 1;
            
            if (next_v_pts % 30 == 0) printf("Video: %ld frames\r", next_v_pts);
          }
        }
      }
      av_packet_unref(pkt);
    }

    if (video_finished) break;

    // --- STEP B: Process Audio until it catches up to Video ---
    // Calculate current times in Seconds
    double video_time = (double)next_v_pts / VIDEO_FPS;
    double audio_time = (double)next_a_pts / a_enc_ctx->sample_rate;

    while (audio_time < video_time) {
      
      int ret = av_read_frame(in_a_fmt, pkt);
      
      // >>> AUDIO LOOP LOGIC <<<
      if (ret == AVERROR_EOF) {
        // Rewind to start
        av_seek_frame(in_a_fmt, in_a_idx, 0, AVSEEK_FLAG_BACKWARD);
        // Flush Decoder buffers (Critical for looping audio)
        avcodec_flush_buffers(a_dec_ctx);
        continue;
      }

      if (pkt->stream_index == in_a_idx) {
        if (avcodec_send_packet(a_dec_ctx, pkt) == 0) {
          while (avcodec_receive_frame(a_dec_ctx, a_raw) == 0) {
            // Resample
            uint8_t** tmp = NULL;
            av_samples_alloc_array_and_samples(&tmp, NULL, 2, a_raw->nb_samples, AV_SAMPLE_FMT_FLTP, 0);
            swr_convert(swr, tmp, a_raw->nb_samples, (const uint8_t**)a_raw->extended_data, a_raw->nb_samples);
            av_audio_fifo_write(fifo, (void**)tmp, a_raw->nb_samples);
            av_freep(&tmp[0]); free(tmp);

            // Encode (if enough samples)
            while (av_audio_fifo_size(fifo) >= a_enc_ctx->frame_size) {
              av_audio_fifo_read(fifo, (void**)a_out->data, a_enc_ctx->frame_size);
              
              a_out->pts = next_a_pts;
              next_a_pts += a_out->nb_samples;
              
              mml_frame_write(a_enc_ctx, out_fmt, out_a_stream, a_out);
              
              // Update audio time check
              audio_time = (double)next_a_pts / a_enc_ctx->sample_rate;
            }
          }
        }
      }
      av_packet_unref(pkt);
    }
  }

  // --- FLUSH & CLEANUP ---
  mml_frame_write(v_enc_ctx, out_fmt, out_v_stream, NULL);
  mml_frame_write(a_enc_ctx, out_fmt, out_a_stream, NULL);
  av_write_trailer(out_fmt);

  if (sws) sws_freeContext(sws);
  if (swr) swr_free(&swr);
  av_audio_fifo_free(fifo);
  av_frame_free(&v_raw); av_frame_free(&v_out);
  av_frame_free(&a_raw); av_frame_free(&a_out);
  av_packet_free(&pkt);
  
  avcodec_free_context(&v_dec_ctx); avcodec_free_context(&v_enc_ctx);
  avcodec_free_context(&a_dec_ctx); avcodec_free_context(&a_enc_ctx);
  
  avformat_close_input(&in_v_fmt);
  avformat_close_input(&in_a_fmt);
  if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone.\n");
  return 0;
}