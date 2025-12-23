/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>

#include "libmml-internal.h"
#include "libmml-error.h"
#include "libmml-frame.h"
#include "libmml-effect.h"

/*!
** @brief Applies a fast "Separable Box Blur" to a flat memory buffer.
**
** A naive box blur has a complexity of O(R^2) per pixel. This implementation 
** separates the blur into two 1D passes (Horizontal + Vertical), reducing 
** complexity to O(R). This is significantly faster for large radii.
**
** @param src    Source buffer containing the raw pixel data of the Region of Interest (ROI).
** @param dst    Destination buffer where the blurred pixels will be written.
** @param w      Width of the buffer.
** @param h      Height of the buffer.
** @param radius Blur strength (kernel radius).
*/
static void 
mml_buff_blur_fast(uint8_t* src, uint8_t* dst, int w, int h, int radius) {
  // If radius is 0, just copy the data and exit.
  if (radius < 1) {
    memcpy(dst, src, w * h);
    return;
  }

  uint8_t* temp = (uint8_t*)malloc(w * h);
  if (!temp) return; // Allocation failed

  for (int y = 0; y < h; y++) {
    int y_off = y * w; // Offset for the start of the current row
    
    for (int x = 0; x < w; x++) {
      int sum = 0;
      int count = 0;
      
      // 1D Kernel Loop (Left to Right)
      for (int k = -radius; k <= radius; k++) {
        int nx = x + k;
        
        // Boundary Check: Ensure neighbor is inside the buffer width
        if (nx >= 0 && nx < w) {
          sum += src[y_off + nx];
          count++;
        }
      }
      // Calculate average and store in intermediate buffer
      temp[y_off + x] = sum / count;
    }
  }

  for (int x = 0; x < w; x++) {
    for (int y = 0; y < h; y++) {
      int sum = 0;
      int count = 0;
      
      // 1D Kernel Loop (Top to Bottom)
      for (int k = -radius; k <= radius; k++) {
        int ny = y + k;
        
        // Boundary Check: Ensure neighbor is inside the buffer height
        if (ny >= 0 && ny < h) {
          sum += temp[ny * w + x];
          count++;
        }
      }
      // Write final result to destination
      dst[y * w + x] = sum / count;
    }
  }

  free(temp);
}

/*!
** @brief Applies a "Box Blur" to a specific rectangular region of a single image plane.
**
** This function calculates the average value of neighboring pixels for every pixel
** within the specified region. It handles memory strides (linesize) correctly.
**
** @note This is an O(R^2) algorithm per pixel. High radius values (>20) will be slow.
**
** @param data      Pointer to the start of the plane data (e.g., frame->data[0]).
** @param linesize  The memory stride (width + padding) of the plane.
** @param x_off     Top-Left X coordinate of the region to blur.
** @param y_off     Top-Left Y coordinate of the region to blur.
** @param w         Width of the region to blur.
** @param h         Height of the region to blur.
** @param radius    Blur strength (kernel size). E.g., 10 means a 21x21 grid average.
*/
static void 
mml_plane_blur_rect(uint8_t* data, int linesize, int x_off, int y_off, int w, int h, int radius) {

  if (radius < 1) return;

  // Allocate temp buffer for the ROI
  uint8_t* temp = (uint8_t*)malloc(w * h);
  if (!temp) return;

  // ---------------------------------------------------------
  // PASS 1: Horizontal Blur (Source -> Temp)
  // ---------------------------------------------------------
  for (int y = 0; y < h; y++) {
    // Pre-calculate row pointers
    uint8_t* src_row = data + ((y_off + y) * linesize) + x_off;
    uint8_t* dst_row = temp + (y * w);

    for (int x = 0; x < w; x++) {
      int sum = 0;
      int count = 0;

      // 1D Loop (Left to Right)
      for (int k = -radius; k <= radius; k++) {
        int nx = x + k;
        if (nx >= 0 && nx < w) {
          sum += src_row[nx];
          count++;
        }
      }
      dst_row[x] = sum / count;
    }
  }
  
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < h; y++) {
      int sum = 0;
      int count = 0;

      // 1D Loop (Top to Bottom)
      for (int k = -radius; k <= radius; k++) {
        int ny = y + k;
        if (ny >= 0 && ny < h) {
          // Read from Temp
          sum += temp[ny * w + x];
          count++;
        }
      }
      
      // Write back to original frame
      uint8_t* dst_pixel = data + ((y_off + y) * linesize) + (x_off + x);
      *dst_pixel = sum / count;
    }
  }

  free(temp);
}
 
/*!
** @brief Handles the cropping, blurring, and masked pasting for a single image plane.
**
** This function optimizes performance by:
** 1. Defining a bounding box (ROI) around the circle.
** 2. Copying that small ROI to a continuous buffer (handling memory stride/linesize).
** 3. Blurring the ROI buffer.
** 4. Copying the blurred pixels back to the original frame ONLY if they fall inside the circle.
**
** @param data          Pointer to the start of the plane (e.g., frame->data[0]).
** @param linesize      Stride of the plane (width + padding).
** @param width         Full width of the plane.
** @param height        Full height of the plane.
** @param cx            Center X of the circle.
** @param cy            Center Y of the circle.
** @param rad           Radius of the circle.
** @param blur_strength Blur kernel size.
*/
static void 
mml_plane_blur_circle(uint8_t* data, int linesize, /*int width, int height, */
                      int cx, int cy, int rad, int blur_strength) {
  
  int min_x = cx - rad;
  int max_x = cx + rad;
  int min_y = cy - rad;
  int max_y = cy + rad;

  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  // if (max_x >= width) max_x = width - 1;
  // if (max_y >= height) max_y = height - 1;

  int w = max_x - min_x + 1;
  int h = max_y - min_y + 1;

  // If box is invalid (e.g., circle is off-screen), exit.
  if (w <= 0 || h <= 0) return;

  uint8_t* roi_src = (uint8_t*)malloc(w * h);
  uint8_t* roi_blurred = (uint8_t*)malloc(w * h);
  
  if (!roi_src || !roi_blurred) {
    if (roi_src) free(roi_src);
    if (roi_blurred) free(roi_blurred);
    return;
  }

  for (int y = 0; y < h; y++) {
    // Pointer to the start of the row in the Video Frame
    uint8_t* ptr_in = data + ((min_y + y) * linesize) + min_x;
    
    // Pointer to the start of the row in our Flat Buffer
    uint8_t* ptr_out = roi_src + (y * w);
    
    memcpy(ptr_out, ptr_in, w);
  }

  mml_buff_blur_fast(roi_src, roi_blurred, w, h, blur_strength);

  int r_sq = rad * rad; // Compare squared distances to avoid expensive sqrt()

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      // Calculate absolute coordinates in the main image
      int abs_x = min_x + x;
      int abs_y = min_y + y;

      // Calculate distance vector from circle center
      int dx = abs_x - cx;
      int dy = abs_y - cy;
      int dist_sq = dx * dx + dy * dy;

      // If the pixel is inside the circle...
      if (dist_sq <= r_sq) {
        // ... overwrite the original pixel with the value from 'roi_blurred'
        data[(abs_y * linesize) + abs_x] = roi_blurred[y * w + x];
      }
    }
  }

  free(roi_src);
  free(roi_blurred);
}
 
AVFrame* 
mml_frame_deep(AVFrame* src, 
               struct SwsContext** sws_cache) 
{
  AVFrame* dst = av_frame_alloc();
  dst->format = AV_PIX_FMT_YUV420P;
  dst->width = src->width;
  dst->height = src->height;
  
  if (av_frame_get_buffer(dst, 32) < 0) {
    av_frame_free(&dst);
    return NULL;
  }

  // Initialize Scaler if needed
  if (!*sws_cache) {
    *sws_cache = sws_getContext(
        src->width, src->height, src->format,
        src->width, src->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL);
  }

  // Convert/Copy pixel data
  sws_scale(*sws_cache, (const uint8_t* const*)src->data, src->linesize,
            0, src->height, dst->data, dst->linesize);

  return dst;
}

int 
mml_frame_save(AVFrame* frame, const char* filename) 
{
  int ret = 0;
  const AVCodec* codec = NULL;
  AVCodecContext* ctx = NULL;
  AVPacket* pkt = NULL;
  AVFrame* jpg_frame = NULL;
  struct SwsContext* sws = NULL;
  FILE* file = NULL;

  codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
  if (!codec) {
    fprintf(stderr, "[Error] MJPEG encoder not found.\n");
    return -1;
  }

  ctx = avcodec_alloc_context3(codec);
  if (!ctx) return -1;

  ctx->width = frame->width;
  ctx->height = frame->height;
  
  // "time_base" is mandatory for encoders, even for single images.
  ctx->time_base = (AVRational){1, 25}; 
  
  // JPEG usually uses YUVJ420P (Full Range: 0-255).
  // Standard video is YUV420P (Limited Range: 16-235).
  // We explicitly set this to ensure the JPEG looks correct.
  ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;

  if (avcodec_open2(ctx, codec, NULL) < 0) {
    fprintf(stderr, "[Error] Could not open codec.\n");
    ret = -1;
    goto cleanup;
  }

  // We cannot send just *any* frame to the MJPEG encoder. It must be YUVJ420P.
  // We allocate a temporary frame and convert using libswscale.
  
  jpg_frame = av_frame_alloc();
  jpg_frame->format = ctx->pix_fmt;
  jpg_frame->width  = ctx->width;
  jpg_frame->height = ctx->height;
  
  if (av_frame_get_buffer(jpg_frame, 32) < 0) {
    ret = -1;
    goto cleanup;
  }

  sws = sws_getContext(
    frame->width, frame->height, frame->format,
    ctx->width, ctx->height, ctx->pix_fmt, 
    SWS_BILINEAR, NULL, NULL, NULL
  );

  if (!sws) {
    ret = -1;
    goto cleanup;
  }

  /*
  ** Input (YUV420P)                     Output (RGB24)
  ** ----------------                    --------------
  ** data[0] -> [YYYYYYYY...]            data[0] -> [RGBRGBRGBRGB...]
  ** data[1] -> [UUUU...]                data[1] -> NULL
  ** data[2] -> [VVVV...]                data[2] -> NULL
  */
  sws_scale(sws, 
            (const uint8_t* const*)frame->data, frame->linesize, 
            0, frame->height, 
            jpg_frame->data, jpg_frame->linesize);

  pkt = av_packet_alloc();
  if (!pkt) {
    ret = -1;
    goto cleanup;
  }

  // Send the converted frame to encoder
  ret = avcodec_send_frame(ctx, jpg_frame);
  if (ret < 0) {
    fprintf(stderr, "[Error] Error sending frame to encoder.\n");
    goto cleanup;
  }

  // Receive the compressed packet
  ret = avcodec_receive_packet(ctx, pkt);
  if (ret == 0) {
    // ------------------------------------------------------------------------
    // 5. Write to Disk
    // ------------------------------------------------------------------------
    file = fopen(filename, "wb");
    if (file) {
      fwrite(pkt->data, 1, pkt->size, file);
      fclose(file);
      printf("[Success] Saved %s (%d bytes)\n", filename, pkt->size);
    } else {
      fprintf(stderr, "[Error] Could not open %s for writing.\n", filename);
      ret = -1;
    }
    av_packet_unref(pkt);
  } else {
    fprintf(stderr, "[Error] Encoding failed.\n");
  }

cleanup:
  if (sws) sws_freeContext(sws);
  if (jpg_frame) av_frame_free(&jpg_frame);
  if (pkt) av_packet_free(&pkt);
  if (ctx) avcodec_free_context(&ctx);

  return ret;
}

int 
mml_frame_write(AVCodecContext* enc_ctx, 
                AVFormatContext* fmt_ctx,
                AVStream* stream,
                AVFrame* frame)
{
  int ret = MML_SUCCESS;

  ret = avcodec_send_frame(enc_ctx, frame);
  if (ret < 0) return ret;

  // 2. Allocate packet once (Efficiency)
  AVPacket* pkt = av_packet_alloc();
  if (!pkt) return AVERROR(ENOMEM);

  while (ret >= 0) {
    ret = avcodec_receive_packet(enc_ctx, pkt);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      ret = 0;
      break;
    } else if (ret < 0) {
      break;
    }

    av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
    pkt->stream_index = stream->index;

    ret = av_interleaved_write_frame(fmt_ctx, pkt);
    av_packet_unref(pkt);

    if (ret < 0) break; 
  }

  av_packet_free(&pkt);
  return ret;
}

int 
mml_frame_capture(struct SwsContext*  sws, 
                  AVFrame*            dec_frame, 
                  AVFrame**           dst_frame)
{
  int ret = MML_SUCCESS;

  if (*dst_frame == NULL)
  {
    *dst_frame = av_frame_alloc();
    (*dst_frame)->format = AV_PIX_FMT_YUV420P;
    (*dst_frame)->width = dec_frame->width;
    (*dst_frame)->height = dec_frame->height;
    av_frame_get_buffer(*dst_frame, 32);
  }
  sws_scale(sws, 
            (const uint8_t* const*)dec_frame->data, 
            dec_frame->linesize,
            0, 
            dec_frame->height, 
            (*dst_frame)->data, 
            (*dst_frame)->linesize);
  return ret;
}

void 
mml_frame_zoom(AVFrame* dst, AVFrame* src, int off_x, int off_y, int width, int height) {
  for (int y = 0; y < src->height; y++) {
    uint8_t* p_dst = dst->data[0] + ((off_y + y) * dst->linesize[0]) + off_x;
    uint8_t* p_src = src->data[0] + (y * src->linesize[0]);
    memcpy(p_dst, p_src, src->width);
  }

  // 2. U and V Planes (Half size for YUV420P)
  int uv_off_x = off_x / 2;
  int uv_off_y = off_y / 2;
  int uv_w = width;
  int uv_h = height;

  for (int i = 1; i < 3; i++) 
  {
    for (int y = 0; y < uv_h; y++) 
    {
      uint8_t* p_dst = dst->data[i] + ((uv_off_y + y) * dst->linesize[i]) + uv_off_x;
      uint8_t* p_src = src->data[i] + (y * src->linesize[i]);
      memcpy(p_dst, p_src, uv_w);
    }
  }
}

void 
mml_frame_overlay(AVFrame* dst, AVFrame* src, int x_off, int y_off) 
{
  if (x_off < 0 || y_off < 0) return;
  if (x_off + src->width > dst->width) return;
  if (y_off + src->height > dst->height) return;

  for (int y = 0; y < src->height; y++) 
  {
    uint8_t* p_dst = dst->data[0] + ((y + y_off) * dst->linesize[0]) + x_off;
    uint8_t* p_src = src->data[0] + (y * src->linesize[0]);
    memcpy(p_dst, p_src, src->width);
  }

  int uv_w = src->width / 2;
  int uv_h = src->height / 2;
  int uv_x = x_off / 2;
  int uv_y = y_off / 2;

  for (int i = 1; i < 3; i++) 
  {
    for (int y = 0; y < uv_h; y++) 
    {
      uint8_t* p_dst = dst->data[i] + ((y + uv_y) * dst->linesize[i]) + uv_x;
      uint8_t* p_src = src->data[i] + (y * src->linesize[i]);
      memcpy(p_dst, p_src, uv_w);
    }
  }
}

void 
mml_frame_blur(AVFrame* frame, 
               int x, 
               int y,
               int w,
               int h,
               int r)
{
  int bx = x;
  int by = y;
  int bw = w;
  int bh = h;

  // Clamp to frame dimensions
  if (bx < 0) bx = 0;
  if (by < 0) by = 0;
  if (bx + bw > frame->width) bw = frame->width - bx;
  if (by + bh > frame->height) bh = frame->height - by;

  // Process Y
  mml_plane_blur_circle(frame->data[0], frame->linesize[0], bx, by, bw / 2, r);

  int uv_x = bx / 2;
  int uv_y = by / 2;
  int uv_w = bw / 2;
  int uv_h = bh / 2;
  // Radius also scales down slightly for efficiency, or keep same for smoothness
  int uv_rad = r / 2; 
  if (uv_rad < 1) uv_rad = 1;

  mml_plane_blur_circle(frame->data[1], frame->linesize[1], uv_x, uv_y, uv_w / 2, uv_rad);
  mml_plane_blur_circle(frame->data[2], frame->linesize[2], uv_x, uv_y, uv_w / 2, uv_rad);
}

void 
mml_frame_rect(AVFrame* frame, 
               int x, 
               int y, 
               int w, 
               int h,
               int bw,
               int yc,
               int uc,
               int vc) {
  // Top & Bottom lines
  for (int i = -bw; i < w + bw; i++) {
    for (int t = 0; t < bw; t++) {
      int bx = x + i;
      int by_top = y - bw + t;
      int by_bot = y + h + t;
      
      if (bx >= 0 && bx < frame->width) {
        if (by_top >= 0) {
          frame->data[0][by_top * frame->linesize[0] + bx] = yc;
          // Apply Chroma only on even coordinates (YUV420P subsampling)
          if (bx % 2 == 0 && by_top % 2 == 0) {
            int uv_idx = (by_top / 2) * frame->linesize[1] + (bx / 2);
            frame->data[1][uv_idx] = uc;
            frame->data[2][uv_idx] = vc;
          }
        }
        if (by_bot < frame->height) {
          frame->data[0][by_bot * frame->linesize[0] + bx] = yc;
          if (bx % 2 == 0 && by_bot % 2 == 0) {
            int uv_idx = (by_bot / 2) * frame->linesize[1] + (bx / 2);
            frame->data[1][uv_idx] = uc;
            frame->data[2][uv_idx] = vc;
          }
        }
      }
    }
  }
  // Left & Right lines
  for (int i = -bw; i < h + bw; i++) {
    for (int t = 0; t < bw; t++) {
      int by = y + i;
      int bx_left = x - bw + t;
      int bx_right = x + w + t;

      if (by >= 0 && by < frame->height) {
        if (bx_left >= 0) {
          frame->data[0][by * frame->linesize[0] + bx_left] = yc;
          if (bx_left % 2 == 0 && by % 2 == 0) {
            int uv_idx = (by / 2) * frame->linesize[1] + (bx_left / 2);
            frame->data[1][uv_idx] = uc;
            frame->data[2][uv_idx] = vc;
          }
        }
        if (bx_right < frame->width) {
          frame->data[0][by * frame->linesize[0] + bx_right] = yc;
          if (bx_right % 2 == 0 && by % 2 == 0) {
            int uv_idx = (by / 2) * frame->linesize[1] + (bx_right / 2);
            frame->data[1][uv_idx] = uc;
            frame->data[2][uv_idx] = vc;
          }
        }
      }
    }
  }
}

void 
mml_frame_circle(AVFrame* frame, 
                 int cx, 
                 int cy, 
                 int rad, 
                 int bw,
                 int yc,
                 int uc,
                 int vc) 
{
  int r_inner_sq = rad * rad;
  int r_outer_sq = (rad + bw) * (rad + bw);

  int min_x = cx - rad - bw;
  int max_x = cx + rad + bw;
  int min_y = cy - rad - bw;
  int max_y = cy + rad + bw;

  // Clamp to frame dimensions to prevent Segmentation Faults
  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  if (max_x >= frame->width) max_x = frame->width;
  if (max_y >= frame->height) max_y = frame->height;

  for (int y = min_y; y < max_y; y++) {
    for (int x = min_x; x < max_x; x++) {
      
      // Calculate distance squared from center
      int dx = x - cx;
      int dy = y - cy;
      int dist_sq = dx * dx + dy * dy;

      // Check if pixel falls inside the border ring
      if (dist_sq >= r_inner_sq && dist_sq < r_outer_sq) {
        
        // --- Set Luma (Y) ---
        // data[0] + (Row * Stride) + Column
        frame->data[0][y * frame->linesize[0] + x] = yc;

        // --- Set Chroma (U/V) ---
        // YUV420P: Chroma is shared for a 2x2 block of pixels.
        // We only write when both coordinates are even.
        if (x % 2 == 0 && y % 2 == 0) {
          int uv_idx = (y / 2) * frame->linesize[1] + (x / 2);
          
          frame->data[1][uv_idx] = uc; 
          frame->data[2][uv_idx] = vc; 
        }
      }
    }
  }
}

int 
mml_frame_resize(AVFrame* src, 
                 AVFrame** dst, 
                 struct SwsContext** sws, 
                 int width, int height) 
{
  int ret = 0;

  if (!src || !dst) return -1;

  if (*sws == NULL)
    *sws = sws_getContext(
      src->width, src->height, src->format, 
      width, height, src->format,                 
      SWS_BICUBIC, NULL, NULL, NULL 
    );

  if (*dst == NULL) {
    *dst = av_frame_alloc();
    if (!*dst) return AVERROR(ENOMEM);

    // Set properties
    (*dst)->format = src->format; // Preserve pixel format (e.g., YUV420P)
    (*dst)->width  = width;
    (*dst)->height = height;

    // Allocate buffer
    ret = av_frame_get_buffer(*dst, 32);
    if (ret < 0) {
      av_frame_free(dst);
      return ret;
    }
  } 
  else 
  {
    // Safety check: Ensure provided dst matches requested dimensions
    if ((*dst)->width != width || (*dst)->height != height) {
      return AVERROR(EINVAL); // Invalid argument: Dimension mismatch
    }
  }

  if (!sws) return AVERROR(ENOMEM);

  sws_scale(*sws, 
            (const uint8_t* const*)src->data, src->linesize, 
            0, src->height, 
            (*dst)->data, (*dst)->linesize);

  (*dst)->pts = src->pts;
  (*dst)->pkt_dts = src->pkt_dts;

  return 0;
}

int 
mml_frame_aspect(AVFrame* src, 
                 AVFrame** dst, 
                 struct SwsContext** sws, 
                 int frame_width, int frame_height,
                 int pic_width, int pic_height,
                 int offset_x, int offset_y) 
{
  int ret = 0;

  if (!src || !dst) return -1;

  if (*sws == NULL)
    *sws = sws_getContext(
      src->width, src->height, src->format, 
      pic_width, pic_height, src->format,                 
      SWS_BICUBIC, NULL, NULL, NULL 
    );

  if (*dst == NULL) {
    *dst = av_frame_alloc();
    if (!*dst) return AVERROR(ENOMEM);

    // Set properties
    (*dst)->format = src->format; // Preserve pixel format (e.g., YUV420P)
    (*dst)->width  = frame_width;
    (*dst)->height = frame_height;

    // Allocate buffer
    ret = av_frame_get_buffer(*dst, 32);
    if (ret < 0) {
      av_frame_free(dst);
      return ret;
    }
  } 
  else 
  {
    // Safety check: Ensure provided dst matches requested dimensions
    if ((*dst)->width != frame_width || (*dst)->height != frame_height) {
      return AVERROR(EINVAL); // Invalid argument: Dimension mismatch
    }
  }

  if (!sws) return AVERROR(ENOMEM);

  //
  // fill in black
  //
  for (int y = 0; y < (*dst)->height; y++) {
    memset((*dst)->data[0] + y * (*dst)->linesize[0], 0, (*dst)->width);
  }

  // U and V Planes (Chroma) -> 128 (Neutral Gray)
  // Note: In YUV, 0 is green. 128 is "no color". 
  // Combined with Y=0, this produces visual black.
  for (int i = 1; i < 3; i++) {
    int h_chroma = (*dst)->height / 2;
    int w_chroma = (*dst)->width / 2;
    for (int y = 0; y < h_chroma; y++) {
      memset((*dst)->data[i] + y * (*dst)->linesize[i], 128, w_chroma);
    }
  }

  uint8_t* dst_data[4];
  int dst_linesize[4];

  // Copy linesize from destination frame (Stride must remain full width)
  for (int i=0; i<4; i++) dst_linesize[i] = (*dst)->linesize[i];

  // Offset Y
  dst_data[0] = (*dst)->data[0] + (offset_y * (*dst)->linesize[0]) + offset_x;
  
  // Offset U/V
  int uv_off_y = offset_y / 2;
  int uv_off_x = offset_x / 2;
  dst_data[1] = (*dst)->data[1] + (uv_off_y * (*dst)->linesize[1]) + uv_off_x;
  dst_data[2] = (*dst)->data[2] + (uv_off_y * (*dst)->linesize[2]) + uv_off_x;

  sws_scale(*sws, 
            (const uint8_t* const*)src->data, src->linesize, 
            0, src->height, 
            dst_data, dst_linesize);

  (*dst)->pts = src->pts;
  (*dst)->pkt_dts = src->pkt_dts;

  return 0;
}

void mml_frame_fade(AVFrame* frame, double current_time, double duration) 
{
  // If the animation time has passed, do nothing. 
  // The frame remains at full brightness (Original state).
  if (current_time >= duration) return;

  // Alpha range: 0.0 (Fully Black) -> 1.0 (Fully Visible)
  double alpha = current_time / duration;

  // Safety clamping to ensure we don't overflow later
  if (alpha < 0.0) alpha = 0.0;
  if (alpha > 1.0) alpha = 1.0;

  // --- Optimization: Fixed Point Arithmetic ---
  // Performing floating-point multiplication for every single pixel (2 million+ for 1080p)
  // is expensive. We convert alpha to an integer scale (0 to 256).
  // Later, we can use bitwise shift (>> 8) instead of division, which is much faster.
  int fade_scale = (int)(alpha * 256);

  int w = frame->width;
  int h = frame->height;

  // In YUV, Y=0 is Black, Y=255 is White.
  // To fade to black, we simply scale the Y value towards 0.
  for (int y = 0; y < h; y++) {
    uint8_t* row = frame->data[0] + (y * frame->linesize[0]);
    for (int x = 0; x < w; x++) {
      // Logic: New_Y = Old_Y * alpha
      // Implementation: (Pixel * 0..256) / 256
      row[x] = (row[x] * fade_scale) >> 8;
    }
  }

  // YUV420P Subsampling: Chroma planes are half the width and half the height.
  int uv_h = h / 2;
  int uv_w = w / 2;

  // Loop through U (data[1]) and V (data[2])
  for (int i = 1; i < 3; i++) { 
    for (int y = 0; y < uv_h; y++) {
      uint8_t* row = frame->data[i] + (y * frame->linesize[i]);
      for (int x = 0; x < uv_w; x++) {
        
        // --- The "Fade to Gray" Logic ---
        // Unlike RGB where Black is (0,0,0), in YUV "Black" is (Y=0, U=128, V=128).
        // 128 is the "neutral" point (no color saturation).
        // If we scaled U/V to 0, the image would turn Green, not Black/Gray.
        
        // 1. Shift origin to 0 (subtract 128)
        int val = row[x] - 128;
        
        // 2. Scale towards 0 (reduce saturation)
        val = (val * fade_scale) >> 8;
        
        // 3. Shift origin back to 128
        row[x] = (uint8_t)(val + 128);
      }
    }
  }
}

void 
mml_frame_rotate(AVFrame* base, AVFrame** work, float angle) 
{
  if (!base) return;

  if (*work == NULL) 
  {
    *work = av_frame_alloc();
    (*work)->format = AV_PIX_FMT_YUV420P;
    (*work)->width = base->width;
    (*work)->height = base->height;
    av_frame_get_buffer(*work, 32);
  }

  // Convert degree to radian
  float rad = angle * M_PI / 180.0f;

  mml_plane_rotate(base->data[0], (*work)->data[0], base->linesize[0], 
                   base->width, base->height, rad, 0);

  // Rotate Chroma (U/V)
  // YUV420P: Chroma is half size
  // Background color 128 (Gray/Neutral)
  mml_plane_rotate(base->data[1], (*work)->data[1], base->linesize[1], 
                   base->width / 2, base->height / 2, rad, 128);

  mml_plane_rotate(base->data[2], (*work)->data[2], base->linesize[2], 
                   base->width / 2, base->height / 2, rad, 128);
}

/*!
** @brief Performs a "Wipe" transition effect (Left to Right) on a video frame.
**
** This function copies pixels from the source frame up to a specific horizontal 
** point determined by 'progress'. The rest of the frame is filled with black.
**
** @note This implementation assumes **AV_PIX_FMT_YUV420P**.
**
** @param src       [In] The source frame containing the full image.
** @param work      [In/Out] Double pointer to the working frame (canvas).
**                  If *work is NULL, it will be allocated automatically.
** @param progress  Animation progress from 0.0 (All Black) to 1.0 (Full Image).
*/
void 
mml_frame_wipe(AVFrame* src, AVFrame** work, float progress) 
{
  if (*work == NULL) 
  {
    *work = av_frame_alloc();
    (*work)->format = AV_PIX_FMT_YUV420P;
    (*work)->width = src->width;
    (*work)->height = src->height;
    av_frame_get_buffer(*work, 32);
  }
  // progress: 0.0 (All Black) -> 1.0 (All Image)
  
  // 1. Calculate Split X coordinate
  int split_x = (int)(src->width * progress);
  
  // Clamp
  if (split_x < 0) split_x = 0;
  if (split_x > src->width) split_x = src->width;

  int h = src->height;
  int w = src->width;

  // 2. Process Luma (Y)
  // We use memcpy for the visible part and memset for the hidden part.
  // This is much faster than setting pixels one by one.
  for (int y = 0; y < h; y++) {
    uint8_t* src_row = src->data[0] + (y * src->linesize[0]);
    uint8_t* dst_row = (*work)->data[0] + (y * (*work)->linesize[0]);

    // Copy the "Revealed" part
    if (split_x > 0) {
      memcpy(dst_row, src_row, split_x);
    }

    // Black out the "Hidden" part (Y=0)
    if (split_x < w) {
      memset(dst_row + split_x, 0, w - split_x);
    }
  }

  // 3. Process Chroma (U/V)
  // YUV420P: Chroma width is half of Luma width
  int uv_h = h / 2;
  int uv_w = w / 2;
  int uv_split = split_x / 2; // Split point also scales down

  for (int i = 1; i < 3; i++) {
    for (int y = 0; y < uv_h; y++) {
      uint8_t* src_row = src->data[i] + (y * src->linesize[i]);
      uint8_t* dst_row = (*work)->data[i] + (y * (*work)->linesize[i]);

      // Copy Revealed
      if (uv_split > 0) {
        memcpy(dst_row, src_row, uv_split);
      }

      // Fill Hidden (Neutral Gray = 128)
      if (uv_split < uv_w) {
        memset(dst_row + uv_split, 128, uv_w - uv_split);
      }
    }
  }
}

int 
mml_frame_flash(AVFrame* src, AVFrame** work, float intensity) 
{
  if (*work == NULL) {
    *work = av_frame_alloc();
    if (!*work) return -1;
    (*work)->format = AV_PIX_FMT_YUV420P;
    (*work)->width  = src->width;
    (*work)->height = src->height;
    if (av_frame_get_buffer(*work, 32) < 0) return -1;
  }

  if (intensity <= 0.001f) {
    av_frame_copy(*work, src);
    return 0;
  }

  int scale = (int)(intensity * 256);
  if (scale > 256) scale = 256;

  int h = src->height;
  int w = src->width;

  for (int y = 0; y < h; y++) {
    uint8_t* s_row = src->data[0] + (y * src->linesize[0]);
    uint8_t* d_row = (*work)->data[0] + (y * (*work)->linesize[0]);

    for (int x = 0; x < w; x++) {
      int pixel = s_row[x];
      // Formula: P_new = P_old + (255 - P_old) * intensity
      // Integer: P_old + ((255 - P_old) * scale >> 8)
      int diff = 255 - pixel;
      d_row[x] = (uint8_t)(pixel + ((diff * scale) >> 8));
    }
  }

  int uv_h = h / 2;
  int uv_w = w / 2;

  for (int i = 1; i < 3; i++) {
    for (int y = 0; y < uv_h; y++) {
      uint8_t* s_row = src->data[i] + (y * src->linesize[i]);
      uint8_t* d_row = (*work)->data[i] + (y * (*work)->linesize[i]);

      for (int x = 0; x < uv_w; x++) {
        int pixel = s_row[x];
        // Target is 128. Diff can be positive or negative.
        int diff = 128 - pixel; 
        d_row[x] = (uint8_t)(pixel + ((diff * scale) >> 8));
      }
    }
  }

  return 0;
}

void 
mml_frame_pixelate(AVFrame* src, AVFrame** work, int block_size) 
{
  if (*work == NULL)
  {
    *work = av_frame_alloc();
    (*work)->format = AV_PIX_FMT_YUV420P;
    (*work)->width = src->width;
    (*work)->height = src->height;
    av_frame_get_buffer((*work), 32);
  }
  if (block_size < 2) return;
  
  // Force even block size (round down)
  if (block_size % 2 != 0) block_size--; 

  av_frame_copy(*work, src);
  mml_plane_pixelate((*work)->data[0], (*work)->linesize[0], (*work)->width, (*work)->height, block_size);

  int uv_w = (*work)->width / 2;
  int uv_h = (*work)->height / 2;
  int uv_block = block_size / 2;

  mml_plane_pixelate((*work)->data[1], (*work)->linesize[1], uv_w, uv_h, uv_block);
  mml_plane_pixelate((*work)->data[2], (*work)->linesize[2], uv_w, uv_h, uv_block);
}