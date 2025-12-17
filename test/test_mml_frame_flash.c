/**
 * FFmpeg C API: Image Flash Effect (Whiteout)
 * 
 * Logic:
 * 1. Load Image.
 * 2. Loop frames.
 * 3. Calculate 'Intensity' (0.0 to 1.0) based on a Triangle Wave (Up then Down).
 * 4. Blend pixels towards White (Y=255, UV=128).
 * 5. Encode.
 * 
 * Compile:
 * gcc flash_effect.c -o flash_effect -lavformat -lavcodec -lavutil -lswscale
 */

#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include "libmml-image.h"
#include "libmml-frame.h"
#include "libmml-video.h"

#define OUT_FILE "flash_output.mp4"
#define FPS 30
#define FLASH_DURATION_FRAMES 15 // Short, sharp flash

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  const char* image_path = "../../data/1.jpg";

  // 1. Setup
  AVFrame* base_frame = NULL;
  mml_image_frame(image_path, &base_frame);

  AVFormatContext* out_fmt = NULL;
  AVCodecContext* enc_ctx = NULL; 
  AVStream* out_st = NULL; 

  mml_image_load(image_path, &base_frame, OUT_FILE, &out_fmt, &enc_ctx, &out_st);

  AVFrame* work_frame = NULL; // Lazy allocated in helper
  int total_frames = 60;      // 2 seconds total

  printf("Generating Flash... Output: %s\n", OUT_FILE);

  // 2. Loop
  for (int i = 0; i < total_frames; i++) {
    
    // --- INTENSITY LOGIC (The Flash) ---
    // We want a sharp spike.
    // Frame 0-5: Normal
    // Frame 5-10: Rapidly go to White
    // Frame 10-25: Fade back to Normal
    float intensity = 0.0f;
    
    int start_flash = 5;
    int peak_flash  = 10;
    int end_flash   = 25;

    if (i >= start_flash && i < peak_flash) {
      // Ramp Up (0 -> 1)
      intensity = (float)(i - start_flash) / (peak_flash - start_flash);
    } else if (i >= peak_flash && i < end_flash) {
      // Ramp Down (1 -> 0)
      intensity = 1.0f - (float)(i - peak_flash) / (end_flash - peak_flash);
    } else {
      intensity = 0.0f;
    }

    // Apply Effect
    mml_frame_flash(base_frame, &work_frame, intensity);

    work_frame->pts = i;
    mml_frame_write(enc_ctx, out_fmt, out_st, work_frame);
    if (i % 10 == 0) printf("Frame %d | Intensity: %.2f\n", i, intensity);
  }

  // 3. Cleanup
  mml_frame_write(enc_ctx, out_fmt, out_st, NULL);
  av_write_trailer(out_fmt);
  av_frame_free(&base_frame);
  av_frame_free(&work_frame);
  avcodec_free_context(&enc_ctx);
  avio_closep(&out_fmt->pb);
  avformat_free_context(out_fmt);

  printf("Done.\n");
  return 0;
}