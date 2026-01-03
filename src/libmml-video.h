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


/*!
** @brief Transcodes and merges a video stream with an audio stream.
**
** This function implements a specific logic for content creation:
** 1. **Video Dominant**: The output duration is determined strictly by the video length.
** 2. **Audio Looping**: If the audio is shorter than the video, it loops automatically.
** 3. **Audio Cutting**: If the audio is longer than the video, it cuts off when video ends.
** 4. **PTS Reset**: Both streams get new, synthetic timestamps starting from 0.
**
** @param in_v_fmt     Input Video Format Context.
** @param v_dec_ctx    Input Video Decoder Context.
** @param in_v_idx     Input Video Stream Index.
** @param in_a_fmt     Input Audio Format Context.
** @param a_dec_ctx    Input Audio Decoder Context.
** @param in_a_idx     Input Audio Stream Index.
** @param out_fmt      Output Format Context (Muxer).
** @param v_enc_ctx    Output Video Encoder (defines output resolution/fps).
** @param out_v_stream Output Video Stream.
** @param a_enc_ctx    Output Audio Encoder (defines sample rate/format).
** @param out_a_stream Output Audio Stream.
** @return int         MML_SUCCESS (0) on success.
*/
int 
mml_video_audio(AVFormatContext* in_v_fmt, 
                AVCodecContext* v_dec_ctx, 
                int in_v_idx,
                AVFormatContext* in_a_fmt, 
                AVCodecContext* a_dec_ctx,
                int in_a_idx,
                AVFormatContext* out_fmt, 
                AVCodecContext* v_enc_ctx,
                AVStream* out_v_stream,
                AVCodecContext* a_enc_ctx,
                AVStream* out_a_stream);              

#ifdef __cplusplus
}
#endif

#endif