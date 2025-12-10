/**
 * FFmpeg C API: Image Pixelation (Mosaic) Effect
 * 
 * Logic:
 * 1. Load Image.
 * 2. Loop frames.
 * 3. Copy Original -> Work Frame.
 * 4. Apply Pixelate In-Place on Work Frame.
 *    (Animation: Block size goes from Large -> 1).
 * 
 * Compile:
 * gcc pixelate.c -o pixelate -lavformat -lavcodec -lavutil -lswscale
 */

 #include <stdio.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/imgutils.h>
 #include <libavutil/opt.h>
 #include <libswscale/swscale.h>
 
 #define OUT_FILE "pixelate_output.mp4"
 #define FPS 30
 #define DURATION 4 // Seconds
 
 // -----------------------------------------------------------------------------
 // Helper: Process a single plane (Generic Box Fill)
 // -----------------------------------------------------------------------------
 static void 
 pixelate_plane(uint8_t* data, int linesize, int width, int height, int block_size) 
 {
   if (block_size <= 1) return; // No pixelation needed
 
   for (int y = 0; y < height; y += block_size) {
     for (int x = 0; x < width; x += block_size) {
       
       // 1. Calculate actual block dimensions (handle edges)
       int bw = block_size;
       int bh = block_size;
       if (x + bw > width)  bw = width - x;
       if (y + bh > height) bh = height - y;
 
       // 2. Calculate Average Value of the block
       // (Optimization: You could just pick top-left pixel for speed, 
       // but averaging looks smoother/less noisy).
       unsigned int sum = 0;
       for (int by = 0; by < bh; by++) {
         uint8_t* row = data + ((y + by) * linesize);
         for (int bx = 0; bx < bw; bx++) {
           sum += row[x + bx];
         }
       }
       uint8_t avg = (uint8_t)(sum / (bw * bh));
 
       // 3. Fill the block with average color
       for (int by = 0; by < bh; by++) {
         uint8_t* row = data + ((y + by) * linesize);
         memset(row + x, avg, bw);
       }
     }
   }
 }
 
 // -----------------------------------------------------------------------------
 // Core: Pixelate Frame (YUV420P Aware)
 // -----------------------------------------------------------------------------
 /**
  * @brief Applies a mosaic/pixelate effect to a frame in-place.
  * 
  * @param frame      The frame to modify (YUV420P).
  * @param block_size Size of the pixel block (e.g., 16). 
  *                   Must be even. If 1 or 0, no effect.
  */
 void 
 mml_frame_pixelate(AVFrame* frame, int block_size) 
 {
   // Safety: YUV420P requires even block sizes for chroma alignment
   if (block_size < 2) return;
   
   // Force even block size (round down)
   if (block_size % 2 != 0) block_size--; 
 
   // 1. Process Luma (Y)
   pixelate_plane(frame->data[0], frame->linesize[0], 
                  frame->width, frame->height, block_size);
 
   // 2. Process Chroma (U/V)
   // Dimensions and Block Size are halved
   int uv_w = frame->width / 2;
   int uv_h = frame->height / 2;
   int uv_block = block_size / 2;
 
   pixelate_plane(frame->data[1], frame->linesize[1], uv_w, uv_h, uv_block);
   pixelate_plane(frame->data[2], frame->linesize[2], uv_w, uv_h, uv_block);
 }
 
 // -----------------------------------------------------------------------------
 // Standard Boilerplate (Load, Encode, Main)
 // -----------------------------------------------------------------------------
 int load_image(const char* fname, AVFrame** out) {
   AVFormatContext* fmt = NULL;
   if (avformat_open_input(&fmt, fname, NULL, NULL) < 0) return -1;
   avformat_find_stream_info(fmt, NULL);
   const AVCodec* dec = NULL;
   int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
   AVCodecContext* ctx = avcodec_alloc_context3(dec);
   avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar);
   avcodec_open2(ctx, dec, NULL);
   AVPacket* pkt = av_packet_alloc();
   AVFrame* raw = av_frame_alloc();
   int ret = -1;
   while (av_read_frame(fmt, pkt) >= 0) {
     if (pkt->stream_index == idx) {
       if (avcodec_send_packet(ctx, pkt) == 0) {
         if (avcodec_receive_frame(ctx, raw) == 0) {
           *out = av_frame_alloc();
           (*out)->format = AV_PIX_FMT_YUV420P;
           (*out)->width = raw->width;
           (*out)->height = raw->height;
           av_frame_get_buffer(*out, 32);
           struct SwsContext* sws = sws_getContext(raw->width, raw->height, raw->format,
             raw->width, raw->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
           sws_scale(sws, (const uint8_t* const*)raw->data, raw->linesize, 0, raw->height, (*out)->data, (*out)->linesize);
           sws_freeContext(sws);
           ret = 0; break;
         }
       }
     }
     av_packet_unref(pkt);
   }
   av_frame_free(&raw); av_packet_free(&pkt); avcodec_free_context(&ctx); avformat_close_input(&fmt);
   return ret;
 }
 
 int encode_write(AVCodecContext* enc, AVFormatContext* fmt, AVStream* st, AVFrame* frame, int64_t pts) {
   if (frame) {
     if (av_frame_make_writable(frame) < 0) return -1;
     frame->pts = pts;
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
 
 int main(int argc, char* argv[]) {
   if (argc < 2) { printf("Usage: %s <image>\n", argv[0]); return 1; }
 
   AVFrame* base_frame = NULL;
   if (load_image(argv[1], &base_frame) < 0) return 1;
 
   AVFormatContext* out_fmt = NULL;
   avformat_alloc_output_context2(&out_fmt, NULL, NULL, OUT_FILE);
   const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
   AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
   enc_ctx->width = base_frame->width;
   enc_ctx->height = base_frame->height;
   enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
   enc_ctx->time_base = (AVRational){1, FPS};
   enc_ctx->framerate = (AVRational){FPS, 1};
   if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER) enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
   avcodec_open2(enc_ctx, enc, NULL);
   AVStream* out_st = avformat_new_stream(out_fmt, NULL);
   avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
   out_st->time_base = enc_ctx->time_base;
   avio_open(&out_fmt->pb, OUT_FILE, AVIO_FLAG_WRITE);
   avformat_write_header(out_fmt, NULL);
 
   AVFrame* work_frame = av_frame_alloc();
   work_frame->format = AV_PIX_FMT_YUV420P;
   work_frame->width = base_frame->width;
   work_frame->height = base_frame->height;
   av_frame_get_buffer(work_frame, 32);
 
   int total_frames = FPS * DURATION;
   int max_block_size = 60; // Start with 60px blocks
 
   printf("Generating Pixelation... Output: %s\n", OUT_FILE);
 
   for (int i = 0; i < total_frames; i++) {
     
     // --- ANIMATION LOGIC ---
     // Start: High Pixelation (Blurry)
     // End:   No Pixelation (Clear)
     float progress = (float)i / total_frames; 
     
     // Ease-out curve (fast clear at start, slow polish at end)
     int current_size = (int)(max_block_size * (1.0f - progress));
     
     // 1. Reset Canvas (Copy clean image to work frame)
     av_frame_copy(work_frame, base_frame);
 
     // 2. Apply Pixelate In-Place
     mml_frame_pixelate(work_frame, current_size);
 
     encode_write(enc_ctx, out_fmt, out_st, work_frame, i);
     if (i % 15 == 0) printf("Frame %d | Block Size: %d\n", i, current_size);
   }
 
   encode_write(enc_ctx, out_fmt, out_st, NULL, total_frames);
   av_write_trailer(out_fmt);
   av_frame_free(&base_frame);
   av_frame_free(&work_frame);
   avcodec_free_context(&enc_ctx);
   avio_closep(&out_fmt->pb);
   avformat_free_context(out_fmt);
 
   printf("Done.\n");
   return 0;
 }