/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_FRAME_H__
#define __LIBMML_FRAME_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

/*!
** @brief Encodes a single AVFrame into a JPEG image and saves it to a file.
**
** This function creates a temporary MJPEG encoder, pushes the raw frame into it,
** retrieves the encoded packet, and writes the bytes to disk.
**
** @note The input frame->format MUST support JPEG encoding (usually AV_PIX_FMT_YUVJ420P).
**       If your video is standard AV_PIX_FMT_YUV420P, the encoder usually handles it,
**       but strictly speaking, JPEGs use the full 0-255 color range (YUVJ).
**
** @param frame    Pointer to the raw (decoded) AVFrame to be saved.
** @param filename Target file path (e.g., "snapshot.jpg").
** @return int     0 on success, or a negative AVERROR code on failure.
*/
int 
mml_frame_save(AVFrame* frame, 
               const char* filename);

/*!
** 将原始 AVFrame 编码并写入输出文件
** 
** @param enc_ctx    编码器上下文 (存储编码参数)
** @param ofmt_ctx   输出格式上下文 (管理输出文件)
** @param out_stream 输出流 (用于获取时间基和索引)
** @param frame      要编码的原始帧 (如果为 NULL，则表示刷新编码器，输出剩余缓存数据)
** @return 0 表示成功，负数表示错误
*/
int 
mml_frame_write(AVCodecContext* enc_ctx, 
                AVFormatContext* ofmt_ctx, 
                AVStream* out_stream,
                AVFrame* frame);

/*！
** @brief Captures a decoded frame and converts it to a standard YUV420P buffer.
**
** This function handles "Lazy Initialization": if the destination frame (*dst_frame)
** is NULL, it allocates memory for it. It then uses libswscale to convert the 
** input frame format to AV_PIX_FMT_YUV420P.
**
** @param sws        Initialized SwsContext (Input Fmt -> YUV420P).
** @param dec_frame  The raw frame coming from the decoder (Source).
** @param dst_frame  [In/Out] Pointer to the destination AVFrame pointer. 
**                   If *dst_frame is NULL, it will be allocated.
** @return int       MML_SUCCESS (0) on success.
*/
int 
mml_frame_capture(struct SwsContext*  sws, 
                  AVFrame*            dec_frame, 
                  AVFrame**           dst_frame);                
/*!
** @brief Overlays a source frame onto a destination frame at a specific coordinate.
**
** This function performs a raw memory copy of pixel data.
** 
** @note **CRITICAL**: This function assumes the pixel format is **AV_PIX_FMT_YUV420P**.
**       It calculates Chroma (U/V) offsets by dividing dimensions by 2.
**       Using RGB, YUV444, or YUV422 inputs will result in memory corruption.
**
** @param dst    The background frame (canvas). Must be YUV420P.
** @param src    The foreground frame (overlay). Must be YUV420P.
** @param x_off  X coordinate (horizontal) for the top-left of the overlay.
** @param y_off  Y coordinate (vertical) for the top-left of the overlay.
*/
void 
mml_frame_overlay(AVFrame* dst, 
                  AVFrame* src, 
                  int x_off, 
                  int y_off);

/*！
** @brief Applies a blur effect to a specific rectangular region of a video frame.
**
** This function handles the specifics of the YUV420P pixel format.
** It blurs the Luma (Y) plane at full resolution and the Chroma (U/V) planes
** at half resolution (subsampled).
**
** @param frame  The target AVFrame to modify. MUST be AV_PIX_FMT_YUV420P.
** @param x      Top-left X coordinate of the blur box.
** @param y      Top-left Y coordinate of the blur box.
** @param w      Width of the blur box.
** @param h      Height of the blur box.
** @param r      Blur radius (strength).
*/                  
void 
mml_frame_blur(AVFrame* frame, 
               int x, 
               int y,
               int w,
               int h,
               int r);      
               

/*!
** @brief Draws a hollow rectangle (border) on a video frame.
**
** This function manually manipulates pixel data to draw a colored border.
** It is designed specifically for **AV_PIX_FMT_YUV420P**.
**
** @param frame  Target frame. Must be YUV420P.
** @param x      Top-left X coordinate of the rectangle.
** @param y      Top-left Y coordinate of the rectangle.
** @param w      Width of the rectangle.
** @param h      Height of the rectangle.
** @param bw     Border width (thickness) in pixels.
** @param yc     Luma (Y) color component (0-255).
** @param uc     Chroma (U) color component (0-255).
** @param vc     Chroma (V) color component (0-255).
*/               
void 
mml_frame_rect(AVFrame* frame, 
               int x, 
               int y, 
               int w, 
               int h,
               int bw,
               int yc,
               int uc,
               int vc);

/*!
** @brief Draws a hollow circle with a specified border thickness on a video frame.
**
** This function manually manipulates pixel data using the equation (x-cx)^2 + (y-cy)^2 = r^2.
** It is designed specifically for **AV_PIX_FMT_YUV420P**.
**
** @param frame  Target frame. Must be YUV420P.
** @param cx     Center X coordinate.
** @param cy     Center Y coordinate.
** @param rad    Inner radius of the circle.
** @param bw     Border width (thickness) in pixels.
** @param yc     Luma (Y) color component (0-255).
** @param uc     Chroma (U) color component (0-255).
** @param vc     Chroma (V) color component (0-255).
*/
void 
mml_frame_circle(AVFrame* frame, 
                 int cx, 
                 int cy, 
                 int rad, 
                 int bw,
                 int yc,
                 int uc,
                 int vc); 
#ifdef __cplusplus
}
#endif                 

#endif // __LIBMML_FRAME_H__                 