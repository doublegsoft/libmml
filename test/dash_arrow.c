/**
 * FFmpeg C API: Flowing Dashed Arrow Intro Effect
 * 
 * Logic:
 * 1. Read Frame 1. Convert to YUV420P. Save as Background.
 * 2. Generate N frames of animation (Frozen BG + Moving Arrow).
 * 3. Encode these frames.
 * 4. Continue reading input video, convert to YUV420P, encode.
 */

 #include <stdio.h>
 #include <math.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/imgutils.h>
 #include <libavutil/opt.h>
 #include <libswscale/swscale.h>
 
 #define OUT_FILE "arrow_complete.mp4"
 
 // --- Visual Configuration ---
 // Color: Gold (approx YUV)
 #define DRAW_Y 160
 #define DRAW_U 60
 #define DRAW_V 200
 
 #define LINE_THICKNESS 5.0f   // Thick line
 #define DASH_LENGTH    40.0f  // Long dash
 #define GAP_LENGTH     25.0f  // Wide gap (for visibility)
 #define FLOW_SPEED     8.0f   // Pixels per frame
 #define ARROW_HEAD_LEN 35.0f
 
 // --- Animation Timing ---
 #define EXTEND_FRAMES 45      // Duration of one extension
 #define BLINK_INTERVAL 10     // Blink speed
 #define BLINK_COUNT    2      // How many times to blink
 
 // Total frames generated for the intro
 const int TOTAL_INTRO_FRAMES = (EXTEND_FRAMES * 2) + (BLINK_COUNT * 2 * BLINK_INTERVAL);
 
 // -----------------------------------------------------------------------------
 // Helper: Safe Pixel Set (Bounds & Null Checks)
 // -----------------------------------------------------------------------------
 void set_pixel_safe(AVFrame* frame, int x, int y, uint8_t yv, uint8_t uv, uint8_t vv, float alpha) {
   // 1. Bounds Check
   if (x < 0 || x >= frame->width || y < 0 || y >= frame->height) return;
   
   // 2. Memory Check (Prevent Segfault)
   if (!frame->data[0] || !frame->data[1] || !frame->data[2]) return;
 
   // 3. Blend Luma
   int y_idx = y * frame->linesize[0] + x;
   uint8_t bg = frame->data[0][y_idx];
   frame->data[0][y_idx] = (uint8_t)(yv * alpha + bg * (1.0f - alpha));
 
   // 4. Blend Chroma (Subsampled 2x2)
   if (x % 2 == 0 && y % 2 == 0) {
     int uv_idx = (y / 2) * frame->linesize[1] + (x / 2);
     if (alpha > 0.5f) {
       frame->data[1][uv_idx] = uv;
       frame->data[2][uv_idx] = vv;
     }
   }
 }
 
 // -----------------------------------------------------------------------------
 // Helper: Draw Flowing Dashed Line
 // -----------------------------------------------------------------------------
 void draw_dashed_arrow_body(AVFrame* frame, float x1, float y1, float x2, float y2, float phase) {
   // Bounding Box
   int min_x = (int)fmin(x1, x2) - LINE_THICKNESS - 2;
   int min_y = (int)fmin(y1, y2) - LINE_THICKNESS - 2;
   int max_x = (int)fmax(x1, x2) + LINE_THICKNESS + 2;
   int max_y = (int)fmax(y1, y2) + LINE_THICKNESS + 2;
 
   if (min_x < 0) min_x = 0;
   if (min_y < 0) min_y = 0;
   if (max_x >= frame->width) max_x = frame->width - 1;
   if (max_y >= frame->height) max_y = frame->height - 1;
 
   float dx = x2 - x1;
   float dy = y2 - y1;
   float total_len_sq = dx * dx + dy * dy;
   float total_len = sqrtf(total_len_sq);
   
   if (total_len < 0.1f) return;
 
   float unit_dx = dx / total_len;
   float unit_dy = dy / total_len;
   float period = DASH_LENGTH + GAP_LENGTH;
 
   for (int y = min_y; y <= max_y; y++) {
     for (int x = min_x; x <= max_x; x++) {
       
       // Project point to line
       float t = ((x - x1) * dx + (y - y1) * dy) / total_len_sq;
       if (t < 0 || t > 1) continue; // Out of segment bounds
 
       float cx = x1 + t * dx;
       float cy = y1 + t * dy;
       float dist_to_line = sqrtf(pow(x - cx, 2) + pow(y - cy, 2));
 
       if (dist_to_line > LINE_THICKNESS) continue;
 
       // --- Dashed Flow Logic ---
       float dist_along_line = (x - x1) * unit_dx + (y - y1) * unit_dy;
       
       // Apply phase (Movement)
       // We subtract phase so the dashes move forward (from start to end)
       float flow_pos = dist_along_line - phase;
       
       // Modulo math for pattern
       float cycle = fmodf(flow_pos, period);
       if (cycle < 0) cycle += period; // Handle negative phase
 
       // If in Gap, skip
       if (cycle > DASH_LENGTH) continue;
       // -------------------------
 
       // Anti-aliasing
       float alpha = 1.0f;
       if (dist_to_line > LINE_THICKNESS - 1.5f) {
         alpha = LINE_THICKNESS - dist_to_line;
       }
       set_pixel_safe(frame, x, y, DRAW_Y, DRAW_U, DRAW_V, alpha);
     }
   }
 }
 
 // -----------------------------------------------------------------------------
 // Helper: Draw Solid Arrow Head
 // -----------------------------------------------------------------------------
 void draw_solid_segment(AVFrame* frame, float x1, float y1, float x2, float y2) {
   // Standard line drawing for the head (no dashes)
   int min_x = (int)fmin(x1, x2) - LINE_THICKNESS - 2;
   int min_y = (int)fmin(y1, y2) - LINE_THICKNESS - 2;
   int max_x = (int)fmax(x1, x2) + LINE_THICKNESS + 2;
   int max_y = (int)fmax(y1, y2) + LINE_THICKNESS + 2;
   if (min_x < 0) min_x = 0; if (min_y < 0) min_y = 0;
   if (max_x >= frame->width) max_x = frame->width - 1;
   if (max_y >= frame->height) max_y = frame->height - 1;
   float dx = x2 - x1; float dy = y2 - y1; float len_sq = dx*dx + dy*dy;
   if (len_sq < 0.1f) return;
   for (int y = min_y; y <= max_y; y++) {
     for (int x = min_x; x <= max_x; x++) {
       float t = ((x - x1) * dx + (y - y1) * dy) / len_sq;
       if (t < 0) t = 0; if (t > 1) t = 1;
       float dist = sqrtf(pow(x - (x1 + t * dx), 2) + pow(y - (y1 + t * dy), 2));
       if (dist < LINE_THICKNESS) {
         float alpha = 1.0f;
         if (dist > LINE_THICKNESS - 1.0f) alpha = LINE_THICKNESS - dist;
         set_pixel_safe(frame, x, y, DRAW_Y, DRAW_U, DRAW_V, alpha);
       }
     }
   }
 }
 
 void draw_arrow_head(AVFrame* frame, float tip_x, float tip_y, float angle) {
   float x1 = tip_x - ARROW_HEAD_LEN * cosf(angle - M_PI / 6.0);
   float y1 = tip_y - ARROW_HEAD_LEN * sinf(angle - M_PI / 6.0);
   float x2 = tip_x - ARROW_HEAD_LEN * cosf(angle + M_PI / 6.0);
   float y2 = tip_y - ARROW_HEAD_LEN * sinf(angle + M_PI / 6.0);
   draw_solid_segment(frame, tip_x, tip_y, x1, y1);
   draw_solid_segment(frame, tip_x, tip_y, x2, y2);
 }
 
 // -----------------------------------------------------------------------------
 // Animation State Logic
 // -----------------------------------------------------------------------------
 void draw_anim_logic(AVFrame* frame, int idx) {
   // Coordinates
   float start_x = 200.0f;
   float start_y = frame->height - 200.0f;
   float end_x   = frame->width - 200.0f;
   float end_y   = 200.0f;
 
   float progress = 0.0f;
   int visible = 0;
 
   int phase_extend_end = EXTEND_FRAMES * 2;
   int phase_blink_end  = phase_extend_end + (BLINK_COUNT * 2 * BLINK_INTERVAL);
 
   if (idx < EXTEND_FRAMES) {
     // 1st Extension
     progress = (float)idx / EXTEND_FRAMES;
     visible = 1;
   } else if (idx < phase_extend_end) {
     // 2nd Extension
     progress = (float)(idx - EXTEND_FRAMES) / EXTEND_FRAMES;
     visible = 1;
   } else if (idx < phase_blink_end) {
     // Blinking
     progress = 1.0f;
     int blink_frame = idx - phase_extend_end;
     // Show on even intervals, hide on odd
     if ((blink_frame / BLINK_INTERVAL) % 2 == 0) visible = 1;
   }
 
   if (visible) {
     // Easing
     progress = progress * (2.0f - progress);
 
     float curr_x = start_x + (end_x - start_x) * progress;
     float curr_y = start_y + (end_y - start_y) * progress;
 
     // Calculate Flow Phase (Running Ants)
     float phase = idx * FLOW_SPEED;
 
     draw_dashed_arrow_body(frame, start_x, start_y, curr_x, curr_y, phase);
 
     if (progress > 0.05f) {
       float angle = atan2f(end_y - start_y, end_x - start_x);
       draw_arrow_head(frame, curr_x, curr_y, angle);
     }
   }
 }
 
 // -----------------------------------------------------------------------------
 // Encoder Wrapper
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
     printf("Usage: %s <input_video>\n", argv[0]);
     return 1;
   }
 
   // 1. Setup Input
   AVFormatContext* in_fmt = NULL;
   if (avformat_open_input(&in_fmt, argv[1], NULL, NULL) < 0) return 1;
   avformat_find_stream_info(in_fmt, NULL);
   int vid_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
   if (vid_idx < 0) return 1;
 
   AVCodecContext* dec_ctx = avcodec_alloc_context3(NULL);
   avcodec_parameters_to_context(dec_ctx, in_fmt->streams[vid_idx]->codecpar);
   const AVCodec* dec = avcodec_find_decoder(dec_ctx->codec_id);
   avcodec_open2(dec_ctx, dec, NULL);
 
   // 2. Setup Output
   AVFormatContext* out_fmt = NULL;
   avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
   
   const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
   AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
   
   // Resolution must match input
   enc_ctx->width = dec_ctx->width;
   enc_ctx->height = dec_ctx->height;
   enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P; // Standard H.264
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
 
   // 3. Runtime Resources
   AVPacket* pkt = av_packet_alloc();
   AVFrame* dec_frame = av_frame_alloc();
   
   // Lazy-initialized resources
   struct SwsContext* sws = NULL;
   AVFrame* bg_frame = NULL;     // Holds the frozen background YUV
   AVFrame* canvas_frame = NULL; // Holds the drawing YUV
   
   int64_t global_pts = 0;
   int intro_complete = 0;
 
   printf("Processing... Output: %s\n", OUT_FILE);
 
   while (av_read_frame(in_fmt, pkt) >= 0) {
     if (pkt->stream_index == vid_idx) {
       avcodec_send_packet(dec_ctx, pkt);
       
       while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
         
         // --- A. Initialization (Run Once) ---
         if (!sws) {
           // Initialize Scaler (Anything -> YUV420P)
           sws = sws_getContext(
               dec_frame->width, dec_frame->height, dec_frame->format,
               dec_frame->width, dec_frame->height, AV_PIX_FMT_YUV420P,
               SWS_BILINEAR, NULL, NULL, NULL);
 
           // Allocate buffers as YUV420P
           bg_frame = av_frame_alloc();
           bg_frame->format = AV_PIX_FMT_YUV420P;
           bg_frame->width = dec_frame->width;
           bg_frame->height = dec_frame->height;
           av_frame_get_buffer(bg_frame, 32);
 
           canvas_frame = av_frame_alloc();
           canvas_frame->format = AV_PIX_FMT_YUV420P;
           canvas_frame->width = dec_frame->width;
           canvas_frame->height = dec_frame->height;
           av_frame_get_buffer(canvas_frame, 32);
         }
 
         // --- B. Intro Phase ---
         if (!intro_complete) {
           printf("Generating Intro Animation (%d frames)...\n", TOTAL_INTRO_FRAMES);
 
           // 1. Save Frame 1 into bg_frame (Convert to YUV420P)
           sws_scale(sws, (const uint8_t* const*)dec_frame->data, dec_frame->linesize,
                     0, dec_frame->height, bg_frame->data, bg_frame->linesize);
 
           // 2. Loop to generate animation
           for (int i = 0; i < TOTAL_INTRO_FRAMES; i++) {
             // Reset canvas to clean background
             av_frame_copy(canvas_frame, bg_frame);
             av_frame_make_writable(canvas_frame);
 
             // Draw arrow
             draw_anim_logic(canvas_frame, i);
 
             // Encode
             encode_write(enc_ctx, out_fmt, out_st, canvas_frame, &global_pts);
           }
           
           intro_complete = 1;
           printf("Intro done. Resuming Video.\n");
         }
 
         // --- C. Normal Playback Phase ---
         // Convert current video frame to YUV420P and encode
         sws_scale(sws, (const uint8_t* const*)dec_frame->data, dec_frame->linesize,
                   0, dec_frame->height, canvas_frame->data, canvas_frame->linesize);
         
         encode_write(enc_ctx, out_fmt, out_st, canvas_frame, &global_pts);
       }
     }
     av_packet_unref(pkt);
   }
 
   // 4. Flush & Cleanup
   avcodec_send_frame(enc_ctx, NULL);
   encode_write(enc_ctx, out_fmt, out_st, NULL, &global_pts);
   av_write_trailer(out_fmt);
 
   if (sws) sws_freeContext(sws);
   if (bg_frame) av_frame_free(&bg_frame);
   if (canvas_frame) av_frame_free(&canvas_frame);
   
   av_frame_free(&dec_frame);
   av_packet_free(&pkt);
   avcodec_free_context(&dec_ctx);
   avcodec_free_context(&enc_ctx);
   avformat_close_input(&in_fmt);
   if (out_fmt && !(out_fmt->oformat->flags & AVFMT_NOFILE))
     avio_closep(&out_fmt->pb);
   avformat_free_context(out_fmt);
 
   printf("Done.\n");
   return 0;
 }