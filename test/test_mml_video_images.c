#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include  "libmml.h"

#define IN_FILE "/Users/christian/Downloads/test.mp4"
#define OUT_FILE "/Users/christian/Downloads/test_out.mp4"

int main(int argc, char **argv) {
  clock_t start, end;
  double cpu_time_used;
  start = clock();
  mml_video_save_images(IN_FILE, 5, 20, "/Users/christian/Downloads/frames", 1);
  end = clock();
  cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("test_mml_video_images elapsed CPU time: %f seconds\n", cpu_time_used);
}

