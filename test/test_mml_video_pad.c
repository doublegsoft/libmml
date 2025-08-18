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
  const char* video_path = "../../data/V1.mp4";
  const char* output_path = "../../data/V1P_1920x1080.mp4";
  int rc = mml_video_pad(video_path, output_path, 1920, 1080);
  if (rc != MML_SUCCESS)
    printf("error: %s\n", mml_error());
	return 0;
}
