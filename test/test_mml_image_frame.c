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
  AVFrame* resized_frame = NULL;
  struct SwsContext* sws = NULL;
  mml_image_frame("../../data/1.jpg", &frame);
  mml_frame_resize(frame, &resized_frame, &sws, 400, 400);
  mml_frame_save(resized_frame, "/Users/christian/Downloads/a.jpg");
  return 0;
}