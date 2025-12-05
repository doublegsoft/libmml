/**
* FFmpeg C API: PiP Transcoder (No Structs, No Filters)
* 
* Compile:
* gcc pip_flat.c -o pip_flat -lavformat -lavcodec -lavutil -lswscale
*/

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-video.h"
#include "libmml-frame.h"

#define PIP_W 320
#define PIP_H 240
#define PIP_PAD 20
#define OUT_FILE "output_pip_flat.mp4"

// -----------------------------------------------------------------------------
// Helper: Manual Pixel Overlay (YUV420P)
// -----------------------------------------------------------------------------
void overlay_yuv420p(AVFrame* dst, AVFrame* src, int x_off, int y_off) {
  // Safety bounds check
  if (x_off < 0 || y_off < 0) return;
  if (x_off + src->width > dst->width) return;
  if (y_off + src->height > dst->height) return;

  // 1. Copy Y Plane (Luma)
  for (int y = 0; y < src->height; y++) {
    uint8_t* p_dst = dst->data[0] + ((y + y_off) * dst->linesize[0]) + x_off;
    uint8_t* p_src = src->data[0] + (y * src->linesize[0]);
    memcpy(p_dst, p_src, src->width);
  }

  // 2. Copy U and V Planes (Chroma)
  // Subsampled by 2 in both dimensions for YUV420P
  int uv_w = src->width / 2;
  int uv_h = src->height / 2;
  int uv_x = x_off / 2;
  int uv_y = y_off / 2;

  // Plane 1 = U, Plane 2 = V
  for (int i = 1; i < 3; i++) {
    for (int y = 0; y < uv_h; y++) {
      uint8_t* p_dst = dst->data[i] + ((y + uv_y) * dst->linesize[i]) + uv_x;
      uint8_t* p_src = src->data[i] + (y * src->linesize[i]);
      memcpy(p_dst, p_src, uv_w);
    }
  }
}

// -----------------------------------------------------------------------------
// Helper: Open Input File and setup Decoder
// -----------------------------------------------------------------------------
int open_input_file(const char* fname, 
                    AVFormatContext** fmt_ctx, 
                    AVCodecContext** dec_ctx, 
                    int* stream_idx) {
  *fmt_ctx = NULL;
  if (avformat_open_input(fmt_ctx, fname, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open %s\n", fname);
    return -1;
  }
  avformat_find_stream_info(*fmt_ctx, NULL);

  *stream_idx = av_find_best_stream(*fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (*stream_idx < 0) return -1;

  AVCodecParameters* par = (*fmt_ctx)->streams[*stream_idx]->codecpar;
  const AVCodec* dec = avcodec_find_decoder(par->codec_id);
  
  *dec_ctx = avcodec_alloc_context3(dec);
  avcodec_parameters_to_context(*dec_ctx, par);
  
  if (avcodec_open2(*dec_ctx, dec, NULL) < 0) return -1;
  return 0;
}

// -----------------------------------------------------------------------------
// Main Application
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {

  const char* main_video_path = "../../data/V3.mp4";
  const char* pip_video_path = "../../data/V1.mp4";

  // --- VARIABLES ---
  
  // Input 1 (Main)
  AVFormatContext* in1_fmt = NULL;
  AVCodecContext* in1_dec = NULL;
  int in1_idx = -1;

  AVPacket* in1_pkt = av_packet_alloc();
  AVFrame* in1_frame = av_frame_alloc();
  int in1_finished = 0;

  // Input 2 (PiP)
  AVFormatContext* in2_fmt = NULL;
  AVCodecContext* in2_dec = NULL;
  int in2_idx = -1;
  AVPacket* in2_pkt = av_packet_alloc();
  AVFrame* in2_frame = av_frame_alloc();
  int in2_finished = 0;

  // Output
  AVFormatContext* out_fmt = NULL;
  AVCodecContext* out_enc = NULL;
  AVStream* out_stream = NULL;
  int64_t next_pts = 0;

  // Processing (Scaler for PiP)
  struct SwsContext* sws_ctx = NULL;
  AVFrame* pip_small_frame = av_frame_alloc();

  // --- INITIALIZATION ---

  mml_video_load(main_video_path, &in1_fmt, &in1_dec, &in1_idx, OUT_FILE, &out_fmt, &out_enc, &out_stream);
  mml_video_load(pip_video_path, &in2_fmt, &in2_dec, &in2_idx, NULL, NULL, NULL, NULL);

  // 6. Setup Scaler (PiP -> 320x240 YUV420P)
  sws_ctx = sws_getContext(
    in2_dec->width, in2_dec->height, in2_dec->pix_fmt, // Src
    PIP_W, PIP_H, AV_PIX_FMT_YUV420P,                  // Dst
    SWS_BILINEAR, NULL, NULL, NULL
  );

  // Allocate buffer for the resized PiP frame
  pip_small_frame->format = AV_PIX_FMT_YUV420P;
  pip_small_frame->width = PIP_W;
  pip_small_frame->height = PIP_H;
  av_frame_get_buffer(pip_small_frame, 32);

  printf("Processing... Output: %s\n", OUT_FILE);

  // --- LOOP ---

  while (!in1_finished) {
    
    // Step A: Read & Decode Main Video
    int got_main = 0;
    while (av_read_frame(in1_fmt, in1_pkt) >= 0) {
      if (in1_pkt->stream_index == in1_idx) {
        avcodec_send_packet(in1_dec, in1_pkt);
        if (avcodec_receive_frame(in1_dec, in1_frame) == 0) {
          got_main = 1;
          av_packet_unref(in1_pkt);
          break; // Got a frame, proceed to processing
        }
      }
      av_packet_unref(in1_pkt);
    }
    if (!got_main) {
      in1_finished = 1;
      break;
    }

    // Step B: Read & Decode PiP Video (Best Effort)
    if (!in2_finished) {
      int got_pip = 0;
      while (av_read_frame(in2_fmt, in2_pkt) >= 0) {
        if (in2_pkt->stream_index == in2_idx) {
          avcodec_send_packet(in2_dec, in2_pkt);
          if (avcodec_receive_frame(in2_dec, in2_frame) == 0) {
            got_pip = 1;
            av_packet_unref(in2_pkt);
            break;
          }
        }
        av_packet_unref(in2_pkt);
      }
      
      if (got_pip) {
        // Resize PiP to small buffer
        sws_scale(sws_ctx, 
                  (const uint8_t* const*)in2_frame->data, in2_frame->linesize,
                  0, in2_frame->height,
                  pip_small_frame->data, pip_small_frame->linesize);
      } else {
        // Stop checking input 2 if it's done, but continue main loop
        // We will just keep overlaying the LAST valid PiP frame
        in2_finished = 1;
      }
    }

    // Step C: Manual Overlay
    // Ensure we can modify the main frame pixels
    av_frame_make_writable(in1_frame);

    // Calc Position: Top Right
    int x_pos = in1_frame->width - PIP_W - PIP_PAD;
    int y_pos = PIP_PAD;

    // Perform the copy (Check format to avoid crash)
    if (in1_frame->format == AV_PIX_FMT_YUV420P) {
      mml_frame_overlay(in1_frame, pip_small_frame, x_pos, y_pos);
    }

    // Step D: Encode
    // Reset PTS for the new output timeline
    in1_frame->pts = next_pts++;
    in1_frame->pkt_dts = AV_NOPTS_VALUE; // Clear old DTS
    in1_frame->pict_type = AV_PICTURE_TYPE_NONE;

    if (mml_frame_write(out_enc, out_fmt, out_stream, in1_frame) < 0) {
      fprintf(stderr, "Error encoding\n");
      break;
    }

    if (next_pts % 30 == 0) printf("Frames: %ld\r", next_pts);
  }

  // --- FLUSH & CLEANUP ---
  
  // Flush encoder
  mml_frame_write(out_enc, out_fmt, out_stream, NULL);
  av_write_trailer(out_fmt);
  printf("\nDone.\n");

end:
  // Free Main Input
  if (in1_dec) avcodec_free_context(&in1_dec);
  if (in1_fmt) avformat_close_input(&in1_fmt);
  if (in1_pkt) av_packet_free(&in1_pkt);
  if (in1_frame) av_frame_free(&in1_frame);

  // Free PiP Input
  if (in2_dec) avcodec_free_context(&in2_dec);
  if (in2_fmt) avformat_close_input(&in2_fmt);
  if (in2_pkt) av_packet_free(&in2_pkt);
  if (in2_frame) av_frame_free(&in2_frame);

  // Free Processing
  if (sws_ctx) sws_freeContext(sws_ctx);
  if (pip_small_frame) av_frame_free(&pip_small_frame);

  // Free Output
  if (out_fmt) {
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
      avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
  }
  if (out_enc) avcodec_free_context(&out_enc);

  return 0;
}