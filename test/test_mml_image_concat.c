/**
 * FFmpeg C API: Directory to Vertical Panorama (JPEG)
 *
 * Logic:
 * 1. Scan directory for images.
 * 2. Sort filenames alphabetically.
 * 3. Load 1st image to set the "Base Width".
 * 4. Pre-calculate total height (scaling all images to Base Width).
 * 5. Allocate one giant YUVJ420P canvas.
 * 6. Load, Scale, and Paint images one by one onto the canvas.
 * 7. Save result as JPEG.
 *
 * Compile:
 * gcc dir_concat.c -o dir_concat -lavcodec -lavformat -lavutil -lswscale
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

// -----------------------------------------------------------------------------
// PART 1: Directory Scanning Logic
// -----------------------------------------------------------------------------

int is_image_file(const char* filename) {
  const char* dot = strrchr(filename, '.');
  if (!dot) return 0;
  if (strcasecmp(dot, ".jpg") == 0) return 1;
  if (strcasecmp(dot, ".jpeg") == 0) return 1;
  if (strcasecmp(dot, ".png") == 0) return 1;
  if (strcasecmp(dot, ".bmp") == 0) return 1;
  return 0;
}

int compare_filenames(const void* a, const void* b) {
  return strcmp(*(const char**)a, *(const char**)b);
}

char** get_image_files(const char* dir_path, int* count) {
  DIR* d;
  struct dirent* dir;
  d = opendir(dir_path);
  if (!d) return NULL;

  char** files = NULL;
  int capacity = 10;
  int n = 0;

  files = malloc(sizeof(char*) * capacity);

  while ((dir = readdir(d)) != NULL) {
    if (dir->d_type == DT_REG || dir->d_type == DT_UNKNOWN) {
      if (is_image_file(dir->d_name)) {
        if (n >= capacity) {
          capacity *= 2;
          files = realloc(files, sizeof(char*) * capacity);
        }

        int len = strlen(dir_path) + strlen(dir->d_name) + 2;
        files[n] = malloc(len);
        snprintf(files[n], len, "%s/%s", dir_path, dir->d_name);
        n++;
      }
    }
  }
  closedir(d);

  qsort(files, n, sizeof(char*), compare_filenames);
  *count = n;
  return files;
}

// -----------------------------------------------------------------------------
// PART 2: FFmpeg Image Loading
// -----------------------------------------------------------------------------

int load_image(const char* filename, AVFrame** out_frame) {
  AVFormatContext* fmt_ctx = NULL;
  AVCodecContext* dec_ctx = NULL;
  const AVCodec* dec = NULL;
  AVPacket* pkt = NULL;
  AVFrame* frame = NULL;
  int ret = -1;

  if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
    fprintf(stderr, "Err: Cannot open %s\n", filename);
    return -1;
  }
  avformat_find_stream_info(fmt_ctx, NULL);

  int idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
  if (idx < 0) goto end;

  dec_ctx = avcodec_alloc_context3(dec);
  avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[idx]->codecpar);
  if (avcodec_open2(dec_ctx, dec, NULL) < 0) goto end;

  pkt = av_packet_alloc();
  frame = av_frame_alloc();

  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == idx) {
      if (avcodec_send_packet(dec_ctx, pkt) == 0) {
        if (avcodec_receive_frame(dec_ctx, frame) == 0) {
          *out_frame = frame;
          frame = NULL;
          ret = 0;
          break;
        }
      }
    }
    av_packet_unref(pkt);
  }

end:
  if (frame) av_frame_free(&frame);
  if (pkt) av_packet_free(&pkt);
  if (dec_ctx) avcodec_free_context(&dec_ctx);
  if (fmt_ctx) avformat_close_input(&fmt_ctx);
  return ret;
}

// -----------------------------------------------------------------------------
// PART 3: Painting / Resizing Logic
// -----------------------------------------------------------------------------

void get_target_dims(int src_w, int src_h, int target_w, int* out_h) {
  double ratio = (double)target_w / src_w;
  *out_h = (int)(src_h * ratio);
  *out_h &= ~1; // Align to 2 for YUV420P
}

int paint_image(AVFrame* canvas, AVFrame* src, int y_offset, int h) {
  struct SwsContext* sws = sws_getContext(
    src->width, src->height, src->format,
    canvas->width, h, canvas->format,
    SWS_BICUBIC, NULL, NULL, NULL
  );
  
  if (!sws) return -1;

  // Manual pointer arithmetic to write to specific Y-offset in canvas
  uint8_t* dst_data[4];
  int dst_linesize[4];
  for (int i = 0; i < 4; i++) dst_linesize[i] = canvas->linesize[i];

  // Y Plane Offset
  dst_data[0] = canvas->data[0] + (y_offset * canvas->linesize[0]);
  
  // U/V Plane Offset
  int uv_off = y_offset / 2;
  dst_data[1] = canvas->data[1] + (uv_off * canvas->linesize[1]);
  dst_data[2] = canvas->data[2] + (uv_off * canvas->linesize[2]);

  sws_scale(sws, (const uint8_t* const*)src->data, src->linesize, 
            0, src->height, dst_data, dst_linesize);

  sws_freeContext(sws);
  return 0;
}

int save_jpg(AVFrame* frame, const char* filename) {
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  ctx->width = frame->width;
  ctx->height = frame->height;
  ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
  ctx->time_base = (AVRational){1, 25};
  
  if (avcodec_open2(ctx, codec, NULL) < 0) return -1;

  AVPacket* pkt = av_packet_alloc();
  avcodec_send_frame(ctx, frame);
  
  if (avcodec_receive_packet(ctx, pkt) == 0) {
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(pkt->data, 1, pkt->size, f);
      fclose(f);
    }
    av_packet_unref(pkt);
  }
  
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  return 0;
}

// -----------------------------------------------------------------------------
// PART 4: Main Execution
// -----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printf("Usage: %s <output.jpg> <input_directory>\n", argv[0]);
    return 1;
  }

  const char* out_file = argv[1];
  const char* in_dir = argv[2];

  // 1. Scan Directory
  int count = 0;
  char** file_list = get_image_files(in_dir, &count);
  
  if (count == 0) {
    printf("No images found in %s\n", in_dir);
    return 1;
  }
  printf("Found %d images.\n", count);

  // 2. Load FIRST image to determine Base Width
  AVFrame* first_frame = NULL;
  if (load_image(file_list[0], &first_frame) != 0) {
    fprintf(stderr, "Failed to load first image.\n");
    return 1;
  }

  int canvas_w = first_frame->width;
  canvas_w &= ~1; // Align width
  
  // 3. Pre-calculate Total Height
  printf("Calculating dimensions (Target Width: %d)...\n", canvas_w);
  
  int total_h = 0;
  int* heights = malloc(sizeof(int) * count);

  // Handle first frame
  get_target_dims(first_frame->width, first_frame->height, canvas_w, &heights[0]);
  total_h += heights[0];
  
  // Handle rest of the frames (Load -> Check Size -> Free)
  for (int i = 1; i < count; i++) {
    AVFrame* tmp = NULL;
    if (load_image(file_list[i], &tmp) == 0) {
      get_target_dims(tmp->width, tmp->height, canvas_w, &heights[i]);
      total_h += heights[i];
      av_frame_free(&tmp);
    } else {
      heights[i] = 0;
    }
    printf("  [%d/%d] Height: %d (Total: %d)\r", i + 1, count, heights[i], total_h);
  }
  printf("\nTotal Canvas: %dx%d\n", canvas_w, total_h);

  // 4. Allocate Canvas
  AVFrame* canvas = av_frame_alloc();
  canvas->format = AV_PIX_FMT_YUVJ420P;
  canvas->width = canvas_w;
  canvas->height = total_h;
  if (av_frame_get_buffer(canvas, 32) < 0) {
    fprintf(stderr, "Failed to allocate huge canvas.\n");
    return 1;
  }
  
  // Init Black
  memset(canvas->data[0], 0, canvas->height * canvas->linesize[0]);
  memset(canvas->data[1], 128, canvas->height / 2 * canvas->linesize[1]);
  memset(canvas->data[2], 128, canvas->height / 2 * canvas->linesize[2]);

  // 5. Paint Logic
  int current_y = 0;

  // Paint First Frame (already loaded)
  paint_image(canvas, first_frame, current_y, heights[0]);
  current_y += heights[0];
  av_frame_free(&first_frame);

  // Paint Rest
  for (int i = 1; i < count; i++) {
    if (heights[i] > 0) {
      AVFrame* tmp = NULL;
      if (load_image(file_list[i], &tmp) == 0) {
        paint_image(canvas, tmp, current_y, heights[i]);
        current_y += heights[i];
        av_frame_free(&tmp);
      }
    }
    printf("Painting... %d%%\r", (i * 100) / count);
  }

  // 6. Save
  printf("\nSaving JPEG...\n");
  save_jpg(canvas, out_file);

  // Cleanup
  av_frame_free(&canvas);
  free(heights);
  for (int i = 0; i < count; i++) free(file_list[i]);
  free(file_list);

  printf("Done. Saved to %s\n", out_file);
  return 0;
}