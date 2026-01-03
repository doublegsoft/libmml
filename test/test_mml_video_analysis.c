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
#define INPUT_FILENAME "../../data/G2.mp4"
#define COORDS_FILENAME "../../data/coords.txt"
#define OUTPUT_FILENAME "output_analysis.mp4"

typedef struct {
  double x1;
  double y1;
  double x2;
  double y2;
} Rect;

// Logic: Calculate ellipse position based on TIME (seconds), not raw PTS
static void 
draw_moving_ellipse(AVFrame* frame, double time_sec) {
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

  mml_shape_ellipse(frame, cx, cy, rx, ry, 0, col_y, col_u, col_v, 0.2);
}


// --- Main Program ---

int main(int argc, char** argv) 
{
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

  FILE* f_read = fopen(COORDS_FILENAME, "r");
  if (!f_read) {
    perror("Error opening file");
    return 1;
  }
  int capacity = 10;
  int count = 0;
  Rect* data_array = (Rect*)malloc(capacity * sizeof(Rect));
  if (!data_array) {
    perror("Initial allocation failed");
    fclose(f_read);
    return 1;
  }
  double t1, t2, t3, t4;

  while (fscanf(f_read, "%lf,%lf,%lf,%lf", &t1, &t2, &t3, &t4) == 4) {
    if (count >= capacity) {
      capacity *= 2;
      printf("  [Debug] Resizing array capacity to %d\n", capacity);
      
      Rect* temp = (Rect*)realloc(data_array, capacity * sizeof(Rect));
      if (!temp) {
        perror("Realloc failed");
        free(data_array);
        fclose(f_read);
        return 1;
      }
      data_array = temp;
    }

    data_array[count].x1 = t1;
    data_array[count].y1 = t2;
    data_array[count].x2 = t3;
    data_array[count].y2 = t4; 
    count++;
  }
  fclose(f_read);

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
  
  // for (int i = 0; i < count; i++) {
  //   Rect rect = data_array[i];
  //   printf("rect %d: %lf, %lf, %lf, %lf\n", i, rect.x1, rect.y1, rect.x2, rect.y2);
  // }
  // if (1) return 0;
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
        
        if (frame_count < count) 
        {
          Rect rect = data_array[frame_count];
          int cx = (int)(rect.x1 + rect.x2) / 2;
          int cy = (int)(rect.y2 - 3);
          mml_shape_ring(frame, cx, cy, 30, 10, 0.5f, 81, 94, 240, 0.8);
          char fn[1024];
          sprintf(fn, "/Users/christian/Downloads/abc/crop_%03lld.jpg", frame_count);
          mml_frame_image(frame, fn, rect.x1, rect.y1, rect.x2 - rect.x1, rect.y2 - rect.y1);
        }

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