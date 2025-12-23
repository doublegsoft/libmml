/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_VIDEO_H__
#define __LIBMML_VIDEO_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

/*!
** @brief Initializes the full transcoding pipeline: Input -> Decoder -> Encoder -> Output.
**
** This function opens an input video file, sets up the decoder, and optionally
** sets up an H.264 encoder and output file context if an output path is provided.
**
** @param filepath    Path to the source video file.
** @param ifmt_ctx    [Out] Pointer to the Input Format Context (Container).
** @param dec_ctx     [Out] Pointer to the Decoder Context.
** @param outpath     Path to the destination file (pass NULL to only load input).
** @param ofmt_ctx    [Out] Pointer to the Output Format Context (Muxer).
** @param enc_ctx     [Out] Pointer to the Encoder Context.
** @param out_stream  [Out] Pointer to the Output Stream.
** @param video_idx   [Out] Index of the best video stream in the input file.
**
** @return int        MML_SUCCESS (0) on success, or 1 on failure.
*/
int
mml_video_load(const char*          filepath,
               AVFormatContext**    ifmt_ctx,
               AVCodecContext**     dec_ctx,
               int*                 video_idx,
               const char*          outpath,
               AVFormatContext**    ofmt_ctx,
               AVCodecContext**     enc_ctx,
               AVStream**           out_stream);


/*!
** @brief Transcodes a single video file and appends it to an output stream.
**
** This function performs the following steps:
** 1. Decodes the input video file.
** 2. Scales/Converts input frames to match the output encoder's resolution and pixel format.
** 3. Re-timestamps frames to ensure continuous playback (Concatenation).
** 4. Encodes and writes the frames to the output file.
**
** @param filename     [In] Path to the input video file to append.
** @param out_fmt_ctx  [In] Output Format Context (Muxer).
** @param out_enc_ctx  [In] Output Codec Context (Encoder, defines Output Resolution).
** @param out_stream   [In] Output Stream.
** @param out_pts      [In/Out] Pointer to the global monotonic PTS counter. 
**                     Increments for every frame written.
** @return int         MML_SUCCESS (0) on success.
*/
int 
mml_video_concat(const char* filename, 
                 AVFormatContext* out_fmt_ctx, 
                 AVCodecContext* out_enc_ctx, 
                 AVStream* out_stream, 
                 int64_t* out_pts);

/*!
** @brief Transcodes a video stream into slow motion by duplicating frames.
**
** This function reads from an input context, decodes frames, and writes them 
** multiple times to the output context to achieve a slow-down effect.
**
** @param ifmt_ctx    Input Format Context (Demuxer).
** @param dec_ctx     Input Decoder Context.
** @param vid_idx     Index of the video stream in the input file.
** @param ofmt_ctx    Output Format Context (Muxer).
** @param enc_ctx     Output Encoder Context.
** @param out_stream  Output Stream.
** @param slow_factor How many times to repeat each frame (e.g., 2 = 0.5x speed).
** @param next_pts    [In/Out] Pointer to the global monotonic PTS counter.
** @return int        MML_SUCCESS (0) on success.
*/
int 
mml_video_slow(AVFormatContext* ifmt_ctx, 
               AVCodecContext* dec_ctx, 
               int vid_idx,
               AVFormatContext* ofmt_ctx, 
               AVCodecContext* enc_ctx, 
               AVStream* out_stream,
               int slow_factor,
               int64_t* next_pts);      
               
               

/*!
** @brief Transcodes a video stream in reverse order, with optional slow motion.
**
** @note **High Memory Usage**: This function buffers *every decoded frame* into RAM 
**       before writing. 10 seconds of 1080p video can consume ~1.5GB RAM.
**       Do not use on very long videos.
**
** @param ifmt_ctx    Input Format Context.
** @param dec_ctx     Input Decoder Context.
** @param vid_idx     Index of the video stream.
** @param ofmt_ctx    Output Format Context.
** @param enc_ctx     Output Encoder Context.
** @param out_stream  Output Stream.
** @param slow_factor Frame duplication factor (e.g., 2 = 0.5x speed).
** @param next_pts    [In/Out] Pointer to global continuous PTS counter.
** @return int        MML_SUCCESS (0) on success.
*/
int 
mml_video_reverse(AVFormatContext* ifmt_ctx, 
                  AVCodecContext* dec_ctx, 
                  int vid_idx,
                  AVFormatContext* ofmt_ctx, 
                  AVCodecContext* enc_ctx, 
                  AVStream* out_stream,
                  int slow_factor,
                  int64_t* next_pts);


/*!
** @brief Transcodes a specific segment (Clip) of a video stream.
**
** This function decodes the input stream, checks the timestamp of every frame,
** and only encodes frames that fall within the [start, end] time window.
**
** @note **Performance**: This implementation reads from the current position. 
**       If the stream is at the beginning, it will decode (and discard) everything 
**       until `start_time` is reached. For better performance on long videos, 
**       `av_seek_frame` should be called before this function.
**
** @param ifmt_ctx    Input Format Context (Demuxer).
** @param dec_ctx     Input Decoder Context.
** @param vid_idx     Index of the video stream.
** @param ofmt_ctx    Output Format Context (Muxer).
** @param enc_ctx     Output Encoder Context.
** @param out_stream  Output Stream.
** @param start_time  Start timestamp string (e.g., "00:00:10").
** @param end_time    End timestamp string (e.g., "00:00:20").
** @param next_pts    [In/Out] Global monotonic PTS counter for the output.
** @return int        MML_SUCCESS (0) on success.
*/
int 
mml_video_clip(AVFormatContext* ifmt_ctx, 
               AVCodecContext* dec_ctx, 
               int vid_idx,
               AVFormatContext* ofmt_ctx, 
               AVCodecContext* enc_ctx, 
               AVStream* out_stream,
               const char* start_time,
               const char* end_time,
               int64_t* next_pts);

#ifdef __cplusplus
}
#endif

#endif