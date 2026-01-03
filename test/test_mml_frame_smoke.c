/**
 * FFmpeg C API: Add Smoke Effect to Existing Video
 * 
 * Pipeline:
 * 1. Decode Input Video -> Raw Frame.
 * 2. SwsScale -> YUV420P Work Frame (Canvas).
 * 3. Update Particle Physics.
 * 4. Draw Smoke ON TOP of Work Frame (Alpha Blend).
 * 5. Encode -> Output.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <math.h>
 #include <time.h>
 #include <string.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/imgutils.h>
 #include <libavutil/opt.h>
 #include <libswscale/swscale.h>
 
 #define OUT_FILE "video_with_smoke.mp4"
 
 // --- Smoke Configuration ---
 #define MAX_PARTICLES 1000
 #define EMIT_RATE     10
 #define SMOKE_COLOR_Y 200
 #define SPAWN_WIDTH   300
 
 typedef struct {
   float x, y;
   float vx, vy;
   float size;
   float life;
   float decay;
   float grow;
   int active;
 } Particle;
 
 Particle particles[MAX_PARTICLES];
 
 void init_particles() {
   memset(particles, 0, sizeof(particles));
 }
 
 void spawn_particle(int start_x, int start_y) {
   for (int i = 0; i < MAX_PARTICLES; i++) {
     if (!particles[i].active) {
       particles[i].active = 1;
       int offset = (rand() % SPAWN_WIDTH) - (SPAWN_WIDTH / 2);
       particles[i].x = start_x + offset;
       particles[i].y = start_y;
       
       particles[i].vx = ((rand() % 100) / 100.0f - 0.5f) * 2.0f;
       particles[i].vy = -1.5f - ((rand() % 100) / 50.0f);
       
       particles[i].size = 10.0f + (rand() % 20);
       particles[i].grow = 0.3f;
       particles[i].life = 1.0f;
       particles[i].decay = 0.01f;
       return;
     }
   }
 }
 
 void update_particles() {
   for (int i = 0; i < MAX_PARTICLES; i++) {
     if (particles[i].active) {
       particles[i].x += particles[i].vx;
       particles[i].y += particles[i].vy;
       particles[i].size += particles[i].grow;
       particles[i].life -= particles[i].decay;
       
       if (particles[i].life <= 0.0f || particles[i].y < -150) {
         particles[i].active = 0;
       }
     }
   }
 }
 
 // -----------------------------------------------------------------------------
 // Safe Drawing (Alpha Blends onto existing image content)
 // -----------------------------------------------------------------------------
 void draw_smoke_overlay(AVFrame* frame) {
   // Safety checks
   if (!frame || frame->format != AV_PIX_FMT_YUV420P) return;
   if (av_frame_make_writable(frame) < 0) return;
 
   for (int i = 0; i < MAX_PARTICLES; i++) {
     if (!particles[i].active) continue;
 
     int r = (int)particles[i].size;
     int cx = (int)particles[i].x;
     int cy = (int)particles[i].y;
     float base_alpha = particles[i].life * 0.15f; 
 
     int min_x = cx - r; if (min_x < 0) min_x = 0;
     int max_x = cx + r; if (max_x >= frame->width) max_x = frame->width;
     int min_y = cy - r; if (min_y < 0) min_y = 0;
     int max_y = cy + r; if (max_y >= frame->height) max_y = frame->height;
 
     if (min_x >= max_x || min_y >= max_y) continue;
 
     float r_sq = (float)(r * r);
 
     // --- Draw Y (Luma) ---
     for (int y = min_y; y < max_y; y++) {
       uint8_t* row = frame->data[0] + y * frame->linesize[0];
       for (int x = min_x; x < max_x; x++) {
         float dist_sq = (float)((x - cx)*(x - cx) + (y - cy)*(y - cy));
         if (dist_sq < r_sq) {
           float dist = sqrtf(dist_sq);
           float falloff = 1.0f - (dist / r);
           float alpha = base_alpha * falloff;
           
           int a_scale = (int)(alpha * 256);
           int inv_scale = 256 - a_scale;
           
           // BLEND: SmokeColor on top of VideoPixel
           // Video pixel is 'row[x]'
           row[x] = (uint8_t)((SMOKE_COLOR_Y * a_scale + row[x] * inv_scale) >> 8);
         }
       }
     }
 
     // --- Draw U/V (Chroma) ---
     int uv_min_x = min_x / 2; int uv_max_x = max_x / 2;
     int uv_min_y = min_y / 2; int uv_max_y = max_y / 2;
 
     for (int y = uv_min_y; y < uv_max_y; y++) {
       uint8_t* row_u = frame->data[1] + y * frame->linesize[1];
       uint8_t* row_v = frame->data[2] + y * frame->linesize[2];
       for (int x = uv_min_x; x < uv_max_x; x++) {
         int a_scale = (int)(base_alpha * 256);
         int inv_scale = 256 - a_scale;
         
         // Blend towards Gray (128)
         row_u[x] = (uint8_t)((128 * a_scale + row_u[x] * inv_scale) >> 8);
         row_v[x] = (uint8_t)((128 * a_scale + row_v[x] * inv_scale) >> 8);
       }
     }
   }
 }
 
 // -----------------------------------------------------------------------------
 // Encoder Helper
 // -----------------------------------------------------------------------------
 int encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame, int64_t* pts) {
   if (frame) {
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
 
   srand(time(NULL));
   init_particles();
 
   // 1. Input Setup
   AVFormatContext* in_fmt = NULL;
   avformat_open_input(&in_fmt, argv[1], NULL, NULL);
   avformat_find_stream_info(in_fmt, NULL);
   int vid_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
   
   AVCodecContext* dec_ctx = avcodec_alloc_context3(NULL);
   avcodec_parameters_to_context(dec_ctx, in_fmt->streams[vid_idx]->codecpar);
   const AVCodec* dec = avcodec_find_decoder(dec_ctx->codec_id);
   avcodec_open2(dec_ctx, dec, NULL);
 
   int width = dec_ctx->width;
   int height = dec_ctx->height;
   
   // Use input frame rate (or default to 30)
   AVRational fps = in_fmt->streams[vid_idx]->avg_frame_rate;
   if (fps.den == 0) { fps.num = 30; fps.den = 1; }
 
   // 2. Output Setup
   AVFormatContext* out_fmt = NULL;
   avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
   const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
   AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
   
   enc_ctx->width = width;
   enc_ctx->height = height;
   enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
   enc_ctx->time_base = av_inv_q(fps);
   enc_ctx->framerate = fps;
   
   if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) 
     enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
 
   avcodec_open2(enc_ctx, enc, NULL);
 
   AVStream* st = avformat_new_stream(out_fmt, NULL);
   avcodec_parameters_from_context(st->codecpar, enc_ctx);
   st->time_base = enc_ctx->time_base;
 
   if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
   avformat_write_header(out_fmt, NULL);
 
   // 3. Resources
   AVPacket* pkt = av_packet_alloc();
   AVFrame* raw_frame = av_frame_alloc();
   
   // "Work Frame": The YUV420P canvas we draw on
   AVFrame* work_frame = av_frame_alloc();
   work_frame->format = AV_PIX_FMT_YUV420P;
   work_frame->width = width;
   work_frame->height = height;
   av_frame_get_buffer(work_frame, 32);
 
   struct SwsContext* sws = NULL;
   int64_t pts = 0;
 
   printf("Processing Video (%dx%d)... Output: %s\n", width, height, OUT_FILE);
 
   // 4. Loop
   while (av_read_frame(in_fmt, pkt) >= 0) {
     if (pkt->stream_index == vid_idx) {
       if (avcodec_send_packet(dec_ctx, pkt) == 0) {
         while (avcodec_receive_frame(dec_ctx, raw_frame) == 0) {
           
           // A. Lazy Init Scaler (Format Conversion)
           if (!sws) {
             sws = sws_getContext(raw_frame->width, raw_frame->height, raw_frame->format,
                                  width, height, AV_PIX_FMT_YUV420P,
                                  SWS_BILINEAR, NULL, NULL, NULL);
           }
 
           // B. Copy input video to work frame (Convert to YUV420P)
           // This keeps the original video image!
           sws_scale(sws, (const uint8_t* const*)raw_frame->data, raw_frame->linesize,
                     0, raw_frame->height, work_frame->data, work_frame->linesize);
 
           // C. Update Smoke Physics
           for (int k = 0; k < EMIT_RATE; k++) spawn_particle(width / 2, height - 50);
           update_particles();
 
           // D. Draw Smoke ON TOP of the video image
           // (We do NOT use memset to clear the frame here)
           draw_smoke_overlay(work_frame);
 
           // E. Encode
           encode_write(enc_ctx, out_fmt, st, work_frame, &pts);
           
           if (pts % 30 == 0) printf("Processed: %ld\r", pts);
         }
       }
     }
     av_packet_unref(pkt);
   }
 
   // Flush
   encode_write(enc_ctx, out_fmt, st, NULL, &pts);
   av_write_trailer(out_fmt);
 
   // Clean
   if (sws) sws_freeContext(sws);
   avcodec_free_context(&dec_ctx);
   avcodec_free_context(&enc_ctx);
   avformat_close_input(&in_fmt);
   if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt->pb);
   avformat_free_context(out_fmt);
   av_frame_free(&raw_frame);
   av_frame_free(&work_frame);
   av_packet_free(&pkt);
 
   printf("\nDone.\n");
   return 0;
 }