/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_IMAGE_H__
#define __LIBMML_IMAGE_H__

#ifdef __cplusplus
extern "C"
{
#endif

/*！
** 辅助函数：加载任意图片并将其强制缩放到指定的视频尺寸
**
** 这个函数解决了两个问题：
** 1. 格式统一：无论原图是 JPG 还是 PNG，都解码为 RGB24 格式。
** 2. 尺寸统一：无论原图多大，都缩放到 target_w * target_h，
**    以便可以直接填充到视频帧中。
**
** 注意：
** - 返回的内存是使用 malloc 分配的，调用者(Caller)有责任在使用完后 free() 它。
**
** @param filename  图片文件路径 (如 "image.jpg")
** @param target_w  目标宽度 (通常是视频宽度，如 640)
** @param target_h  目标高度 (通常是视频高度，如 480)
** @return uint8_t* 指向缩放后 RGB 数据的指针，如果加载失败返回 NULL
*/
// uint8_t* 
// mml_image_load(const char* filename, 
//                int target_w, 
//                int target_h);

/**
 * @brief Decodes an image file into a raw FFmpeg AVFrame.
 *
 * FFmpeg treats image files as a video stream containing a single frame.
 * This function sets up the demuxer and decoder, reads that single frame,
 * and returns it.
 *
 * @note The output pixel format is usually **AV_PIX_FMT_YUVJ420P** (Full Range YUV).
 *       If you need standard YUV420P for video encoding, you must convert it 
 *       using sws_scale after loading.
 *
 * @param filename   Path to the input image file (e.g., "input.jpg").
 * @param out_frame  [Out] Double pointer to an AVFrame. This function allocates 
 *                   the frame on success. The caller must free it using 
 *                   av_frame_free().
 * @return int       0 on success, or -1 on failure.
 */
int 
mml_image_frame(const char*  filename, 
                AVFrame**    out_frame);

/*!
** @brief Loads a static image and initializes a video encoding pipeline.
**
** This function performs two main tasks:
** 1. Decodes an image file (JPG/PNG) into a raw AVFrame.
** 2. Sets up an H.264 encoder and MP4 muxer with settings matching the image dimensions.
**
** @param filename   [In] Path to the source image file.
** @param out_frame  [Out] Pointer to the decoded source frame.
** @param out_path   [In] Path to the output video file (e.g., "output.mp4").
** @param ofmt_ctx   [Out] Pointer to the Output Format Context (Muxer).
** @param enc_ctx    [Out] Pointer to the Encoder Context.
** @param out_stream [Out] Pointer to the Output Stream.
** @return int       MML_SUCCESS (0) on success.
*/                
int 
mml_image_load(const char*          filename, 
               AVFrame**            out_frame,
               const char*          out_path,
               AVFormatContext**    ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_IMAGE_H__