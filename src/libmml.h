/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_H__
#define __LIBMML_H__

#ifdef __cplusplus
extern "C" 
{
#endif
  
#include "libmml-error.h"
#include "libmml-frame.h"

struct mml_encoder_s;
struct mml_decoder_s;

typedef struct mml_encoder_s mml_encoder_t;
typedef struct mml_decoder_s mml_decoder_t;

typedef mml_encoder_t* mml_encoder_p;
typedef mml_decoder_t* mml_decoder_p;

int
mml_encoder_init(mml_encoder_p* encoder, int encoder_id);

void
mml_encoder_free(mml_encoder_p encoder);

mml_decoder_p
mml_decoder_init();

/*!
** Gets the last error message.
**
** @return last error message
*/  
const char*
mml_error(void);  
  
/*!
** Removes audio stream in video file.
**
** @param original_video_path
**        the original video path
**
** @param output_video_path
**        the output video path without audio stream. if the parameter is NULL, 
**         the output directory is the same as orginal video path and the output
**         file name is the origin file name plus '_without_audio' suffix.
**
** @return success or error code
*/
int  
mml_audio_remove(const char* original_video_path, const char* output_video_path);

/*
** Checks video file having audio stream.
**
** @param original_video_path
**        the original video path
**
** @return non-zero means true and 0 means false
*/
int
mml_audio_exist(const char* original_video_path);  

/*!
** Extracts audio stream as a file from video file.
**
** @param original_video_path
**        the original video path
**
** @param output_audio_path
**        the output audio path
**
** @return success or error code
*/
int  
mml_audio_extract(const char* original_video_path, 
                  const char* output_audio_path);  

/*!
** Gets video resolution information.
**
** @param original_video_path
**        the original video path
**
** @param width [out]
**        the video width
**
** @param height [out]
**        the video height
**
** @return success or error code
*/
int 
mml_video_resolution(const char* original_video_path, 
                     int* width, 
                     int* height);

/*!
** Resizes video stream to a new size.
**
** @param original_path
**        the original video path
**
** @param output_path
**        the output video path
**
** @param width 
**        the new video width
**
** @param height
**        the new video height
**
** @return success or error code
*/  
int
mml_video_resize(const char* original_path, 
                 const char* output_path, 
                 int width, 
                 int height);

int
mml_video_pad(const char* original_path, 
              const char* output_path, 
              int width, 
              int height);  
  
/*!
** Concatenates two videos into one.
**
** @param original_path1
**        the first original video path
**
** @param original_path2
**        the second original video path
**
** @param output_path
**        the output video path
**
** @return success or error code
*/  
int
mml_video_concat(const char* original_path1, 
                 const char* original_path2, 
                 const char* output_path);  
  
int
mml_video_cut(const char* original_path, 
              double start_time,
              double end_time,
              const char* output_path);  
  
int
mml_video_add_audio(const char* original_video_path, 
                      const char* original_audio_path, 
                     const char* output_path);
                    
/*!
** Saves a segment of video as images under the given output path. Only 
** keyframes are saved.
**
** @param original_path
**        the original video path
**
** @param start_time
**        the start time of the segment
**
** @param end_time
**        the end time of the segment
**
** @param output_path
**        the output image directory path
**
** @param image_index
**        the index of the first image
**
** @return success or error code
*/
int
mml_video_save_images(const char* original_path, 
                      double start_time, 
                      double end_time, 
                      const char* output_path, 
                      int image_index);
                                          
/*!
**
*/
int  
mml_script_add(const char* original_video_path, const char* output_script_path);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_H__