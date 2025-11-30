#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> // 必须包含数学库

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "libmml-image.h"
#include "libmml-blend.h"
#include "libmml-frame.h"
#include "libmml-text.h"
#include "libmml-error.h"
#include "libmml-video.h"
#include "libmml-util.h"


// --- 核心处理流程 ---
int main(int argc, char** argv) 
{
  const char* in_filename = "../../data/V3.10.mp4";
  const char* out_filename = "text-ltr.mp4";
  const char* font_filename = "../../data/playwrite.ttf";

  mml_fontctx_t* font_ctx = mml_font_init(font_filename);
  if (!font_ctx) {
    fprintf(stderr, "Failed to load font: %s\n", font_filename);
    return 1;
  }

  AVFormatContext* ifmt_ctx = NULL;
  AVFormatContext* ofmt_ctx = NULL;
  AVCodecContext* dec_ctx = NULL;
  AVCodecContext* enc_ctx = NULL;
  
  AVStream* out_stream = NULL;
  
  int video_idx = -1;
  double fps = 0.0;

  int rc = mml_video_load(in_filename, 
                          &ifmt_ctx,
                          &dec_ctx,
                          out_filename,
                          &ofmt_ctx,
                          &enc_ctx,
                          &out_stream,
                          &video_idx);
  fps = av_q2d(enc_ctx->framerate); 

  if (fps < 1.0) fps = 25.0; 
  double time_per_frame = 1.0 / fps;
  
  // 6. 数据包处理循环
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  int ret;
  int frame_count = 0;

  printf("Start processing...\n");

  while (av_read_frame(ifmt_ctx, pkt) >= 0) 
  {
    // 只处理视频流，忽略音频
    if (pkt->stream_index != video_idx) 
      continue;
    // 发送数据包给解码器
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) break;

    while (ret >= 0) 
    {
      ret = avcodec_receive_frame(dec_ctx, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
      else if (ret < 0) { fprintf(stderr, "Error decoding\n"); exit(1); }

      av_frame_make_writable(frame);

      // 2. 准备文字
      char text_buf[128];
      // 计算秒数
      double pts_sec = (double)frame->pts * av_q2d(ifmt_ctx->streams[video_idx]->time_base);
      sprintf(text_buf, "Frame: %d | Time: %.2fs", frame_count++, pts_sec);

      double current_time = frame_count * time_per_frame;
      frame->pts = frame_count; 

      // 3. 绘制文字 (白色，坐标 50,50，大小 48)
      // mml_text_ltr(frame, font_ctx, text_buf, 50, 50, 128.0f, 255, 255, 0);

      mml_frame_write(enc_ctx, ofmt_ctx, out_stream, frame);
      // avcodec_send_frame(enc_ctx, frame);
      
      // while (1) 
      // {
      //   AVPacket* enc_pkt = av_packet_alloc();
      //   int enc_ret = avcodec_receive_packet(enc_ctx, enc_pkt);
      //   if (enc_ret == AVERROR(EAGAIN) || enc_ret == AVERROR_EOF) {
      //     av_packet_free(&enc_pkt);
      //     break;
      //   }

      //   // 转换时间戳：Encoder TimeBase -> Output Stream TimeBase
      //   av_packet_rescale_ts(enc_pkt, ifmt_ctx->streams[video_idx]->time_base, out_stream->time_base);
      //   enc_pkt->stream_index = out_stream->index;

      //   printf("Write frame %d (pts:%lld)\r", frame_count, enc_pkt->pts);
      //   fflush(stdout);

      //   av_interleaved_write_frame(ofmt_ctx, enc_pkt);
      //   av_packet_free(&enc_pkt);
      // }
      av_frame_unref(frame); // 清理引用，为下一帧做准备
    }
    av_packet_unref(pkt);
  }

  mml_frame_write(enc_ctx, ofmt_ctx, out_stream, NULL);
  // 7. 刷新编码器 (Flush Encoder)
  // avcodec_send_frame(enc_ctx, NULL);
  // while (1) {
  //   AVPacket* enc_pkt = av_packet_alloc();
  //   if (avcodec_receive_packet(enc_ctx, enc_pkt) < 0) {
  //     av_packet_free(&enc_pkt);
  //     break;
  //   }
  //   av_packet_rescale_ts(enc_pkt, ifmt_ctx->streams[video_idx]->time_base, out_stream->time_base);
  //   enc_pkt->stream_index = out_stream->index;
  //   av_interleaved_write_frame(ofmt_ctx, enc_pkt);
  //   av_packet_free(&enc_pkt);
  // }

  // 8. 写入文件尾部
  av_write_trailer(ofmt_ctx);

  // 9. 清理所有资源
  printf("\nDone.\n");
  av_packet_free(&pkt);
  av_frame_free(&frame);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&ifmt_ctx);
  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);
  mml_font_free(font_ctx);
  return 0;
}