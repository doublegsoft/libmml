cmake_minimum_required(VERSION 3.13)

project(libmml C)

set(CMAKE_C_STANDARD            99)
set(CMAKE_C_FLAGS               "${CMAKE_C_FLAGS}")

set(GFC_DIR                     "3rd/gfc")
set(FFMPEG_DIR                  "3rd/ffmpeg")

set(WITH_GFC_CRYPTO OFF CACHE BOOL "gfc crypto" FORCE)
set(WITH_GFC_JVM OFF CACHE BOOL "gfc jvm" FORCE)
set(WITH_GFC_LUA OFF CACHE BOOL "gfc lua" FORCE)
add_subdirectory("${GFC_DIR}")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(BUILD                    "build/darwin")
endif()    

set(LIBMML_SRC
  "src/libmml.c" 
  "src/libmml-frame.c"
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
)

link_directories(
  "${FFMPEG_DIR}/${BUILD}/libavcodec"
  "${FFMPEG_DIR}/${BUILD}/libavdevice"
  "${FFMPEG_DIR}/${BUILD}/libavfilter"
  "${FFMPEG_DIR}/${BUILD}/libavformat"
  "${FFMPEG_DIR}/${BUILD}/libavutil"
  "${FFMPEG_DIR}/${BUILD}/libswresample"
  "${FFMPEG_DIR}/${BUILD}/libswscale"
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

add_executable(test_frames_in_video
  "test/test_frames_in_video.c"
)

target_link_libraries(test_frames_in_video PRIVATE
  mml
)

add_executable(test_mml_video_images
  "test/test_mml_video_images.c"
)

target_link_libraries(test_mml_video_images PRIVATE
  mml
)