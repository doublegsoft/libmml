/**
 * FFmpeg C API: Readable Long Image Scroll Video
 * 
 * Features:
 * 1. Auto-resize image to fit video width.
 * 2. Vertical scroll based on readable speed (pixels per second).
 * 3. Hold at start and end for better user experience.
 * 4. Style: 2-space indent, pointers left-aligned.
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#define OUT_FILE "scroll_final.mp4"

// --- Video Configuration ---
#define VIDEO_W 1080
#define VIDEO_H 1920
#define FPS 30

// --- Reading Experience Configuration ---
#define READ_SPEED_PPS  250.0  // Speed: 250 pixels per second (Adjust for speed)
#define HOLD_SEC        2.0    // Pause time at Start and End (Seconds)

// -----------------------------------------------------------------------------
// Helper: Load Image, Resize to VIDEO_W, Convert to YUV420P
// Returns a "Giant Frame" containing the entire long image
// -----------------------------------------------------------------------------
int load_long_image(const char* filename, AVFrame** out_giant_frame) {
  AVFormatContext* fmt_ctx = NULL;
  if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) return -1;
  avformat_find_stream_info(fmt_ctx, NULL);
  
  const AVCodec* dec = NULL;
  int idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
  AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
  avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[idx]->codecpar);
  avcodec_open2(dec_ctx, dec, NULL);

  AVPacket* pkt = av_packet_alloc();
  AVFrame* raw_frame = av_frame_alloc();
  AVFrame* yuv_frame = NULL;
  int ret = -1;

  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == idx) {
      if (avcodec_send_packet(dec_ctx, pkt) == 0) {
        if (avcodec_receive_frame(dec_ctx, raw_frame) == 0) {
          
          // 1. Calculate New Height based on Aspect Ratio
          // We fix Width to VIDEO_W (1080)
          int new_w = VIDEO_W;
          int new_h = (int)((double)raw_frame->height * ((double)VIDEO_W / raw_frame->width));
          
          // Force height to be even (Required for YUV420P)
          new_h &= ~1; 

          printf("[Image Loaded] Original: %dx%d -> Resized: %dx%d\n", 
                raw_frame->width, raw_frame->height, new_w, new_h);

          // 2. Allocate Giant YUV Frame
          yuv_frame = av_frame_alloc();
          yuv_frame->format = AV_PIX_FMT_YUV420P;
          yuv_frame->width  = new_w;
          yuv_frame->height = new_h;
          if (av_frame_get_buffer(yuv_frame, 32) < 0) {
            fprintf(stderr, "Failed to allocate giant frame\n");
            break;
          }

          // 3. Scale & Convert (RGB -> YUV420P)
          struct SwsContext* sws = sws_getContext(
            raw_frame->width, raw_frame->height, raw_frame->format,
            new_w, new_h, AV_PIX_FMT_YUV420P,
            SWS_BICUBIC, NULL, NULL, NULL);
          
          sws_scale(sws, (const uint8_t* const*)raw_frame->data, raw_frame->linesize,
                    0, raw_frame->height, yuv_frame->data, yuv_frame->linesize);
          
          sws_freeContext(sws);
          *out_giant_frame = yuv_frame;
          ret = 0;
          break;
        }
      }
    }
    av_packet_unref(pkt);
  }

  av_frame_free(&raw_frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avformat_close_input(&fmt_ctx);
  return ret;
}

// -----------------------------------------------------------------------------
// Helper: Copy Viewport (Memcpy based crop)
// -----------------------------------------------------------------------------
void copy_viewport(AVFrame* giant, AVFrame* out, int y_offset) {
  // Safety clamps
  if (y_offset < 0) y_offset = 0;
  int max_y = giant->height - out->height;
  if (max_y < 0) max_y = 0; // Image shorter than screen
  if (y_offset > max_y) y_offset = max_y;

  // 1. Copy Luma (Y)
  for (int i = 0; i < out->height; i++) {
    uint8_t* src_ptr = giant->data[0] + (y_offset + i) * giant->linesize[0];
    uint8_t* dst_ptr = out->data[0] + i * out->linesize[0];
    memcpy(dst_ptr, src_ptr, VIDEO_W);
  }

  // 2. Copy Chroma (U/V) - Subsampled
  int uv_h = out->height / 2;
  int uv_offset = y_offset / 2;
  int uv_w = VIDEO_W / 2;

  for (int p = 1; p < 3; p++) {
    for (int i = 0; i < uv_h; i++) {
      uint8_t* src_ptr = giant->data[p] + (uv_offset + i) * giant->linesize[p];
      uint8_t* dst_ptr = out->data[p] + i * out->linesize[p];
      memcpy(dst_ptr, src_ptr, uv_w);
    }
  }
}

// -----------------------------------------------------------------------------
// Helper: Encode and Write Packet
// -----------------------------------------------------------------------------
int encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame, int64_t* pts) {
  if (frame) {
    if (av_frame_make_writable(frame) < 0) return -1;
    frame->pts = (*pts)++;
    frame->pkt_dts = AV_NOPTS_VALUE;
  }

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
// Main Function
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  if (argc < 2) { 
    printf("Usage: %s <long_image>\n", argv[0]); 
    return 1; 
  }

  // --- 1. Load Long Image ---
  AVFrame* giant_frame = NULL;
  if (load_long_image(argv[1], &giant_frame) < 0) {
    fprintf(stderr, "Error loading image.\n");
    return 1;
  }

  // --- 2. Setup Output Context (Muxer + Encoder) ---
  AVFormatContext* out_fmt = NULL;
  avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
  
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
  enc_ctx->width = VIDEO_W;
  enc_ctx->height = VIDEO_H;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = (AVRational){1, FPS};
  enc_ctx->framerate = (AVRational){FPS, 1};
  
  if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) 
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  
  if (avcodec_open2(enc_ctx, enc, NULL) < 0) return 1;

  AVStream* out_st = avformat_new_stream(out_fmt, NULL);
  avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
  out_st->time_base = enc_ctx->time_base;

  if (avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE) < 0) return 1;
  avformat_write_header(out_fmt, NULL);

  // --- 3. Allocate Video Viewport Frame ---
  AVFrame* video_frame = av_frame_alloc();
  video_frame->format = AV_PIX_FMT_YUV420P;
  video_frame->width = VIDEO_W;
  video_frame->height = VIDEO_H;
  av_frame_get_buffer(video_frame, 32);

  // --- 4. Calculate Time & Frames ---
  // Scroll distance = Total Height - One Screen Height
  int scroll_distance = giant_frame->height - VIDEO_H;
  if (scroll_distance < 0) scroll_distance = 0;

  // Time = Distance / Speed
  double scroll_duration = (double)scroll_distance / READ_SPEED_PPS;
  
  // Total Time = Hold Start + Scroll + Hold End
  double total_duration = HOLD_SEC + scroll_duration + HOLD_SEC;
  int total_frames = (int)(total_duration * FPS);

  printf("[Info] Image Height: %d\n", giant_frame->height);
  printf("[Info] Scroll Dist : %d px\n", scroll_distance);
  printf("[Info] Scroll Time : %.2f sec\n", scroll_duration);
  printf("[Info] Total Time  : %.2f sec (%d frames)\n", total_duration, total_frames);

  // --- 5. Render Loop ---
  int64_t pts = 0;

  for (int i = 0; i < total_frames; i++) {
    double current_time = (double)i / FPS;
    int current_y = 0;

    // Timeline Logic
    if (current_time < HOLD_SEC) {
      // Phase 1: Hold Top
      current_y = 0;
    } 
    else if (current_time < (HOLD_SEC + scroll_duration)) {
      // Phase 2: Scroll Down (Linear)
      double time_scrolling = current_time - HOLD_SEC;
      double progress = time_scrolling / scroll_duration;
      current_y = (int)(scroll_distance * progress);
    } 
    else {
      // Phase 3: Hold Bottom
      current_y = scroll_distance;
    }

    // Align Y to even for YUV420P
    current_y &= ~1;

    // Copy & Encode
    copy_viewport(giant_frame, video_frame, current_y);
    encode_write(enc_ctx, out_fmt, out_st, video_frame, &pts);

    if (i % 60 == 0) printf("Render: %.1f%% (Y=%d)\r", (double)i/total_frames*100, current_y);
  }

  // --- 6. Cleanup ---
  encode_write(enc_ctx, out_fmt, out_st, NULL, &pts); // Flush
  av_write_trailer(out_fmt);

  av_frame_free(&giant_frame);
  av_frame_free(&video_frame);
  avcodec_free_context(&enc_ctx);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone. Saved to %s\n", OUT_FILE);
  return 0;
}