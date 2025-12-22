/**
 * FFmpeg C API: Manual Reverse + Slow Motion (No Filters)
 * 
 * Logic:
 * 1. Decode entire video -> Store frames in a dynamic array (RAM).
 * 2. Iterate array backwards (Reverse).
 * 3. Encode each frame N times (Slow Motion).
 * 
 * Compile:
 * gcc reverse_slow_manual.c -o reverse_slow_manual -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#define OUT_FILE "reverse_slow_manual.mp4"
#define SLOW_FACTOR 2  // 2x Slower
#define TIME_BASE 30   // Output FPS

// -----------------------------------------------------------------------------
// Helper: Encode and Write
// -----------------------------------------------------------------------------
int encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame) {
  int ret = avcodec_send_frame(enc, frame);
  if (ret < 0) return ret;

  AVPacket* pkt = av_packet_alloc();
  while (ret >= 0) {
    ret = avcodec_receive_packet(enc, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      av_packet_free(&pkt);
      return 0;
    } else if (ret < 0) {
      av_packet_free(&pkt);
      return ret;
    }

    av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
    pkt->stream_index = st->index;
    av_interleaved_write_frame(fmt, pkt);
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  return 0;
}

// -----------------------------------------------------------------------------
// Helper: Create a deep copy of a frame (converted to YUV420P)
// -----------------------------------------------------------------------------
AVFrame* clone_as_yuv420p(AVFrame* src, struct SwsContext** sws_cache) {
  AVFrame* dst = av_frame_alloc();
  dst->format = AV_PIX_FMT_YUV420P;
  dst->width = src->width;
  dst->height = src->height;
  
  if (av_frame_get_buffer(dst, 32) < 0) {
    av_frame_free(&dst);
    return NULL;
  }

  // Initialize Scaler if needed
  if (!*sws_cache) {
    *sws_cache = sws_getContext(
        src->width, src->height, src->format,
        src->width, src->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL);
  }

  // Convert/Copy pixel data
  sws_scale(*sws_cache, (const uint8_t* const*)src->data, src->linesize,
            0, src->height, dst->data, dst->linesize);

  return dst;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Usage: %s <input_video>\n", argv[0]);
    return 1;
  }

  // 1. Setup Input
  AVFormatContext* in_fmt = NULL;
  if (avformat_open_input(&in_fmt, argv[1], NULL, NULL) < 0) {
    fprintf(stderr, "Could not open input.\n");
    return 1;
  }
  avformat_find_stream_info(in_fmt, NULL);
  int vid_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVCodecContext* dec_ctx = avcodec_alloc_context3(NULL);
  avcodec_parameters_to_context(dec_ctx, in_fmt->streams[vid_idx]->codecpar);
  const AVCodec* dec = avcodec_find_decoder(dec_ctx->codec_id);
  avcodec_open2(dec_ctx, dec, NULL);

  // 2. Setup Output
  AVFormatContext* out_fmt = NULL;
  avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
  enc_ctx->width = dec_ctx->width;
  enc_ctx->height = dec_ctx->height;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = (AVRational){1, TIME_BASE};
  enc_ctx->framerate = (AVRational){TIME_BASE, 1};
  
  if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) 
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  
  avcodec_open2(enc_ctx, enc, NULL);
  AVStream* out_st = avformat_new_stream(out_fmt, NULL);
  avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
  out_st->time_base = enc_ctx->time_base;
  avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
  avformat_write_header(out_fmt, NULL);

  // 3. Decoding & Buffering Loop
  AVPacket* pkt = av_packet_alloc();
  AVFrame* temp_frame = av_frame_alloc();
  struct SwsContext* sws = NULL;

  // Dynamic Array to store ALL frames
  AVFrame** frame_list = NULL;
  int frame_count = 0;
  int list_capacity = 0;

  printf("Phase 1: Decoding and Buffering Frames (RAM Usage will rise)...\n");

  while (av_read_frame(in_fmt, pkt) >= 0) {
    if (pkt->stream_index == vid_idx) {
      if (avcodec_send_packet(dec_ctx, pkt) == 0) {
        while (avcodec_receive_frame(dec_ctx, temp_frame) == 0) {
          
          // A. Deep Copy Frame to Memory
          AVFrame* stored_frame = clone_as_yuv420p(temp_frame, &sws);
          if (!stored_frame) {
            fprintf(stderr, "OOM or Error cloning frame.\n");
            break;
          }

          // B. Resize List if needed
          if (frame_count >= list_capacity) {
            list_capacity = (list_capacity == 0) ? 128 : list_capacity * 2;
            frame_list = realloc(frame_list, sizeof(AVFrame*) * list_capacity);
          }

          // C. Store
          frame_list[frame_count++] = stored_frame;
          if (frame_count % 30 == 0) printf("Buffered: %d frames\r", frame_count);
        }
      }
    }
    av_packet_unref(pkt);
  }

  printf("\nPhase 2: Reverse & Slow Encode (%d frames stored)...\n", frame_count);

  // 4. Reverse Processing Loop
  int64_t pts = 0;

  // Iterate Backwards
  for (int i = frame_count - 1; i >= 0; i--) {
    AVFrame* frame = frame_list[i];

    // Ensure frame metadata is clean for the encoder
    frame->pkt_dts = AV_NOPTS_VALUE;
    frame->pict_type = AV_PICTURE_TYPE_NONE;

    // Encode multiple times for Slow Motion
    for (int k = 0; k < SLOW_FACTOR; k++) {
      
      // Make writable mainly to ensure thread safety if encoder buffers
      av_frame_make_writable(frame);
      
      // Set new monotonic PTS
      frame->pts = pts++;

      if (encode_write(enc_ctx, out_fmt, out_st, frame) < 0) {
        fprintf(stderr, "Encode failed.\n");
        break;
      }
    }
    
    if ((frame_count - i) % 30 == 0) 
      printf("Encoded: %d / %d\r", frame_count - i, frame_count);
  }

  // 5. Cleanup
  encode_write(enc_ctx, out_fmt, out_st, NULL); // Flush
  av_write_trailer(out_fmt);

  // Free all stored frames
  for (int i = 0; i < frame_count; i++) {
    av_frame_free(&frame_list[i]);
  }
  free(frame_list);

  sws_freeContext(sws);
  av_frame_free(&temp_frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&in_fmt);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone. Output: %s\n", OUT_FILE);
  return 0;
}