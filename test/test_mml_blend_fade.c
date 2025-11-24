#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> // 必须包含数学库

#define STB_IMAGE_IMPLEMENTATION

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "libmml-image.h"
#include "libmml-blend.h"
#include "libmml-frame.h"

// 配置
#define OUT_WIDTH 640
#define OUT_HEIGHT 480
#define FPS 25
#define BITRATE 800000
#define STATIC_DURATION 2.0    // 静止显示2秒
#define TRANSITION_DURATION 1.0 // 过渡1秒

int main(int argc, char** argv) {
  const char* outfile = "fade.mp4";

  const char* imgs[] = {"../../data/1.jpg", "../../data/2.jpg", "../../data/3.jpg"};
  
  // --- FFmpeg Setup ---
  AVFormatContext* oc = NULL;
  avformat_alloc_output_context2(&oc, NULL, NULL, outfile);
  
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVStream* st = avformat_new_stream(oc, NULL);
  AVCodecContext* c = avcodec_alloc_context3(codec);
  
  c->width = OUT_WIDTH;
  c->height = OUT_HEIGHT;
  c->time_base = (AVRational){1, FPS};
  c->framerate = (AVRational){FPS, 1};
  c->pix_fmt = AV_PIX_FMT_YUV420P;
  c->bit_rate = BITRATE;
  c->gop_size = 10;
  c->max_b_frames = 1;

  if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  avcodec_open2(c, codec, NULL);
  avcodec_parameters_from_context(st->codecpar, c);
  st->time_base = c->time_base;

  if (!(oc->oformat->flags & AVFMT_NOFILE))
    avio_open(&oc->pb, outfile, AVIO_FLAG_WRITE);
  
  avformat_write_header(oc, NULL);

  // --- Buffers ---
  AVFrame* frame_yuv = av_frame_alloc();
  frame_yuv->format = c->pix_fmt;
  frame_yuv->width = c->width;
  frame_yuv->height = c->height;
  av_frame_get_buffer(frame_yuv, 32);
  AVPacket* pkt = av_packet_alloc();

  // 用于转换 RGB canvas -> YUV frame
  struct SwsContext* sws_rgb2yuv = sws_getContext(OUT_WIDTH, OUT_HEIGHT, AV_PIX_FMT_RGB24,
                                                  OUT_WIDTH, OUT_HEIGHT, AV_PIX_FMT_YUV420P,
                                                  SWS_BILINEAR, NULL, NULL, NULL);

  // RGB 画布 (用于存储混合后的结果)
  uint8_t* rgb_canvas = (uint8_t*)malloc(OUT_WIDTH * OUT_HEIGHT * 3);

  int64_t global_pts = 0;
  
  // 加载第一张图
  uint8_t* img_current = mml_image_load(imgs[0], OUT_WIDTH, OUT_HEIGHT);
  if (!img_current) return 1;

  // --- Main Loop ---
  for (int i = 1; i < 4; i++) {
    uint8_t* img_next = NULL;
    if (i + 1 < 4) {
      img_next = mml_image_load(imgs[i], OUT_WIDTH, OUT_HEIGHT);
    }

    // 2. 显示静止画面 (Static)
    printf("Encoding static image %d...\n", i);
    int static_frames = FPS * STATIC_DURATION;
    
    // 将当前的 RGB copy 到 canvas
    memcpy(rgb_canvas, img_current, OUT_WIDTH * OUT_HEIGHT * 3);
    
    // RGB -> YUV
    const uint8_t* srcSlice[] = { rgb_canvas };
    int srcStride[] = { OUT_WIDTH * 3 };
    sws_scale(sws_rgb2yuv, srcSlice, srcStride, 0, OUT_HEIGHT,
              frame_yuv->data, frame_yuv->linesize);

    for (int f = 0; f < static_frames; f++) {
      av_frame_make_writable(frame_yuv);
      frame_yuv->pts = global_pts++;
      mml_frame_encode(c, oc, st, frame_yuv, pkt);
    }

    // 3. 显示过渡画面 (Transition)
    if (img_next) {
      printf("Encoding rotation transition...\n");
      int trans_frames = FPS * TRANSITION_DURATION;
      
      for (int f = 0; f < trans_frames; f++) {
        float progress = (float)f / (float)trans_frames;
        
        // 计算旋转和混合，结果存入 rgb_canvas
        mml_blend_fade(rgb_canvas, img_current, img_next, OUT_WIDTH, OUT_HEIGHT, progress);

        // RGB -> YUV
        sws_scale(sws_rgb2yuv, srcSlice, srcStride, 0, OUT_HEIGHT,
                  frame_yuv->data, frame_yuv->linesize);
        
        av_frame_make_writable(frame_yuv);
        frame_yuv->pts = global_pts++;
        mml_frame_encode(c, oc, st, frame_yuv, pkt);
      }
      
      // 切换当前图片：释放旧的 current，将 next 变为 current
      free(img_current);
      img_current = img_next; // 指针移交
    } else {
      // 最后一张图，释放内存
      free(img_current);
    }
  }

  // Flush & Cleanup
  mml_frame_encode(c, oc, st, NULL, pkt);
  av_write_trailer(oc);

  sws_freeContext(sws_rgb2yuv);
  avcodec_free_context(&c);
  av_frame_free(&frame_yuv);
  av_packet_free(&pkt);
  free(rgb_canvas);
  if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
  avformat_free_context(oc);

  printf("Done. Saved to %s\n", outfile);
  return 0;
}
