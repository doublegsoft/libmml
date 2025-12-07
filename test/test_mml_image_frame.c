#include <stdio.h>
#include <math.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-image.h"
#include "libmml-frame.h"

int main(int argc, char* argv[]) {

  AVFrame* frame = NULL;
  mml_image_frame("../../data/1.jpg", &frame);
  printf("%d, %d\n", frame->width, frame->height);
  mml_frame_save(frame, "/Users/christian/Downloads/a.jpg");
  return 0;
}