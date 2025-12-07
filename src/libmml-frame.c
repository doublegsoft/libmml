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
    frame->width, frame->height, frame->format,       // Src
    ctx->width, ctx->height, ctx->pix_fmt,            // Dst
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
                AVFormatContext* ofmt_ctx,
                AVStream* out_stream,
                AVFrame* frame)
{
  int ret;

  ret = avcodec_send_frame(enc_ctx, frame);
  if (ret < 0) return ret;

  while (ret >= 0) 
  {
    AVPacket* pkt = av_packet_alloc();
    ret = avcodec_receive_packet(enc_ctx, pkt);
    
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      av_packet_free(&pkt);
      break;
    } else if (ret < 0) {
      av_packet_free(&pkt);
      return ret;
    }

    // rescale timestamps for output (encoder time base -> stream time base)
    av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
    pkt->stream_index = out_stream->index;

    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    av_packet_free(&pkt);
    
    if (ret < 0) return ret;
  }
  return MML_SUCCESS;
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