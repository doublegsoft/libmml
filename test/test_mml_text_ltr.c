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
  if (avformat_open_input(&ifmt_ctx, in_filename, NULL, NULL) < 0) {
    fprintf(stderr, "Could not open input file\n");
    return 1;
  }
  if (avformat_find_stream_info(ifmt_ctx, NULL) < 0) return 1;

  // 找到视频流
  int video_idx = -1;
  for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
    if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = i;
      break;
    }
  }
  if (video_idx == -1) return 1;

  // 3. 准备解码器
  AVCodecParameters* codecpar_in = ifmt_ctx->streams[video_idx]->codecpar;
  const AVCodec* dec = avcodec_find_decoder(codecpar_in->codec_id);
  AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
  avcodec_parameters_to_context(dec_ctx, codecpar_in);
  avcodec_open2(dec_ctx, dec, NULL);

  // 4. 准备输出文件
  AVFormatContext* ofmt_ctx = NULL;
  avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
  if (!ofmt_ctx) return 1;

  // 5. 准备编码器 (H.264)
  const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVStream* out_stream = avformat_new_stream(ofmt_ctx, NULL);
  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);

  // 配置编码参数 (与输入保持一致)
  enc_ctx->height = dec_ctx->height;
  enc_ctx->width = dec_ctx->width;
  enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P; // 强制输出格式
  enc_ctx->time_base = (AVRational){1, 25}; // 假设 25fps，实际应读取源视频
  enc_ctx->framerate = (AVRational){25, 1};
  
  // 复制输入流的时间基，这很重要
  out_stream->time_base = ifmt_ctx->streams[video_idx]->time_base;

  if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  avcodec_open2(enc_ctx, enc, NULL);
  avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);

  // 打开输出文件 IO
  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE) < 0) {
      fprintf(stderr, "Could not open output file\n");
      return 1;
    }
  }
  avformat_write_header(ofmt_ctx, NULL);

  // 6. 数据包处理循环
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  int ret;
  int frame_count = 0;

  printf("Start processing...\n");

  while (av_read_frame(ifmt_ctx, pkt) >= 0) {
    // 只处理视频流，忽略音频
    if (pkt->stream_index == video_idx) {
      
      // 发送数据包给解码器
      ret = avcodec_send_packet(dec_ctx, pkt);
      if (ret < 0) break;

      while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        else if (ret < 0) { fprintf(stderr, "Error decoding\n"); exit(1); }

        // --- 关键步骤：修改画面 ---
        
        // 1. 确保帧可写 (Decoded frames are read-only reference counted)
        av_frame_make_writable(frame);

        // 2. 准备文字
        char text_buf[128];
        // 计算秒数
        double pts_sec = (double)frame->pts * av_q2d(ifmt_ctx->streams[video_idx]->time_base);
        sprintf(text_buf, "Frame: %d | Time: %.2fs", frame_count++, pts_sec);

        // 3. 绘制文字 (白色，坐标 50,50，大小 48)
        mml_text_ltr(frame, font_ctx, text_buf, 50, 50, 128.0f, 255, 255, 0);

        // --- 结束修改 ---

        // 发送 Frame 给编码器
        // 重要：这里直接使用解码出来的 PTS，编码器会处理
        // 但有时候编码器的时间基和流不同，保险起见可以转换，但这里简化处理
        avcodec_send_frame(enc_ctx, frame);
        
        while (1) {
          AVPacket* enc_pkt = av_packet_alloc();
          int enc_ret = avcodec_receive_packet(enc_ctx, enc_pkt);
          if (enc_ret == AVERROR(EAGAIN) || enc_ret == AVERROR_EOF) {
            av_packet_free(&enc_pkt);
            break;
          }

          // 转换时间戳：Encoder TimeBase -> Output Stream TimeBase
          av_packet_rescale_ts(enc_pkt, ifmt_ctx->streams[video_idx]->time_base, out_stream->time_base);
          enc_pkt->stream_index = out_stream->index;

          printf("Write frame %d (pts:%lld)\r", frame_count, enc_pkt->pts);
          fflush(stdout);

          av_interleaved_write_frame(ofmt_ctx, enc_pkt);
          av_packet_free(&enc_pkt);
        }
        av_frame_unref(frame); // 清理引用，为下一帧做准备
      }
    }
    av_packet_unref(pkt);
  }

  // 7. 刷新编码器 (Flush Encoder)
  avcodec_send_frame(enc_ctx, NULL);
  while (1) {
    AVPacket* enc_pkt = av_packet_alloc();
    if (avcodec_receive_packet(enc_ctx, enc_pkt) < 0) {
      av_packet_free(&enc_pkt);
      break;
    }
    av_packet_rescale_ts(enc_pkt, ifmt_ctx->streams[video_idx]->time_base, out_stream->time_base);
    enc_pkt->stream_index = out_stream->index;
    av_interleaved_write_frame(ofmt_ctx, enc_pkt);
    av_packet_free(&enc_pkt);
  }

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