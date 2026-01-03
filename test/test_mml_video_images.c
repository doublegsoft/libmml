// #include <unistd.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <time.h>

// #include  "libmml.h"

// #define IN_FILE "/Users/christian/Downloads/test.mp4"
// #define OUT_FILE "/Users/christian/Downloads/test_out.mp4"

// int main(int argc, char **argv) {
//   clock_t start, end;
//   double cpu_time_used;
//   start = clock();
//   mml_video_save_images(IN_FILE, 5, 20, "/Users/christian/Downloads/frames", 1);
//   end = clock();
//   cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
//   printf("test_mml_video_images elapsed CPU time: %f seconds\n", cpu_time_used);
// }

/**
 * FFmpeg C API: Video to Images (Interval Based)
 * 
 * Logic:
 * 1. Seek to Target Time.
 * 2. Decode until frame_pts >= Target Time.
 * 3. Save JPEG.
 * 4. Target Time += Interval. Repeat.
 * 
 * Compile:
 * gcc video_to_images.c -o video_to_images -lavformat -lavcodec -lavutil -lswscale
 */

 #include <stdio.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/imgutils.h>
 #include <libswscale/swscale.h>
 
 #define INTERVAL 0.5 // Save a frame every 5 seconds
 
 // -----------------------------------------------------------------------------
 // Helper: Save AVFrame to JPEG
 // -----------------------------------------------------------------------------
 int save_jpeg(AVFrame* frame, int frame_no) {
   char filename[64];
   snprintf(filename, sizeof(filename), "frames/frame_%03d.jpg", frame_no);
 
   const AVCodec* jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
   if (!jpegCodec) return -1;
 
   AVCodecContext* jpegContext = avcodec_alloc_context3(jpegCodec);
   if (!jpegContext) return -1;
 
   jpegContext->pix_fmt = AV_PIX_FMT_YUVJ420P; // JPEG format
   jpegContext->height  = frame->height;
   jpegContext->width   = frame->width;
   jpegContext->time_base = (AVRational){1, 25};
 
   if (avcodec_open2(jpegContext, jpegCodec, NULL) < 0) return -1;
 
   // Convert Input Format -> YUVJ420P (Required for MJPEG)
   // We use a temporary frame/scaler to be safe against NV12/RGB inputs
   AVFrame* jpgFrame = av_frame_alloc();
   jpgFrame->format = AV_PIX_FMT_YUVJ420P;
   jpgFrame->width  = frame->width;
   jpgFrame->height = frame->height;
   av_frame_get_buffer(jpgFrame, 32);
 
   struct SwsContext* sws = sws_getContext(
     frame->width, frame->height, frame->format,
     frame->width, frame->height, AV_PIX_FMT_YUVJ420P,
     SWS_BILINEAR, NULL, NULL, NULL);
   
   sws_scale(sws, (const uint8_t* const*)frame->data, frame->linesize,
             0, frame->height, jpgFrame->data, jpgFrame->linesize);
 
   // Encode
   AVPacket* pkt = av_packet_alloc();
   avcodec_send_frame(jpegContext, jpgFrame);
   
   if (avcodec_receive_packet(jpegContext, pkt) == 0) {
     FILE* file = fopen(filename, "wb");
     if (file) {
       fwrite(pkt->data, 1, pkt->size, file);
       fclose(file);
       printf("Saved: %s\n", filename);
     }
     av_packet_unref(pkt);
   }
 
   // Cleanup local resources
   av_packet_free(&pkt);
   av_frame_free(&jpgFrame);
   avcodec_free_context(&jpegContext);
   sws_freeContext(sws);
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
 
   const char* in_file = argv[1];
 
   // 1. Open Input
   AVFormatContext* fmt_ctx = NULL;
   if (avformat_open_input(&fmt_ctx, in_file, NULL, NULL) < 0) {
     fprintf(stderr, "Could not open input.\n");
     return 1;
   }
   avformat_find_stream_info(fmt_ctx, NULL);
 
   // 2. Find Video Decoder
   int stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
   if (stream_idx < 0) return 1;
 
   AVStream* stream = fmt_ctx->streams[stream_idx];
   const AVCodec* dec = avcodec_find_decoder(stream->codecpar->codec_id);
   AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
   avcodec_parameters_to_context(dec_ctx, stream->codecpar);
   avcodec_open2(dec_ctx, dec, NULL);
 
   AVPacket* pkt = av_packet_alloc();
   AVFrame* frame = av_frame_alloc();
 
   // 3. Extraction Loop
   double duration = (double)fmt_ctx->duration / AV_TIME_BASE;
   double target_sec = 0.0;
   int img_count = 0;
 
   printf("Video Duration: %.2fs. Interval: %.2fs\n", duration, INTERVAL);
 
   while (target_sec < duration) {
     
     // A. Seek to Target (Snap to previous Keyframe)
     // Convert seconds to Stream Timebase
     int64_t target_pts = (int64_t)(target_sec / av_q2d(stream->time_base));
     
     // Seek backward to ensure we land before the target
     if (av_seek_frame(fmt_ctx, stream_idx, target_pts, AVSEEK_FLAG_BACKWARD) < 0) {
       fprintf(stderr, "Seek error.\n");
       break; 
     }
 
     // Important: Flush buffers after seeking so decoder doesn't output old frames
     avcodec_flush_buffers(dec_ctx);
 
     int found = 0;
 
     // B. Decode Forward until we hit target
     while (!found && av_read_frame(fmt_ctx, pkt) >= 0) {
       if (pkt->stream_index == stream_idx) {
         if (avcodec_send_packet(dec_ctx, pkt) == 0) {
           while (avcodec_receive_frame(dec_ctx, frame) == 0) {
             
             // Calculate current frame time
             double current_sec = frame->pts * av_q2d(stream->time_base);
 
             // If we have reached (or passed) the target time...
             if (current_sec >= target_sec) {
               
               // Save Image
               save_jpeg(frame, img_count++);
               
               // Move target forward
               target_sec += INTERVAL;
               
               // Break inner decoding loop to trigger next Seek
               found = 1;
               break; 
             }
           }
         }
       }
       av_packet_unref(pkt);
     }
 
     if (!found) break; // End of file reached inside decode loop
   }
 
   // Cleanup
   av_packet_free(&pkt);
   av_frame_free(&frame);
   avcodec_free_context(&dec_ctx);
   avformat_close_input(&fmt_ctx);
 
   printf("Done. Extracted %d images.\n", img_count);
   return 0;
 }

