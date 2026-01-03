/**
 * FFmpeg C API: Hand-Written Ellipse Animation
 * 
 * Compile:
 * gcc hand_ellipse.c -o hand_ellipse -lavformat -lavcodec -lavutil -lm
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <math.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/frame.h>
 #include <libavutil/opt.h>
 
 #define OUT_FILE "hand_ellipse.mp4"
 #define WIDTH 1280
 #define HEIGHT 720
 #define FPS 30
 #define DURATION 3
 
 #ifndef M_PI
 #define M_PI 3.14159265358979323846
 #endif
 
 // -----------------------------------------------------------------------------
 // Helper: Draw a Brush Tip (Pixel Blending)
 // -----------------------------------------------------------------------------
 static void 
 mml_blend_dot(AVFrame* frame, int cx, int cy, int radius, 
               uint8_t y_val, uint8_t u_val, uint8_t v_val, float alpha) 
 {
   int r_sq = radius * radius;
   
   for (int y = cy - radius; y <= cy + radius; y++) {
     for (int x = cx - radius; x <= cx + radius; x++) {
       
       if (x < 0 || x >= frame->width || y < 0 || y >= frame->height) continue;
 
       int dist = (x - cx)*(x - cx) + (y - cy)*(y - cy);
       if (dist <= r_sq) {
         
         int a_scale = (int)(alpha * 256);
         int inv_scale = 256 - a_scale;
 
         // Blend Y
         int y_idx = y * frame->linesize[0] + x;
         frame->data[0][y_idx] = (uint8_t)((y_val * a_scale + frame->data[0][y_idx] * inv_scale) >> 8);
 
         // Blend UV (Even coordinates)
         if (x % 2 == 0 && y % 2 == 0) {
           int uv_idx = (y / 2) * frame->linesize[1] + (x / 2);
           frame->data[1][uv_idx] = (uint8_t)((u_val * a_scale + frame->data[1][uv_idx] * inv_scale) >> 8);
           frame->data[2][uv_idx] = (uint8_t)((v_val * a_scale + frame->data[2][uv_idx] * inv_scale) >> 8);
         }
       }
     }
   }
 }
 
 // -----------------------------------------------------------------------------
 // Core: Hand-Writing Ellipse Logic
 // -----------------------------------------------------------------------------
 
 /**
  * @brief Draws an animated "Hand-Written" Ellipse.
  * 
  * @param frame     Target frame.
  * @param cx, cy    Center coordinates.
  * @param rx        Horizontal Radius.
  * @param ry        Vertical Radius.
  * @param thickness Pen thickness.
  * @param progress  0.0 (Start) -> 1.0 (Finished Drawing).
  * @param seed      Random seed (Pass a CONSTANT value to keep shape static).
  */
 void 
 mml_draw_hand_ellipse(AVFrame* frame, 
                       int cx, int cy, 
                       int rx, int ry, 
                       int thickness,
                       uint8_t y_val, uint8_t u_val, uint8_t v_val,
                       float progress, 
                       unsigned int seed) 
 {
   if (progress <= 0.0f) return;
   if (progress > 1.0f) progress = 1.0f;
 
   // 1. Setup Noise Parameters (Deterministic based on seed)
   srand(seed); 
   
   // Random start angle (-20 to +20 degrees)
   float start_angle = ((rand() % 40) - 20) * (M_PI / 180.0f);
   
   // Random total length (360 to 420 degrees for overshoot)
   float total_angle = (360.0f + (rand() % 60)) * (M_PI / 180.0f);
 
   // Noise frequencies and amplitudes
   float distort_freq = 2.0f + ((rand() % 10) / 10.0f); 
   float distort_amp  = 5.0f; // Deviation in pixels
   
   float tremor_freq  = 12.0f + ((rand() % 10) / 10.0f);
   float tremor_amp   = 2.0f; // Shaky hand pixels
 
   // 2. Calculate Current End Angle (with Easing)
   // Smoothstep easing for natural pen movement
   float ease_p = progress * progress * (3.0f - 2.0f * progress);
   float current_max_angle = start_angle + (total_angle * ease_p);
 
   // 3. Draw Path (Iterate angles)
   // Step size: Smaller steps for larger ellipses to keep line solid
   int max_r = (rx > ry) ? rx : ry;
   float step = 1.0f / (max_r * 1.5f); 
 
   for (float a = start_angle; a < current_max_angle; a += step) {
     
     // Calculate Noise
     float noise = sinf(a * distort_freq) * distort_amp 
                 + cosf(a * tremor_freq) * tremor_amp;
 
     // Apply Noise to Radii
     float curr_rx = rx + noise;
     float curr_ry = ry + noise;
 
     // Parametric Equation for Ellipse
     int px = cx + (int)(curr_rx * cosf(a));
     int py = cy + (int)(curr_ry * sinf(a));
 
     // Optional: Fade tip (Ink drying effect)
     float ink_alpha = 1.0f;
     if (current_max_angle - a < 0.1f) {
        ink_alpha = (current_max_angle - a) * 10.0f;
     }
 
     // Draw Pen Stroke
     mml_blend_dot(frame, px, py, thickness / 2, y_val, u_val, v_val, ink_alpha);
   }
 }
 
 // -----------------------------------------------------------------------------
 // Boilerplate: Encoder
 // -----------------------------------------------------------------------------
 int encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame, int64_t* pts) {
   if (frame) {
     frame->pts = (*pts)++;
     frame->pkt_dts = AV_NOPTS_VALUE;
   }
   avcodec_send_frame(enc, frame);
   AVPacket* pkt = av_packet_alloc();
   while (avcodec_receive_packet(enc, pkt) >= 0) {
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
 int main() {
   // Setup Output
   AVFormatContext* fmt_ctx = NULL;
   avformat_alloc_output_context2(&fmt_ctx, NULL, NULL, OUT_FILE);
   const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
   AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
   enc_ctx->width = WIDTH; enc_ctx->height = HEIGHT;
   enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
   enc_ctx->time_base = (AVRational){1, FPS}; enc_ctx->framerate = (AVRational){FPS, 1};
   if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
   avcodec_open2(enc_ctx, enc, NULL);
   AVStream* st = avformat_new_stream(fmt_ctx, NULL);
   avcodec_parameters_from_context(st->codecpar, enc_ctx);
   st->time_base = enc_ctx->time_base;
   if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_open(&fmt_ctx->pb, OUT_FILE, AVIO_FLAG_WRITE);
   avformat_write_header(fmt_ctx, NULL);
 
   AVFrame* frame = av_frame_alloc();
   frame->format = AV_PIX_FMT_YUV420P;
   frame->width = WIDTH; frame->height = HEIGHT;
   av_frame_get_buffer(frame, 32);
 
   int total_frames = FPS * DURATION;
   int64_t pts = 0;
 
   printf("Generating Hand-Written Ellipse...\n");
 
   for (int i = 0; i < total_frames; i++) {
     // Background: White Paper
     for(int y=0; y<HEIGHT; y++) memset(frame->data[0] + y*frame->linesize[0], 235, WIDTH);
     for(int y=0; y<HEIGHT/2; y++) {
       memset(frame->data[1] + y*frame->linesize[1], 128, WIDTH/2);
       memset(frame->data[2] + y*frame->linesize[2], 128, WIDTH/2);
     }
 
     float progress = (float)i / (total_frames - 20); // Finish early to hold
 
     // Draw Red Marker Ellipse
     // Center: 640, 360
     // Radius X: 300, Radius Y: 150 (Flat Oval)
     mml_draw_hand_ellipse(frame, 
                           640, 360, 
                           300, 150, 
                           8, 
                           76, 84, 255, // Red
                           progress, 
                           12345); // Constant Seed
 
     encode_write(enc_ctx, fmt_ctx, st, frame, &pts);
     if (i%30==0) printf("Frame %d\r", i);
   }
 
   encode_write(enc_ctx, fmt_ctx, st, NULL, &pts);
   av_write_trailer(fmt_ctx);
   
   // Cleanup
   av_frame_free(&frame);
   avcodec_free_context(&enc_ctx);
   if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt_ctx->pb);
   avformat_free_context(fmt_ctx);
 
   printf("Done. Saved to %s\n", OUT_FILE);
   return 0;
 }