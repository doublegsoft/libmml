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
#define INPUT_FILENAME "../../data/V3.mp4"
#define OUTPUT_FILENAME "output_quad.mp4"

static void 
draw_moving_quad(AVFrame* frame, double time_sec) {
  int width = frame->width;
  int height = frame->height;

  // --- 1. 计算中心点 (位移) ---
  // 与椭圆类似的平滑运动轨迹
  int center_x = width / 2;
  int center_y = height / 2;
  int range_x = width / 3;
  int range_y = height / 4;

  // 使用不同的相位速度，让它和椭圆错开
  int cx = center_x + (int)(range_x * sin(time_sec * 1.2)); 
  int cy = center_y + (int)(range_y * cos(time_sec * 1.7));

  // --- 2. 计算大小与旋转 ---
  // 基础半径 (四边形中心到顶点的距离)
  double base_radius = width / 10.0;
  
  // 呼吸效果：半径随时间在 0.8x 到 1.2x 之间波动
  double pulse = 1.0 + 0.2 * sin(time_sec * 4.0);
  double current_radius = base_radius * pulse;

  // 旋转角度：每秒转 1 弧度
  double theta = time_sec * 1.0;

  // --- 3. 计算四个顶点 (旋转矩形) ---
  // 我们生成一个矩形，四个角相对于中心的偏移分别是 0, 90, 180, 270 度
  // 加上 theta 实现旋转
  
  // Point 1
  int x1 = cx + (int)(current_radius * cos(theta));
  int y1 = cy + (int)(current_radius * sin(theta));

  // Point 2 (+90度)
  int x2 = cx + (int)(current_radius * cos(theta + M_PI / 2.0));
  int y2 = cy + (int)(current_radius * sin(theta + M_PI / 2.0));

  // Point 3 (+180度)
  int x3 = cx + (int)(current_radius * cos(theta + M_PI));
  int y3 = cy + (int)(current_radius * sin(theta + M_PI));

  // Point 4 (+270度)
  int x4 = cx + (int)(current_radius * cos(theta + 3.0 * M_PI / 2.0));
  int y4 = cy + (int)(current_radius * sin(theta + 3.0 * M_PI / 2.0));

  // --- 4. 颜色定义 ---
  // 青色/蓝绿色 (Cyan/Teal)
  // YUV 大致值: Y=170, U=166, V=16
  uint8_t col_y = 170;
  uint8_t col_u = 166;
  uint8_t col_v = 16;

  // --- 5. 绘制 ---
  // 线宽 4，透明度 0.6
  mml_shape_quad(frame, 
                 x1, y1, x2, y2, x3, y3, x4, y4, 
                 -1, 
                 col_y, col_u, col_v, 
                 0.6f);
}


// --- Main Program ---

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
        double current_time = frame_count * time_per_frame;
        draw_moving_quad(frame, current_time);

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