/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_ERROR_H__
#define __LIBMML_ERROR_H__

#ifdef __cplusplus
extern "C"
{
#endif

#define MML_SUCCESS                             0
#define MML_ERROR_NO_CONTENT                    204
#define MML_ERROR_NOT_MODIFIED                  304  
#define MML_ERROR_NOT_FOUND                     404  
  
#define MML_ERROR_FILE_NOT_EXIST                400404
#define MML_ERROR_FILE_NOT_CREATED              400405
#define MML_ERROR_FILE_OPEN_FAILED              400406
#define MML_ERROR_FILE_NOT_WRITTEN              400409
  
#define MML_ERROR_CODEC_NOT_FOUND               500404
#define MML_ERROR_CODEC_NOT_CREATED             500405
#define MML_ERROR_CODEC_OPEN_FAILED             500406
#define MML_ERROR_CODEC_NOT_COPIED              500410
  
#define MML_ERROR_STREAM_NOT_FOUND              600404
#define MML_ERROR_STREAM_NOT_CREATED            600405
#define MML_ERROR_STREAM_OPEN_FAILED            600406
#define MML_ERROR_STREAM_WRITE_FAILED           600407

#define MML_ERROR_PACKET_NOT_CREATED            700405

#define MML_ERROR_FRAME_NOT_CREATED             710405
#define MML_ERROR_FRAME_NOT_SENT                710408
#define MML_ERROR_FRAME_NOT_WRITTEN             710409
  
#define MML_ERROR_FORMAT_NOT_CREATED            720405  

typedef struct mml_error_s mml_error_t;


void
mml_error_set(int code, const char* msg);

const char*
mml_error_msg(void);

int
mml_error_code(void);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_ERROR_H__