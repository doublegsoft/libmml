cmake_minimum_required(VERSION 3.13)

project(libmml C)

set(CMAKE_C_STANDARD            99)
set(CMAKE_C_FLAGS               "${CMAKE_C_FLAGS}")

set(GFC_DIR                     "3rd/gfc")
set(FFMPEG_DIR                  "3rd/ffmpeg")
set(STB_DIR                     "3rd/stb")

set(WITH_GFC_CRYPTO OFF CACHE BOOL "gfc crypto" FORCE)
set(WITH_GFC_JVM OFF CACHE BOOL "gfc jvm" FORCE)
set(WITH_GFC_LUA OFF CACHE BOOL "gfc lua" FORCE)
add_subdirectory("${GFC_DIR}")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(BUILD                    "build/darwin")
endif()    

set(LIBMML_SRC
  "src/libmml.c" 
  "src/libmml-error.c"
   "src/libmml-util.c"
  "src/libmml-frame.c"
  "src/libmml-image.c"
  "src/libmml-text.c"
  "src/libmml-effect.c"
  "src/libmml-shape.c"
  "src/libmml-pixel.c"
  "src/libmml-audio.c"
  "src/libmml-video.c"
)

set(LIBMML_LIB
  gfc
  avcodec
  avfilter
  avformat
  avutil
  swresample
  swscale
)

include_directories(
  "src"
  "${GFC_DIR}/include"
  "${FFMPEG_DIR}"
  "${FFMPEG_DIR}/${BUILD}"
  "${STB_DIR}"
)

link_directories(
  "/opt/homebrew/Cellar/ffmpeg/8.0.1/lib"
  # "${FFMPEG_DIR}/${BUILD}/libavcodec"
  # "${FFMPEG_DIR}/${BUILD}/libavdevice"
  # "${FFMPEG_DIR}/${BUILD}/libavfilter"
  # "${FFMPEG_DIR}/${BUILD}/libavformat"
  # "${FFMPEG_DIR}/${BUILD}/libavutil"
  # "${FFMPEG_DIR}/${BUILD}/libswresample"
  # "${FFMPEG_DIR}/${BUILD}/libswscale"
)

add_library(mml STATIC ${LIBMML_SRC})
add_library(mml_shared SHARED ${LIBMML_SRC})
set_target_properties(mml_shared PROPERTIES OUTPUT_NAME mml)

target_link_libraries(mml PRIVATE
  ${LIBMML_LIB}
)

target_link_libraries(mml_shared PRIVATE
  ${LIBMML_LIB}
)

add_executable(test_mml_audio_remove
  "test/test_mml_audio_remove.c"
)

target_link_libraries(test_mml_audio_remove PRIVATE
  mml
)

add_executable(test_mml_video_analysis
  "test/test_mml_video_analysis.c"
)

target_link_libraries(test_mml_video_analysis PRIVATE
  mml
)

add_executable(test_mml_video_info
  "test/test_mml_video_info.c"
)

target_link_libraries(test_mml_video_info PRIVATE
  mml
)

add_executable(test_mml_audio_extract
  "test/test_mml_audio_extract.c"
)

target_link_libraries(test_mml_audio_extract PRIVATE
  mml
)

add_executable(test_mml_video_resize
  "test/test_mml_video_resize.c"
)

target_link_libraries(test_mml_video_resize PRIVATE
  mml
)

add_executable(test_mml_video_pad
  "test/test_mml_video_pad.c"
)

target_link_libraries(test_mml_video_pad PRIVATE
  mml
)

add_executable(test_mml_video_concat
  "test/test_mml_video_concat.c"
)

target_link_libraries(test_mml_video_concat PRIVATE
  mml
)

add_executable(test_mml_video_cut
  "test/test_mml_video_cut.c"
)

target_link_libraries(test_mml_video_cut PRIVATE
  mml
)

add_executable(test_mml_video_clip
  "test/test_mml_video_clip.c"
)

target_link_libraries(test_mml_video_clip PRIVATE
  mml
)

add_executable(test_mml_video_video
  "test/test_mml_video_video.c"
)

target_link_libraries(test_mml_video_video PRIVATE
  mml
)

add_executable(test_mml_video_audio
  "test/test_mml_video_audio.c"
)

target_link_libraries(test_mml_video_audio PRIVATE
  mml
)

add_executable(test_mml_video_images
  "test/test_mml_video_images.c"
)

target_link_libraries(test_mml_video_images PRIVATE
  mml
)

add_executable(test_mml_effect_rotate
  "test/test_mml_effect_rotate.c"
)

target_link_libraries(test_mml_effect_rotate PRIVATE
  mml
)

add_executable(test_mml_effect_fade
  "test/test_mml_effect_fade.c"
)

target_link_libraries(test_mml_effect_fade PRIVATE
  mml
)

add_executable(test_mml_effect_pixelate
  "test/test_mml_effect_pixelate.c"
)

target_link_libraries(test_mml_effect_pixelate PRIVATE
  mml
)

add_executable(test_mml_text_ltr
  "test/test_mml_text_ltr.c"
)

target_link_libraries(test_mml_text_ltr PRIVATE
  mml
)

add_executable(test_mml_shape_ellipse
  "test/test_mml_shape_ellipse.c"
)

target_link_libraries(test_mml_shape_ellipse PRIVATE
  mml
)

add_executable(test_mml_shape_quad
  "test/test_mml_shape_quad.c"
)

target_link_libraries(test_mml_shape_quad PRIVATE
  mml
)

add_executable(test_mml_shape_ring
  "test/test_mml_shape_ring.c"
)

target_link_libraries(test_mml_shape_ring PRIVATE
  mml
)

add_executable(test_mml_shape_arrow
  "test/test_mml_shape_arrow.c"
)

target_link_libraries(test_mml_shape_arrow PRIVATE
  mml
)

add_executable(test_mml_shape_line
  "test/test_mml_shape_line.c"
)

target_link_libraries(test_mml_shape_line PRIVATE
  mml
)

add_executable(test_mml_video_slow
  "test/test_mml_video_slow.c"
)

target_link_libraries(test_mml_video_slow PRIVATE
  mml
)

add_executable(test_mml_video_grid
  "test/test_mml_video_grid.c"
)

target_link_libraries(test_mml_video_grid PRIVATE
  mml
)

add_executable(test_mml_video_scroll
  "test/test_mml_video_scroll.c"
)

target_link_libraries(test_mml_video_scroll PRIVATE
  mml
)

add_executable(test_mml_video_reverse
  "test/test_mml_video_reverse.c"
)

target_link_libraries(test_mml_video_reverse PRIVATE
  mml
)

add_executable(test_mml_image_frame
  "test/test_mml_image_frame.c"
)


target_link_libraries(test_mml_image_frame PRIVATE
  mml
)

add_executable(test_mml_image_concat
  "test/test_mml_image_concat.c"
)

target_link_libraries(test_mml_image_concat PRIVATE
  mml
)

add_executable(test_mml_frame_rotate
  "test/test_mml_frame_rotate.c"
)

target_link_libraries(test_mml_frame_rotate PRIVATE
  mml
)

add_executable(test_mml_frame_wipe
  "test/test_mml_frame_wipe.c"
)

target_link_libraries(test_mml_frame_wipe PRIVATE
  mml
)

add_executable(test_mml_frame_flash
  "test/test_mml_frame_flash.c"
)

target_link_libraries(test_mml_frame_flash PRIVATE
  mml
)

add_executable(test_mml_frame_smoke
  "test/test_mml_frame_smoke.c"
)

target_link_libraries(test_mml_frame_smoke PRIVATE
  mml
)

add_executable(test_mml_frame_pixelate
  "test/test_mml_frame_pixelate.c"
)

target_link_libraries(test_mml_frame_pixelate PRIVATE
  mml
)

add_executable(test_mml_frame_pixelarea
  "test/test_mml_frame_pixelarea.c"
)

target_link_libraries(test_mml_frame_pixelarea PRIVATE
  mml
)

add_executable(test_mml_frame_blur
  "test/test_mml_frame_blur.c"
)

target_link_libraries(test_mml_frame_blur PRIVATE
  mml
)

add_executable(zoom
  "test/zoom.c"
)

target_link_libraries(zoom PRIVATE
  mml
)

add_executable(magnify
  "test/magnify.c"
)

target_link_libraries(magnify PRIVATE
  mml
)

add_executable(crack
  "test/crack.c"
)

target_link_libraries(crack PRIVATE
  mml
)

add_executable(alpha
  "test/alpha.c"
)

target_link_libraries(alpha PRIVATE
  mml
)

add_executable(ellipse
  "test/ellipse.c"
)

target_link_libraries(ellipse PRIVATE
  mml
)