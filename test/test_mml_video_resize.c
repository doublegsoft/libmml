/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
// #include <stdio.h>
// #include "libmml.h"

// int main(int argc, char* argv[])
// {
//   const char* video_path = "../../data/V1.mp4";
//   const char* output_path = "V1_1920x1080.mp4";
//   int rc = mml_video_resize(video_path, output_path, 1920, 1080);
//   if (rc != MML_SUCCESS)
//     printf("error: %s\n", mml_error());
// 	return 0;
// }
/**
 * FFmpeg C API: Video Resizer
 * 
 * Logic:
 * 1. Open Input (Any Resolution).
 * 2. Configure Output (Fixed Resolution: 1280x720).
 * 3. Initialize SwsContext (Scaler).
 * 4. Loop: Decode -> sws_scale -> Encode.
 * 
 * Compile:
 * gcc video_resize.c -o video_resize -lavformat -lavcodec -lavutil -lswscale
 */

 #include <stdio.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/imgutils.h>
 #include <libavutil/opt.h>
 #include <libswscale/swscale.h>
 
#include "libmml-frame.h"

 #define OUT_FILE "resized_720p.mp4"
 #define TARGET_W 1280
 #define TARGET_H 720
 
 // -----------------------------------------------------------------------------
 // Helper: Encode and Write Packet
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
 
     // Rescale timestamps: Encoder Timebase -> Muxer Timebase
     av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
     pkt->stream_index = st->index;
 
     av_interleaved_write_frame(fmt, pkt);
     av_packet_unref(pkt);
   }
   av_packet_free(&pkt);
   return 0;
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
 
   // --- CONFIGURING RESIZED OUTPUT ---
   enc_ctx->width = TARGET_W;
   enc_ctx->height = TARGET_H;
   enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
   enc_ctx->time_base = (AVRational){1, 30}; // Assuming 30fps for simplicity
   enc_ctx->framerate = (AVRational){30, 1};
   
   if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) 
     enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
 
   avcodec_open2(enc_ctx, enc, NULL);
 
   AVStream* out_st = avformat_new_stream(out_fmt, NULL);
   avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
   out_st->time_base = enc_ctx->time_base;
 
   if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
     avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
   
   avformat_write_header(out_fmt, NULL);
 
   // 3. Runtime Resources
   AVPacket* pkt = av_packet_alloc();
   AVFrame* src_frame = av_frame_alloc();
   
   // Destination Frame (The Resized One)
   AVFrame* dst_frame = NULL;
 
   struct SwsContext* sws = NULL;
   int64_t next_pts = 0;
 
   printf("Resizing %dx%d -> %dx%d...\n", 
          dec_ctx->width, dec_ctx->height, TARGET_W, TARGET_H);
 
   // 4. Processing Loop
   while (av_read_frame(in_fmt, pkt) >= 0) {
     if (pkt->stream_index == vid_idx) {
       
       // Decode
       if (avcodec_send_packet(dec_ctx, pkt) == 0) {
         while (avcodec_receive_frame(dec_ctx, src_frame) == 0) {
           
          mml_frame_resize(src_frame, &dst_frame, &sws, TARGET_W, TARGET_H);
 
           // Copy PTS so the video plays at correct speed
           // Ideally, rescale src_frame->pts if timebases differ, 
           // but generating new monotonic PTS is safer for simple resizing.
           dst_frame->pts = next_pts++;
           dst_frame->pkt_dts = AV_NOPTS_VALUE;
 
           // Encode
           mml_frame_write(enc_ctx, out_fmt, out_st, dst_frame);
           
           if (next_pts % 30 == 0) printf("Processed frames: %ld\r", next_pts);
         }
       }
     }
     av_packet_unref(pkt);
   }
 
   // Flush
   mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
   av_write_trailer(out_fmt);
 
   // Cleanup
   sws_freeContext(sws);
   av_frame_free(&src_frame);
   av_frame_free(&dst_frame);
   av_packet_free(&pkt);
   avcodec_free_context(&dec_ctx);
   avcodec_free_context(&enc_ctx);
   avformat_close_input(&in_fmt);
   avio_closep(&out_fmt->pb);
   avformat_free_context(out_fmt);
 
   printf("\nDone. Saved to %s\n", OUT_FILE);
   return 0;
 }