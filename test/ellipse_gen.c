#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <math.h>
#include <stdio.h>

// --- Configuration ---
#define INPUT_FILENAME "../../data/V3.mp4"
#define OUTPUT_FILENAME "output_ellipse.mp4"

// --- Drawing Helper Functions ---

// Helper: Safely write a pixel to a YUV420P frame
// Checks bounds and handles chroma subsampling (2x2 block)
void put_pixel_yuv(AVFrame* frame, int x, int y, uint8_t y_val, uint8_t u_val, uint8_t v_val) {
  // 1. Draw Luma (Y)
  if (x >= 0 && x < frame->width && y >= 0 && y < frame->height) {
    frame->data[0][y * frame->linesize[0] + x] = y_val;
  }

  // 2. Draw Chroma (U/V) - only update for even coordinates to avoid overdraw
  if (x % 2 == 0 && y % 2 == 0) {
    int uv_x = x / 2;
    int uv_y = y / 2;
    if (uv_x >= 0 && uv_x < frame->width / 2 && uv_y >= 0 && uv_y < frame->height / 2) {
      frame->data[1][uv_y * frame->linesize[1] + uv_x] = u_val;
      frame->data[2][uv_y * frame->linesize[2] + uv_x] = v_val;
    }
  }
}

// Logic: Calculate ellipse position based on TIME (seconds), not raw PTS
void draw_moving_ellipse(AVFrame* frame, double time_sec) {
  int width = frame->width;
  int height = frame->height;

  // Ellipse Dimensions
  int rx = width / 12; // Radius X
  int ry = height / 12; // Radius Y

  // --- SMOOTH MOVEMENT MATH ---
  // Using time_sec ensures smooth 60fps or 24fps movement.
  // sin(time * speed): Speed 1.0 = 1 radian per second.
  
  // X oscillates left-right
  int center_x = width / 2;
  int range_x = width / 3;
  int cx = center_x + (int)(range_x * sin(time_sec * 1.5));

  // Y oscillates up-down (different speed for Lissajous effect)
  int center_y = height / 2;
  int range_y = height / 4;
  int cy = center_y + (int)(range_y * cos(time_sec * 2.0));

  // Color: Bright Red (Y=81, U=90, V=240)
  uint8_t col_y = 81;
  uint8_t col_u = 90;
  uint8_t col_v = 240;

  // Bounding box optimization (don't scan the whole image)
  int min_x = FFMAX(0, cx - rx);
  int max_x = FFMIN(width, cx + rx);
  int min_y = FFMAX(0, cy - ry);
  int max_y = FFMIN(height, cy + ry);

  for (int y = min_y; y < max_y; y++) {
    for (int x = min_x; x < max_x; x++) {
      // Ellipse equation check
      double val = pow((double)(x - cx) / rx, 2) + pow((double)(y - cy) / ry, 2);
      if (val <= 1.0) {
        put_pixel_yuv(frame, x, y, col_y, col_u, col_v);
      }
    }
  }
}

// --- Encoding Helper ---

static int encode_and_write(AVCodecContext* enc_ctx, AVFormatContext* ofmt_ctx, AVStream* out_stream, AVFrame* frame) {
  int ret;

  // 1. Send frame to encoder
  ret = avcodec_send_frame(enc_ctx, frame);
  if (ret < 0) return ret;

  // 2. Receive packets
  while (ret >= 0) {
    AVPacket* pkt = av_packet_alloc();
    ret = avcodec_receive_packet(enc_ctx, pkt);
    
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      av_packet_free(&pkt);
      break;
    } else if (ret < 0) {
      av_packet_free(&pkt);
      return ret;
    }

    // Rescale timestamps for output (Encoder Timebase -> Stream Timebase)
    av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
    pkt->stream_index = out_stream->index;

    // Write
    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    av_packet_free(&pkt);
    
    if (ret < 0) return ret;
  }
  return 0;
}

// --- Main Program ---

int main(int argc, char** argv) {
  AVFormatContext* ifmt_ctx = NULL;
  AVFormatContext* ofmt_ctx = NULL;
  AVCodecContext* dec_ctx = NULL;
  AVCodecContext* enc_ctx = NULL;
  
  const AVCodec* decoder = NULL;
  const AVCodec* encoder = NULL;
  AVStream* in_stream = NULL;
  AVStream* out_stream = NULL;
  
  int video_idx = -1;
  int ret = 0;
  
  // Data containers
  AVFrame* frame = NULL;
  AVPacket* pkt = NULL;

  // 1. Open Input
  if (avformat_open_input(&ifmt_ctx, INPUT_FILENAME, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open input file '%s'\n", INPUT_FILENAME);
    return 1;
  }

  if (avformat_find_stream_info(ifmt_ctx, NULL) < 0) {
    fprintf(stderr, "Could not find stream info\n");
    return 1;
  }

  // Find Video Stream
  video_idx = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
  if (video_idx < 0) {
    fprintf(stderr, "No video stream found\n");
    return 1;
  }
  in_stream = ifmt_ctx->streams[video_idx];

  // 2. Setup Decoder
  dec_ctx = avcodec_alloc_context3(decoder);
  avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
  
  if (avcodec_open2(dec_ctx, decoder, NULL) < 0) {
    fprintf(stderr, "Failed to open decoder\n");
    return 1;
  }

  // 3. Setup Output
  avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, OUTPUT_FILENAME);
  if (!ofmt_ctx) {
    fprintf(stderr, "Failed to allocate output context\n");
    return 1;
  }

  // Find H.264 Encoder
  encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!encoder) {
    fprintf(stderr, "H.264 encoder not found\n");
    return 1;
  }

  out_stream = avformat_new_stream(ofmt_ctx, NULL);
  enc_ctx = avcodec_alloc_context3(encoder);

  // Copy basic props from decoder to encoder
  enc_ctx->height = dec_ctx->height;
  enc_ctx->width = dec_ctx->width;
  enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
  // Use generic YUV420P for H.264 compatibility
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  // Timebase: 1/FPS
  enc_ctx->time_base = av_inv_q(dec_ctx->framerate); 
  out_stream->time_base = enc_ctx->time_base;

  // Optimization
  if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  
  av_opt_set(enc_ctx->priv_data, "preset", "fast", 0);

  if (avcodec_open2(enc_ctx, encoder, NULL) < 0) {
    fprintf(stderr, "Cannot open encoder\n");
    return 1;
  }
  
  avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);

  // Open output file
  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&ofmt_ctx->pb, OUTPUT_FILENAME, AVIO_FLAG_WRITE) < 0) {
      fprintf(stderr, "Could not open output file '%s'\n", OUTPUT_FILENAME);
      return 1;
    }
  }

  if (avformat_write_header(ofmt_ctx, NULL) < 0) return 1;

  // 4. Processing Loop
  frame = av_frame_alloc();
  pkt = av_packet_alloc();
  
  // Calculate FPS for smooth animation
  double fps = av_q2d(in_stream->avg_frame_rate);
  if (fps < 1.0) fps = 25.0; // Fallback
  double time_per_frame = 1.0 / fps;
  
  long long frame_count = 0; // Monotonic counter for smooth animation

  printf("Processing: %dx%d @ %.2f fps\n", dec_ctx->width, dec_ctx->height, fps);

  while (av_read_frame(ifmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == video_idx) {
      
      // Send compressed packet to decoder
      ret = avcodec_send_packet(dec_ctx, pkt);
      if (ret < 0) break;

      while (ret >= 0) {
        // Get raw frame from decoder
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        else if (ret < 0) goto cleanup;

        // A. Make frame writable (FFmpeg might reuse memory)
        if (av_frame_make_writable(frame) < 0) goto cleanup;

        // B. Calculate smooth time
        double current_time = frame_count * time_per_frame;

        // C. Draw Ellipse
        draw_moving_ellipse(frame, current_time);

        // D. Update PTS for Encoder
        // We overwrite the PTS to ensure strictly increasing timestamps
        frame->pts = frame_count; 

        // E. Encode & Write
        if (encode_and_write(enc_ctx, ofmt_ctx, out_stream, frame) < 0) goto cleanup;
        
        frame_count++;
        printf("\rProcessed Frame: %lld", frame_count);
        fflush(stdout);
      }
    }
    av_packet_unref(pkt);
  }

  // 5. Flush Encoder (process remaining frames)
  encode_and_write(enc_ctx, ofmt_ctx, out_stream, NULL);
  av_write_trailer(ofmt_ctx);
  printf("\nDone!\n");

cleanup:
  if (frame) av_frame_free(&frame);
  if (pkt) av_packet_free(&pkt);
  if (ifmt_ctx) avformat_close_input(&ifmt_ctx);
  if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&ofmt_ctx->pb);
  if (ofmt_ctx) avformat_free_context(ofmt_ctx);
  if (dec_ctx) avcodec_free_context(&dec_ctx);
  if (enc_ctx) avcodec_free_context(&enc_ctx);

  return 0;
}