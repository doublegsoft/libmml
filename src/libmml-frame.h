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
** @brief Creates a "Deep Copy" of an AVFrame, converting it to YUV420P.
**
** Decoders typically reuse internal memory buffers. To store frames in a list 
** (e.g., for reversing a video), we must allocate new memory and copy the pixel data.
** This function also normalizes the pixel format to **AV_PIX_FMT_YUV420P** to 
** ensure compatibility with the encoder later.
**
** @param src        [In] The source frame (usually from the decoder).
** @param sws_cache  [In/Out] Double pointer to a SwsContext. 
**                   - Used to cache the scaler context between calls to avoid 
**                     re-initializing it for every frame (performance optimization).
**                   - If *sws_cache is NULL, it is created.
** @return AVFrame*  A newly allocated, independent frame containing the pixel data.
**                   Returns NULL on allocation failure.
*/
AVFrame* 
mml_frame_deep(AVFrame* src, 
               struct SwsContext** sws_cache);

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

/*!
** @brief Resizes a video frame using libswscale with caching and lazy allocation.
**
** This function scales an input frame to a new width/height. It handles:
** 1. Allocating the destination frame if it doesn't exist (Lazy Allocation).
** 2. Initializing the SwsContext if it doesn't exist (Caching).
** 3. Performing the scaling operation.
** 4. Copying timestamps to maintain synchronization.
**
** @param src     [In] Source frame.
** @param dst     [In/Out] Pointer to the destination frame pointer. 
**                If *dst is NULL, it will be allocated.
** @param sws     [In/Out] Pointer to the SwsContext pointer.
**                If *sws is NULL, it is created. It should be freed by caller later.
** @param width   Target width.
** @param height  Target height.
** @return int    0 on success, negative AVERROR on failure.
*/
int 
mml_frame_resize(AVFrame* src, AVFrame** dst, struct SwsContext** sws, int width, int height);    

/*!
** @brief Resizes an input frame to fit into a destination canvas while maintaining aspect ratio.
**
** This function performs "Letterboxing" or "Pillarboxing". It scales the input image
** to a specific size (`pic_width` x `pic_height`) and places it at a specific offset
** within a larger canvas (`frame_width` x `frame_height`), filling the background with black.
**
** @note **Format Constraint**: This function logic (memset 0/128 and subsampling math) 
**       assumes the pixel format is **AV_PIX_FMT_YUV420P**.
**
** @param src           [In] Source frame.
** @param dst           [In/Out] Pointer to destination frame. Lazy allocation supported.
** @param sws           [In/Out] Pointer to SwsContext. Created if NULL.
** @param frame_width   Width of the final output canvas (e.g., 1920).
** @param frame_height  Height of the final output canvas (e.g., 1080).
** @param pic_width     Width of the scaled actual image (e.g., 1440).
** @param pic_height    Height of the scaled actual image (e.g., 1080).
** @param offset_x      Horizontal offset to center the image (should be even).
** @param offset_y      Vertical offset to center the image (should be even).
** @return int          0 on success, negative AVERROR on failure.
*/
int 
mml_frame_aspect(AVFrame* src, 
                 AVFrame** dst, 
                 struct SwsContext** sws, 
                 int frame_width, int frame_height,
                 int pic_width, int pic_height,
                 int offset_x, int offset_y);

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

/*!
** @brief Applies a Fade-In effect (Black -> Normal) to a YUV420P frame.
**
** This function modifies pixel data directly.
** 
** @param frame         The target AVFrame (must be writable).
** @param current_time  The current timestamp of the frame in seconds.
** @param duration      The total duration of the fade effect in seconds.
*/
void mml_frame_fade(AVFrame* frame, 
                    double current_time, 
                    double duration);

/*！
** @brief Rotates a video frame by an arbitrary angle.
**
** This function performs a geometric rotation on the pixel data. 
** It handles the YUV420P format by rotating the Luma plane at full resolution
** and the Chroma planes at half resolution.
**
** @param base   [In] Source frame (Read-only). Must be YUV420P.
** @param work   [In/Out] Double pointer to the destination frame. 
**               - If *work is NULL, it will be allocated.
**               - If *work exists, it is reused (faster for animation loops).
** @param angle  Rotation angle in degrees (0.0 to 360.0).
** @return int   0 on success, -1 on error.
*/
void 
mml_frame_rotate(AVFrame* base, 
                 AVFrame** work, 
                 float angle);

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
mml_frame_wipe(AVFrame* src, 
               AVFrame** work, 
               float progress);

/*!
** @brief Applies a "Flash" or "Whiteout" effect to a video frame.
**
** This function blends the source image towards "Pure White" based on the intensity.
** - Luma (Y) is blended towards 255 (Maximum Brightness).
** - Chroma (U/V) is blended towards 128 (No Color/Grayscale).
**
** @note This implementation assumes **AV_PIX_FMT_YUV420P**.
**
** @param src       [In] The source frame (Original image).
** @param work      [In/Out] Double pointer to the destination frame. 
**                  If *work is NULL, it will be allocated automatically (Lazy Allocation).
** @param intensity Float value from 0.0 (No Flash) to 1.0 (Full White).
** @return int      0 on success, -1 on allocation failure.
*/
int 
mml_frame_flash(AVFrame* src, 
                AVFrame** work, 
                float intensity);

/*!
** @brief Applies a pixelation (mosaic) effect to a complete YUV420P video frame.
**
** This function handles the specific structural requirements of YUV420P:
** 1. The Luma plane (Y) is processed at full resolution.
** 2. The Chroma planes (U/V) are processed at half resolution.
** 3. The block size is forced to be even to ensure alignment between Luma and Chroma blocks.
**
** @param frame      [In/Out] The target frame to modify. Must be AV_PIX_FMT_YUV420P.
** @param block_size The desired size of the mosaic squares (in Luma pixels). 
**                   If odd, it will be rounded down to the nearest even number.
*/
void 
mml_frame_pixelate(AVFrame* src, AVFrame** work, int block_size);

/*!
** @brief Saves a specific rectangular area of a frame as a JPEG image.
**
** This function performs a Crop -> Convert -> Encode pipeline.
**
** @note Input frame MUST be **AV_PIX_FMT_YUV420P**.
**       Coordinates (x, y, w, h) must be even numbers for chroma alignment.
**
** @param src       Source frame (Full video frame).
** @param filename  Output path (e.g. "crop.jpg").
** @param x         Top-left X coordinate of crop.
** @param y         Top-left Y coordinate of crop.
** @param w         Width of crop.
** @param h         Height of crop.
** @return int      0 on success, -1 on failure.
*/
int 
mml_frame_image(AVFrame* src, const char* filename, int x, int y, int w, int h);

/*!
** @brief Executes a Lua script function to process a specific video frame.
**
** This function initializes a Lua environment, loads the script, and calls
** the global Lua function 'process_frame(frame_ptr, width, height, index)'.
**
** @note **Performance Warning**: This function initializes a new Lua state 
**       every call. For video processing, the Lua state should ideally be 
**       created once during initialization and reused here.
**
** @param frame    [In/Out] The AVFrame to be processed.
** @param index    The current frame index (e.g., 0, 1, 2...).
** @param lua_file Path to the .lua script file.
** @param error    [Out] Pointer to a string to return error messages (optional).
** @return int     MML_SUCCESS (0) on success, non-zero on failure.
*/
int 
mml_frame_lua(AVFrame* base, AVFrame* work, int start_pts, int present_pts, const char* lua_file, char** error);

#ifdef __cplusplus
}
#endif                 

#endif // __LIBMML_FRAME_H__                 