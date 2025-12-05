/**
 * FFmpeg C API: Procedural "Broken Glass" Effect
 * 
 * Technical Logic:
 * 1. Midpoint Displacement Algorithm: Recursively subdivides lines with 
 *    random jitter to create natural-looking fractures.
 * 2. Visual Style: Draws lines twice. Once dark (shadow) and once bright 
 *    (highlight) offset by 1 pixel to simulate light refraction on glass edges.
 * 3. Seed Control: Resets random seed per frame to ensure the crack stays 
 *    static (frozen) rather than vibrating like lightning.
 * 
 * Pipeline:
 * [Input] -> [Decoder] -> [SwsScale to YUV420P] -> [Draw Crack] -> [Encoder] -> [Output]
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <math.h>
 #include <time.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/imgutils.h>
 #include <libavutil/opt.h>
 #include <libswscale/swscale.h>
 
 #define OUT_FILE "crack_output.mp4"
 
 // --- Visual Configuration ---
 #define CRACK_Y_SHADOW    16   // Dark Gray (Crack Fissure)
 #define CRACK_Y_LIGHT     235  // Bright White (Edge Highlight)
 #define CRACK_U           128  // Neutral Chroma
 #define CRACK_V           128  // Neutral Chroma
 
 // Algorithm Parameters
 #define NUM_BRANCHES      6    // How many main cracks radiate from center
 #define MAX_JITTER        12.0f // How jagged the cracks are
 #define MIN_SEG_LEN       4.0f  // Recursion stop condition (pixels)
 #define CRACK_RADIUS      350   // Length of cracks
 
 // -----------------------------------------------------------------------------
 // Helper: Safe Pixel Setter (YUV420P)
 // -----------------------------------------------------------------------------
 void 
 set_pixel_safe(AVFrame* frame, int x, int y, uint8_t y_val, uint8_t u_val, uint8_t v_val) 
 {
   // 1. Boundary Check (Crucial for preventing Segfaults)
   if (x < 0 || x >= frame->width || y < 0 || y >= frame->height) return;
   
   // 2. Memory Existence Check
   if (!frame->data[0] || !frame->data[1] || !frame->data[2]) return;
 
   // 3. Set Luma (Y) - Full Resolution
   frame->data[0][y * frame->linesize[0] + x] = y_val;
 
   // 4. Set Chroma (U/V) - 2x2 Subsampling
   // In YUV420P, 4 pixels (2x2 square) share one U and one V value.
   // We only write to the chroma planes if we are on an even coordinate.
   if (x % 2 == 0 && y % 2 == 0) {
     int uv_idx = (y / 2) * frame->linesize[1] + (x / 2);
     frame->data[1][uv_idx] = u_val;
     frame->data[2][uv_idx] = v_val;
   }
 }
 
 // -----------------------------------------------------------------------------
 // Helper: Draw Simple Line Primitive
 // -----------------------------------------------------------------------------
 void 
 draw_line_primitive(AVFrame* frame, float x1, float y1, float x2, float y2, 
                     uint8_t yv, uint8_t uv, uint8_t vv) 
 {
   float dx = x2 - x1;
   float dy = y2 - y1;
   float len = sqrtf(dx*dx + dy*dy);
   
   if (len < 1.0f) return;
 
   // Normalize step
   float step_x = dx / len;
   float step_y = dy / len;
 
   // Simple stepping loop (Good enough for fractal segments)
   for (int i = 0; i <= (int)len; i++) {
     int px = (int)(x1 + step_x * i);
     int py = (int)(y1 + step_y * i);
     set_pixel_safe(frame, px, py, yv, uv, vv);
   }
 }
 
 // -----------------------------------------------------------------------------
 // Core Logic: Recursive Fractal Generation
 // -----------------------------------------------------------------------------
 /**
  * @brief Recursively subdivides a segment to create a jagged look.
  * 
  * @param frame   Target frame.
  * @param x1, y1  Start point.
  * @param x2, y2  End point.
  * @param jitter  Current amplitude of randomness.
  */
 void 
 draw_fractal_segment(AVFrame* frame, float x1, float y1, float x2, float y2, float jitter) 
 {
   float dx = x2 - x1;
   float dy = y2 - y1;
   float len_sq = dx*dx + dy*dy;
 
   // Base Case: If the segment is small enough, just draw it.
   if (len_sq < (MIN_SEG_LEN * MIN_SEG_LEN)) {
     // 1. Draw the "Shadow" (The crack gap)
     draw_line_primitive(frame, x1, y1, x2, y2, 
                         CRACK_Y_SHADOW, CRACK_U, CRACK_V);
     
     // 2. Draw the "Highlight" (The light catching the glass edge)
     // We offset coordinates by -1 pixel to create a bevel effect.
     draw_line_primitive(frame, x1-1, y1-1, x2-1, y2-1, 
                         CRACK_Y_LIGHT, CRACK_U, CRACK_V);
     return;
   }
 
   // Recursive Step: Midpoint Displacement
   float mid_x = (x1 + x2) / 2.0f;
   float mid_y = (y1 + y2) / 2.0f;
 
   // Generate random offset perpendicular-ish to the line
   // (Here we just use raw XY jitter for chaos)
   float off_x = ((float)(rand() % 100) / 50.0f - 1.0f) * jitter;
   float off_y = ((float)(rand() % 100) / 50.0f - 1.0f) * jitter;
 
   mid_x += off_x;
   mid_y += off_y;
 
   // Recurse with reduced jitter (details get finer as lines get smaller)
   draw_fractal_segment(frame, x1, y1, mid_x, mid_y, jitter * 0.6f);
   draw_fractal_segment(frame, mid_x, mid_y, x2, y2, jitter * 0.6f);
 }
 
 /**
  * @brief Main Entry Point for Crack Effect
  * 
  * @param seed A fixed integer. Using the same seed every frame ensures 
  *             the crack doesn't move (it looks like physical damage).
  */
 void 
 mml_draw_crack_effect(AVFrame* frame, int cx, int cy, int radius, unsigned int seed) 
 {
   // Reset Random State (Crucial for static effect)
   srand(seed);
 
   // Generate main branches radiating from center
   for (int i = 0; i < NUM_BRANCHES; i++) {
     // Angle: Evenly distributed + small random variation
     float angle_base = ((float)i / NUM_BRANCHES) * 2.0f * M_PI;
     float angle_var  = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f; 
     float angle = angle_base + angle_var;
 
     // Calculate End Point
     // Vary the radius slightly so cracks aren't a perfect circle
     float r_var = radius * (0.8f + ((float)(rand() % 40) / 100.0f)); 
     float ex = cx + cosf(angle) * r_var;
     float ey = cy + sinf(angle) * r_var;
 
     // Start Recursion
     draw_fractal_segment(frame, (float)cx, (float)cy, ex, ey, MAX_JITTER);
 
     // Optional: Add spider-web cross-links
     if (i > 0 && (rand() % 3 == 0)) {
         // Draw a connector between this branch and the previous one
         // (Logic omitted for brevity, but follows same pattern)
     }
   }
 }
 
 // -----------------------------------------------------------------------------
 // Encoder Helper
 // -----------------------------------------------------------------------------
 int 
 encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame, int64_t* pts) 
 {
   // Prepare frame for encoding
   if (frame) {
     // Deep copy if needed to ensure we can modify/read safely
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
     }
     
     // Rescale timebase (Encoder -> Container)
     av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
     pkt->stream_index = st->index;
     
     av_interleaved_write_frame(fmt, pkt);
     av_packet_unref(pkt);
   }
   av_packet_free(&pkt);
   return 0;
 }
 
 // -----------------------------------------------------------------------------
 // Main Application
 // -----------------------------------------------------------------------------
 int main(int argc, char* argv[]) {
   if (argc < 2) {
     printf("Usage: %s <input_video>\n", argv[0]);
     return 1;
   }
 
   // --- 1. Input Setup ---
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
 
   // --- 2. Output Setup ---
   AVFormatContext* out_fmt = NULL;
   avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
   const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
   AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
   
   // Output Settings
   enc_ctx->width = dec_ctx->width;
   enc_ctx->height = dec_ctx->height;
   enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P; // Standard H.264 pixel format
   enc_ctx->time_base = (AVRational){1, 30};
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
 
   // --- 3. Processing Loop ---
   AVPacket* pkt = av_packet_alloc();
   AVFrame* dec_frame = av_frame_alloc();
   
   // "Work Frame" ensures we always draw on YUV420P memory
   AVFrame* work_frame = av_frame_alloc();
   struct SwsContext* sws = NULL;
   
   int64_t pts = 0;
   int initialized = 0;
 
   printf("Generating Crack Effect... Output: %s\n", OUT_FILE);
 
   while (av_read_frame(in_fmt, pkt) >= 0) {
     if (pkt->stream_index == vid_idx) {
       avcodec_send_packet(dec_ctx, pkt);
       
       while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
         
         // Lazy Initialization (Wait for first frame dimensions)
         if (!initialized) {
           sws = sws_getContext(
             dec_frame->width, dec_frame->height, dec_frame->format, // Src
             dec_frame->width, dec_frame->height, AV_PIX_FMT_YUV420P, // Dst
             SWS_BILINEAR, NULL, NULL, NULL
           );
           
           work_frame->format = AV_PIX_FMT_YUV420P;
           work_frame->width = dec_frame->width;
           work_frame->height = dec_frame->height;
           av_frame_get_buffer(work_frame, 32);
           
           initialized = 1;
         }
 
         // A. Convert Input to safe YUV420P work buffer
         sws_scale(sws, (const uint8_t* const*)dec_frame->data, dec_frame->linesize,
                   0, dec_frame->height, work_frame->data, work_frame->linesize);
 
         // B. Apply Crack Effect
         av_frame_make_writable(work_frame);
         
         // Use a fixed center (Middle of screen)
         int cx = work_frame->width / 2;
         int cy = work_frame->height / 2;
         
         // Use a FIXED seed (42) so the crack doesn't change every frame
         mml_draw_crack_effect(work_frame, cx, cy, CRACK_RADIUS, 42);
 
         // C. Encode
         encode_write(enc_ctx, out_fmt, out_st, work_frame, &pts);
         
         if (pts % 30 == 0) printf("Processed Frames: %ld\r", pts);
       }
     }
     av_packet_unref(pkt);
   }
 
   // Flush Encoder (Send NULL)
   encode_write(enc_ctx, out_fmt, out_st, NULL, &pts);
   av_write_trailer(out_fmt);
 
   // Cleanup
   if (sws) sws_freeContext(sws);
   av_frame_free(&work_frame);
   av_frame_free(&dec_frame);
   av_packet_free(&pkt);
   avcodec_free_context(&dec_ctx);
   avcodec_free_context(&enc_ctx);
   avformat_close_input(&in_fmt);
   if (out_fmt && !(out_fmt->oformat->flags & AVFMT_NOFILE))
     avio_closep(&out_fmt->pb);
   avformat_free_context(out_fmt);
 
   printf("\nDone.\n");
   return 0;
 }