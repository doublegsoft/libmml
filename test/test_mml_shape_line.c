#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <math.h>
#include <stdio.h>

#include "libmml-shape.h"
#include "libmml-frame.h"
#include "libmml-video.h"

// --- Configuration ---
#define INPUT_FILENAME "../../data/OFFSIDE_SLOW.mp4"
#define OUTPUT_FILENAME "output_line.mp4"


/**
 * @brief 在特定时间段内绘制线条。
 * 
 * @param frame       当前视频帧。
 * @param curr_time   当前帧的时间戳 (秒)。
 * @param start_time  线条开始出现的时间 (秒)。
 * @param duration    线条持续的总时间 (秒)。
 * @param fade_time   淡入/淡出的时间 (秒)，建议 0.3 ~ 0.5。
 * @param x1, y1      起点。
 * @param x2, y2      终点。
 * @param thickness   线宽。
 * @param y, u, v     颜色。
 */
void 
mml_effect_timed_line(AVFrame* frame, 
                      double curr_time, 
                      double start_time, 
                      double duration, 
                      double fade_time,
                      int x1, int y1, int x2, int y2, 
                      int thickness,
                      uint8_t y_val, uint8_t u_val, uint8_t v_val) 
{
  double end_time = start_time + duration;

  // 1. 检查时间范围：如果不在显示期内，直接返回
  if (curr_time < start_time || curr_time > end_time) {
    return; 
  }

  // 2. 计算透明度 (Alpha) 用于淡入淡出
  float alpha = 0.3f;
  
  // 计算相对于特效开始的时间
  double t = curr_time - start_time;
  
  // 淡入阶段 (Fade In)
  // if (t < fade_time) {
  //   alpha = (float)(t / fade_time);
  // }
  // // 淡出阶段 (Fade Out)
  // else if (curr_time > (end_time - fade_time)) {
  //   double time_left = end_time - curr_time;
  //   alpha = (float)(time_left / fade_time);
  // }

  mml_shape_line(frame, 
                 x1, y1, x2, y2, 
                 thickness, 
                 y_val, u_val, v_val, 
                 alpha);
}

int main(int argc, char** argv) {
  AVFormatContext* ifmt_ctx = NULL;
  AVFormatContext* ofmt_ctx = NULL;
  AVCodecContext* dec_ctx = NULL;
  AVCodecContext* enc_ctx = NULL;
  
  AVStream* out_stream = NULL;
  
  int video_idx = -1;
  double fps = 0.0;
  int ret = 0;
  
  // Data containers
  AVFrame* frame = NULL;
  AVPacket* pkt = NULL;

  int rc = mml_video_load(INPUT_FILENAME, 
                          &ifmt_ctx,
                          &dec_ctx,
                          &video_idx,
                          OUTPUT_FILENAME,
                          &ofmt_ctx,
                          &enc_ctx,
                          &out_stream);
  fps = av_q2d(enc_ctx->framerate);     
  avformat_write_header(ofmt_ctx, NULL);                   

  // 4. Processing Loop
  frame = av_frame_alloc();
  pkt = av_packet_alloc();
  
  if (fps < 1.0) fps = 25.0; 
  double time_per_frame = 1.0 / fps;
  
  long long frame_count = 0; // Monotonic counter for smooth animation

  printf("processing: %dx%d @ %.2f fps\n", dec_ctx->width, dec_ctx->height, fps);

  while (av_read_frame(ifmt_ctx, pkt) >= 0) 
  {
    if (pkt->stream_index == video_idx) 
    {
      ret = avcodec_send_packet(dec_ctx, pkt);
      if (ret < 0) break;

      while (ret >= 0) 
      {
        // Get raw frame from decoder
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        else if (ret < 0) goto cleanup;

        if (av_frame_make_writable(frame) < 0) goto cleanup;
        double current_seconds = (double)frame_count / fps;

        // -------------------------------------------------------
        // 场景：在第 2 秒时，画一条红线，持续 3 秒
        // -------------------------------------------------------
        // Start: 2.0s
        // Duration: 3.0s (End at 5.0s)
        // Fade: 0.5s (淡入淡出各 0.5秒)
        // Color: Red (76, 84, 255)
        // Coords: (100,100) -> (500,500)
        // Thickness: 5px

        mml_effect_timed_line(frame, 
                              current_seconds, 
                              8.5, 8.0, 0.5,    // Timing
                              0, 848, 1920, 775, // Geometry
                              8,                // Thickness
                              76, 84, 255);     // Color

        frame->pts = frame_count; 
        if (mml_frame_write(enc_ctx, ofmt_ctx, out_stream, frame) < 0) goto cleanup;
        
        frame_count++;
        printf("\rProcessed Frame: %lld", frame_count);
        fflush(stdout);
      }
    }
    av_packet_unref(pkt);
  }

  mml_frame_write(enc_ctx, ofmt_ctx, out_stream, NULL);
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