/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <stdio.h>
#include "libmml.h"

int main(int argc, char* argv[])
{
  const char* video_path1 = "../../data/V1.mp4";
  const char* video_path2 = "../../data/V2.mp4";
  const char* output_path = "../../data/V1V2.mp4";
  int rc = mml_video_concat(video_path1, video_path2, output_path);
  if (rc != MML_SUCCESS)
    printf("error: %s\n", mml_error());
	return 0;
}
