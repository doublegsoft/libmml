/**
 * FFmpeg C API: Dynamic Zoom In (No Filters)
 * 
 * Logic:
 * 1. Calculate Crop Region based on zoom factor (1.0x -> 2.0x).
 * 2. Point sws_scale source to the crop offset.
 * 3. Scale Crop -> Output Resolution.
 *
 * Compile:
 * gcc zoom_video.c -o zoom_video -lavformat -lavcodec -lavutil -lswscale
 */

 #include <stdio.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/imgutils.h>
 #include <libavutil/opt.h>
 #include <libswscale/swscale.h>
 
 #define OUT_FILE "zoom_output.mp4"
 #define TOTAL_FRAMES 150
 #define MAX_ZOOM 2.0f // Zoom from 1.0x to 2.0x
 
 // -----------------------------------------------------------------------------
 // Helper: Encode and Write
 // -----------------------------------------------------------------------------
 int encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame, int64_t pts) {
   if (frame) {
     if (av_frame_make_writable(frame) < 0) return -1;
     frame->pts = pts;
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
 // Main
 // -----------------------------------------------------------------------------
 int main(int argc, char* argv[]) {
   if (argc < 2) {
     printf("Usage: %s <input>\n", argv[0]);
     return 1;
   }
 
   // 1. Input Setup
   AVFormatContext* in_fmt = NULL;
   avformat_open_input(&in_fmt, argv[1], NULL, NULL);
   avformat_find_stream_info(in_fmt, NULL);
   int vid_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
   AVCodecContext* dec_ctx = avcodec_alloc_context3(NULL);
   avcodec_parameters_to_context(dec_ctx, in_fmt->streams[vid_idx]->codecpar);
   const AVCodec* dec = avcodec_find_decoder(dec_ctx->codec_id);
   avcodec_open2(dec_ctx, dec, NULL);
 
   // 2. Output Setup
   AVFormatContext* out_fmt = NULL;
   avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
   const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
   AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
   enc_ctx->width = dec_ctx->width;
   enc_ctx->height = dec_ctx->height;
   enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
   enc_ctx->time_base = (AVRational){1, 30};
   enc_ctx->framerate = (AVRational){30, 1};
   if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
   avcodec_open2(enc_ctx, enc, NULL);
   AVStream* out_st = avformat_new_stream(out_fmt, NULL);
   avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
   out_st->time_base = enc_ctx->time_base;
   avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
   avformat_write_header(out_fmt, NULL);
 
   // 3. Resources
   AVPacket* pkt = av_packet_alloc();
   AVFrame* src_frame = av_frame_alloc();
   AVFrame* dst_frame = av_frame_alloc();
   
   // Allocate Destination Frame (Fixed Size)
   dst_frame->format = AV_PIX_FMT_YUV420P;
   dst_frame->width = enc_ctx->width;
   dst_frame->height = enc_ctx->height;
   av_frame_get_buffer(dst_frame, 32);
 
   struct SwsContext* sws = NULL;
   int64_t pts = 0;
   int frame_count = 0;
 
   printf("Processing Zoom...\n");
 
   while (av_read_frame(in_fmt, pkt) >= 0) {
     if (pkt->stream_index == vid_idx) {
       avcodec_send_packet(dec_ctx, pkt);
       while (avcodec_receive_frame(dec_ctx, src_frame) == 0) {
         
         // --- ZOOM LOGIC ---
         
         // 1. Calculate Zoom Factor (Linear Interpolation)
         float zoom = 1.0f;
         if (frame_count < TOTAL_FRAMES) {
           zoom = 1.0f + ((MAX_ZOOM - 1.0f) * ((float)frame_count / TOTAL_FRAMES));
         } else {
           zoom = MAX_ZOOM;
         }
 
         // 2. Calculate Crop Dimensions
         // As zoom increases, the crop window gets smaller
         int crop_w = (int)(src_frame->width / zoom);
         int crop_h = (int)(src_frame->height / zoom);
 
         // Ensure dimensions are even (required for YUV420P)
         crop_w &= ~1; 
         crop_h &= ~1;
 
         // 3. Calculate Top-Left Offset (Center Zoom)
         int off_x = (src_frame->width - crop_w) / 2;
         int off_y = (src_frame->height - crop_h) / 2;
         
         // Ensure offsets are even
         off_x &= ~1; 
         off_y &= ~1;
 
         // 4. Update Scaler Context
         // Since input dimensions change every frame (crop_w/h changes), 
         // we must re-init or use sws_getCachedContext.
         sws = sws_getCachedContext(sws,
           crop_w, crop_h, src_frame->format,   // Input: The Crop Size
           dst_frame->width, dst_frame->height, AV_PIX_FMT_YUV420P, // Output: Full HD
           SWS_BILINEAR, NULL, NULL, NULL
         );
 
         // 5. Setup Source Pointers (The Trick)
         // Instead of pointing to (0,0), we point to (off_x, off_y).
         const uint8_t* src_data[4];
         int src_linesize[4];
 
         // Copy standard linesizes
         for (int i=0; i<4; i++) src_linesize[i] = src_frame->linesize[i];
 
         // Offset Y Plane
         src_data[0] = src_frame->data[0] + (off_y * src_frame->linesize[0]) + off_x;
 
         // Offset U and V Planes (YUV420P = coordinates / 2)
         int uv_off_x = off_x / 2;
         int uv_off_y = off_y / 2;
         src_data[1] = src_frame->data[1] + (uv_off_y * src_frame->linesize[1]) + uv_off_x;
         src_data[2] = src_frame->data[2] + (uv_off_y * src_frame->linesize[2]) + uv_off_x;
 
         // 6. Scale
         sws_scale(sws, 
                   src_data, src_linesize, 
                   0, crop_h, // We process the 'height' of the crop
                   dst_frame->data, dst_frame->linesize);
 
         // 7. Encode
         encode_write(enc_ctx, out_fmt, out_st, dst_frame, pts++);
         
         frame_count++;
         if (frame_count % 30 == 0) printf("Frame: %d | Zoom: %.2fx\r", frame_count, zoom);
       }
     }
     av_packet_unref(pkt);
   }
 
   // Flush
   encode_write(enc_ctx, out_fmt, out_st, NULL, pts);
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
 
   printf("\nDone.\n");
   return 0;
 }