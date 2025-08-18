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
  const char* video_path = "../../data/V3.mp4";
  const char* output_path = "../../data/V3.10.mp4";
  int rc = mml_video_cut(video_path, 10.0, 20.0, output_path);
  if (rc != MML_SUCCESS)
    printf("error: %s\n", mml_error());
	return 0;
}
