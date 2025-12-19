/**
 * FFmpeg C API: Draw Transparent Rectangle
 * 
 * Logic:
 * 1. Decode Video.
 * 2. Convert to YUV420P.
 * 3. Draw Rect with Alpha Blending.
 * 4. Encode.
 *
 * Compile:
 * gcc draw_alpha.c -o draw_alpha -lavformat -lavcodec -lavutil -lswscale
 */

 #include <stdio.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/imgutils.h>
 #include <libavutil/opt.h>
 #include <libswscale/swscale.h>
 
 #define OUT_FILE "alpha_output.mp4"
 
// Function Definition (as above)
void mml_draw_rect_alpha(AVFrame* frame, int x, int y, int w, int h, 
                        int cy, int cu, int cv, float alpha) 
{
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > frame->width) w = frame->width - x;
  if (y + h > frame->height) h = frame->height - y;
  if (w <= 0 || h <= 0) return;

  int a_scale = (int)(alpha * 256);
  int inv_scale = 256 - a_scale;

  // Y Plane
  for (int cur_y = y; cur_y < y + h; cur_y++) {
    uint8_t* row = frame->data[0] + (cur_y * frame->linesize[0]);
    for (int cur_x = x; cur_x < x + w; cur_x++) {
      int bg = row[cur_x];
      row[cur_x] = (uint8_t)((cy * a_scale + bg * inv_scale) >> 8);
    }
  }

  // UV Planes
  int uv_x = x / 2; int uv_y = y / 2;
  int uv_w = w / 2; int uv_h = h / 2;

  for (int cur_y = uv_y; cur_y < uv_y + uv_h; cur_y++) {
    uint8_t* row_u = frame->data[1] + (cur_y * frame->linesize[1]);
    uint8_t* row_v = frame->data[2] + (cur_y * frame->linesize[2]);
    for (int cur_x = uv_x; cur_x < uv_x + uv_w; cur_x++) {
      row_u[cur_x] = (uint8_t)((cu * a_scale + row_u[cur_x] * inv_scale) >> 8);
      row_v[cur_x] = (uint8_t)((cv * a_scale + row_v[cur_x] * inv_scale) >> 8);
    }
  }
}

// Helper: Encode
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
  if (argc < 2) { printf("Usage: %s <input>\n", argv[0]); return 1; }

  // 1. Input
  AVFormatContext* in_fmt = NULL;
  avformat_open_input(&in_fmt, argv[1], NULL, NULL);
  avformat_find_stream_info(in_fmt, NULL);
  int vid_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVCodecContext* dec_ctx = avcodec_alloc_context3(NULL);
  avcodec_parameters_to_context(dec_ctx, in_fmt->streams[vid_idx]->codecpar);
  const AVCodec* dec = avcodec_find_decoder(dec_ctx->codec_id);
  avcodec_open2(dec_ctx, dec, NULL);

  // 2. Output
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

  // 3. Loop
  AVPacket* pkt = av_packet_alloc();
  AVFrame* dec_frame = av_frame_alloc();
  AVFrame* work_frame = av_frame_alloc();
  struct SwsContext* sws = NULL;
  int64_t pts = 0;
  int init = 0;

  printf("Drawing Transparent Box... Output: %s\n", OUT_FILE);

  while (av_read_frame(in_fmt, pkt) >= 0) {
    if (pkt->stream_index == vid_idx) {
      avcodec_send_packet(dec_ctx, pkt);
      while (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
        
        if (!init) {
          sws = sws_getContext(dec_frame->width, dec_frame->height, dec_frame->format,
                              dec_frame->width, dec_frame->height, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, NULL, NULL, NULL);
          work_frame->format = AV_PIX_FMT_YUV420P;
          work_frame->width = dec_frame->width;
          work_frame->height = dec_frame->height;
          av_frame_get_buffer(work_frame, 32);
          init = 1;
        }

        // Convert to safe YUV420P
        sws_scale(sws, (const uint8_t* const*)dec_frame->data, dec_frame->linesize,
                  0, dec_frame->height, work_frame->data, work_frame->linesize);

        av_frame_make_writable(work_frame);

        // --- DRAWING ---
        // Black Box (Y=0, U=128, V=128) with 50% opacity
        // Center of screen, 400x300 size
        int x = (work_frame->width - 400) / 2;
        int y = (work_frame->height - 300) / 2;
        
        mml_draw_rect_alpha(work_frame, x, y, 400, 300, 
                            0, 128, 128,  // Color: Black
                            0.5f);        // Alpha: 50%

        // Draw a Red Box (Y=76, U=84, V=255) with 70% opacity
        mml_draw_rect_alpha(work_frame, 50, 50, 200, 200,
                            76, 84, 255, 0.7f);

        encode_write(enc_ctx, out_fmt, out_st, work_frame, pts++);
        if (pts % 30 == 0) printf("Frame: %ld\r", pts);
      }
    }
    av_packet_unref(pkt);
  }

  encode_write(enc_ctx, out_fmt, out_st, NULL, pts);
  av_write_trailer(out_fmt);

  // Cleanup
  if (sws) sws_freeContext(sws);
  av_frame_free(&work_frame);
  av_frame_free(&dec_frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&in_fmt);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("\nDone.\n");
  return 0;
}