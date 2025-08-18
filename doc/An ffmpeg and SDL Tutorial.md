# An ffmpeg and SDL Tutorial
==========================

## Tutorial 01: Making Screencaps

Code: tutorial01.c

### Overview

Movie files have a few basic components. First, the file itself is called a
**container**, and the type of container determines where the information in
the file goes. Examples of containers are AVI and Quicktime. Next, you have a
bunch of **streams**; for example, you usually have an audio stream and a
video stream. (A "stream" is just a fancy word for "a succession of data
elements made available over time".) The data elements in a stream are called
**frames**. Each stream is encoded by a different kind of **codec**. The codec
defines how the actual data is COded and DECoded - hence the name CODEC.
Examples of codecs are DivX and MP3. **Packets** are then read from the
stream. Packets are pieces of data that can contain bits of data that are
decoded into raw frames that we can finally manipulate for our application.
For our purposes, each packet contains complete frames, or multiple frames in
the case of audio.

At its very basic level, dealing with video and audio streams is very easy:

    
```basic    
10 OPEN video_stream FROM video.avi
20 READ packet FROM video_stream INTO frame
30 IF frame NOT COMPLETE GOTO 20
40 DO SOMETHING WITH frame
50 GOTO 20
```    
    

Handling multimedia with ffmpeg is pretty much as simple as this program,
although some programs might have a very complex "DO SOMETHING" step. So in
this tutorial, we're going to open a file, read from the video stream inside
it, and our DO SOMETHING is going to be writing the frame to a PPM file.

### Opening the File

First, let's see how we open a file in the first place. With ffmpeg, you have
to first initialize the library.

    
```c
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <ffmpeg/swscale.h>
...
int main(int argc, charg *argv[]) {
av_register_all();
```

This registers all available file formats and codecs with the library so they
will be used automatically when a file with the corresponding format/codec is
opened. Note that you only need to call av_register_all() once, so we do it
here in main(). If you like, it's possible to register only certain individual
file formats and codecs, but there's usually no reason why you would have to
do that.

Now we can actually open the file:

```c
AVFormatContext *pFormatCtx = NULL;
    
// Open video file
if(avformat_open_input(&pFormatCtx;, argv[1], NULL, 0, NULL)!=0)
  return -1; // Couldn't open file
```

We get our filename from the first argument. This function reads the file
header and stores information about the file format in the AVFormatContext
structure we have given it. The last three arguments are used to specify the
file format, buffer size, and format options, but by setting this to NULL or
0, libavformat will auto-detect these.

This function only looks at the header, so next we need to check out the
stream information in the file.:

```c
// Retrieve stream information
if(avformat_find_stream_info(pFormatCtx, NULL)<0)
  return -1; // Couldn't find stream information

```    

This function populates `pFormatCtx->streams` with the proper information. We
introduce a handy debugging function to show us what's inside:

    
```c
// Dump information about file onto standard error
av_dump_format(pFormatCtx, 0, argv[1], 0);

```  

Now `pFormatCtx->streams` is just an array of pointers, of size
`pFormatCtx->nb_streams`, so let's walk through it until we find a video
stream.

    
```c
int i;
AVCodecContext *pCodecCtxOrig = NULL;
AVCodecContext *pCodecCtx = NULL;
    
// Find the first video stream
videoStream=-1;
for(i=0; i<pFormatCtx->nb_streams; i++)
  if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
    videoStream=i;
    break;
  }
if(videoStream==-1)
  return -1; // Didn't find a video stream
    
// Get a pointer to the codec context for the video stream
pCodecCtx=pFormatCtx->streams[videoStream]->codec;
```

The stream's information about the codec is in what we call the "codec
context." This contains all the information about the codec that the stream is
using, and now we have a pointer to it. But we still have to find the actual
codec and open it:

```c
AVCodec *pCodec = NULL;
    
// Find the decoder for the video stream
pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
if(pCodec==NULL) {
  fprintf(stderr, "Unsupported codec!\n");
  return -1; // Codec not found
}
// Copy context
pCodecCtx = avcodec_alloc_context3(pCodec);
if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
  fprintf(stderr, "Couldn't copy codec context");
  return -1; // Error copying codec context
}
// Open codec
if(avcodec_open2(pCodecCtx, pCodec)<0)
  return -1; // Could not open codec
```    

Note that we must not use the AVCodecContext from the video stream directly!
So we have to use avcodec_copy_context() to copy the context to a new location
(after allocating memory for it, of course).

### Storing the Data

Now we need a place to actually store the frame:

    
    
    AVFrame *pFrame = NULL;
    
    // Allocate video frame
    pFrame=av_frame_alloc();
    

Since we're planning to output PPM files, which are stored in 24-bit RGB,
we're going to have to convert our frame from its native format to RGB. ffmpeg
will do these conversions for us. For most projects (including ours) we're
going to want to convert our initial frame to a specific format. Let's
allocate a frame for the converted frame now.

    
    
    // Allocate an AVFrame structure
    pFrameRGB=av_frame_alloc();
    if(pFrameRGB==NULL)
      return -1;
    

Even though we've allocated the frame, we still need a place to put the raw
data when we convert it. We use `avpicture_get_size` to get the size we need,
and allocate the space manually:

    
    
    uint8_t *buffer = NULL;
    int numBytes;
    // Determine required buffer size and allocate buffer
    numBytes=avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width,
                                pCodecCtx->height);
    buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
    

`av_malloc` is ffmpeg's malloc that is just a simple wrapper around malloc
that makes sure the memory addresses are aligned and such. It will _not_
protect you from memory leaks, double freeing, or other malloc problems.

Now we use avpicture_fill to associate the frame with our newly allocated
buffer. About the AVPicture cast: the AVPicture struct is a subset of the
AVFrame struct - the beginning of the AVFrame struct is identical to the
AVPicture struct.

    
    
    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
    // of AVPicture
    avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24,
                    pCodecCtx->width, pCodecCtx->height);
    

Finally! Now we're ready to read from the stream!

### Reading the Data

What we're going to do is read through the entire video stream by reading in
the packet, decoding it into our frame, and once our frame is complete, we
will convert and save it.

    
    
    struct SwsContext *sws_ctx = NULL;
    int frameFinished;
    AVPacket packet;
    // initialize SWS context for software scaling
    sws_ctx = sws_getContext(pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        PIX_FMT_RGB24,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
        );
    
    i=0;
    while(av_read_frame(pFormatCtx, &packet;)>=0) {
      // Is this a packet from the video stream?
      if(packet.stream_index==videoStream) {
    	// Decode video frame
        avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished;, &packet;);
        
        // Did we get a video frame?
        if(frameFinished) {
        // Convert the image from its native format to RGB
            sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
    		  pFrame->linesize, 0, pCodecCtx->height,
    		  pFrameRGB->data, pFrameRGB->linesize);
    	
            // Save the frame to disk
            if(++i<=5)
              SaveFrame(pFrameRGB, pCodecCtx->width, 
                        pCodecCtx->height, i);
        }
      }
        
      // Free the packet that was allocated by av_read_frame
      av_free_packet(&packet;);
    }
    

**A note on packets**

Technically a packet can contain partial frames or other bits of data, but
ffmpeg's parser ensures that the packets we get contain either complete or
multiple frames.

The process, again, is simple: `av_read_frame()` reads in a packet and stores
it in the `AVPacket` struct. Note that we've only allocated the packet
structure - ffmpeg allocates the internal data for us, which is pointed to by
`packet.data`. This is freed by the `av_free_packet()` later.
`avcodec_decode_video()` converts the packet to a frame for us. However, we
might not have all the information we need for a frame after decoding a
packet, so `avcodec_decode_video()` sets frameFinished for us when we have the
next frame. Finally, we use `sws_scale()` to convert from the native format
(`pCodecCtx->pix_fmt`) to RGB. Remember that you can cast an AVFrame pointer
to an AVPicture pointer. Finally, we pass the frame and height and width
information to our SaveFrame function.

Now all we need to do is make the SaveFrame function to write the RGB
information to a file in PPM format. We're going to be kind of sketchy on the
PPM format itself; trust us, it works.

    
    
    void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
      FILE *pFile;
      char szFilename[32];
      int  y;
      
      // Open file
      sprintf(szFilename, "frame%d.ppm", iFrame);
      pFile=fopen(szFilename, "wb");
      if(pFile==NULL)
        return;
      
      // Write header
      fprintf(pFile, "P6\n%d %d\n255\n", width, height);
      
      // Write pixel data
      for(y=0; y<height; y++)
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
      
      // Close file
      fclose(pFile);
    }
    

We do a bit of standard file opening, etc., and then write the RGB data. We
write the file one line at a time. A PPM file is simply a file that has RGB
information laid out in a long string. If you know HTML colors, it would be
like laying out the color of each pixel end to end like `#ff0000#ff0000`....
would be a red screen. (It's stored in binary and without the separator, but
you get the idea.) The header indicated how wide and tall the image is, and
the max size of the RGB values.

Now, going back to our main() function. Once we're done reading from the video
stream, we just have to clean everything up:

    
    
    // Free the RGB image
    av_free(buffer);
    av_free(pFrameRGB);
    
    // Free the YUV frame
    av_free(pFrame);
    
    // Close the codecs
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrig);
    
    // Close the video file
    avformat_close_input(&pFormatCtx;);
    
    return 0;
    

You'll notice we use av_free for the memory we allocated with
avcode_alloc_frame and av_malloc.

That's it for the code! Now, if you're on Linux or a similar platform, you'll
run:

    
    
    gcc -o tutorial01 tutorial01.c -lavutil -lavformat -lavcodec -lz -lavutil -lm
    

If you have an older version of ffmpeg, you may need to drop -lavutil:

    
    
    gcc -o tutorial01 tutorial01.c -lavformat -lavcodec -lz -lm
    

Most image programs should be able to open PPM files. Test it on some movie
files.

```c

// tutorial01.c
// Code based on a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101 
// on GCC 4.7.2 in Debian February 2015

// A small sample program that shows how to use libavformat and libavcodec to
// read video from a file.
//
// Use
//
// gcc -o tutorial01 tutorial01.c -lavformat -lavcodec -lswscale -lz
//
// to build (assuming libavformat and libavcodec are correctly installed
// your system).
//
// Run using
//
// tutorial01 myvideofile.mpg
//
// to write the first five frames from "myvideofile.mpg" to disk in PPM
// format.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <stdio.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
  FILE *pFile;
  char szFilename[32];
  int  y;
  
  // Open file
  sprintf(szFilename, "frame%d.ppm", iFrame);
  pFile=fopen(szFilename, "wb");
  if(pFile==NULL)
    return;
  
  // Write header
  fprintf(pFile, "P6\n%d %d\n255\n", width, height);
  
  // Write pixel data
  for(y=0; y<height; y++)
    fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
  
  // Close file
  fclose(pFile);
}

int main(int argc, char *argv[]) {
  // Initalizing these to NULL prevents segfaults!
  AVFormatContext   *pFormatCtx = NULL;
  int               i, videoStream;
  AVCodecContext    *pCodecCtxOrig = NULL;
  AVCodecContext    *pCodecCtx = NULL;
  AVCodec           *pCodec = NULL;
  AVFrame           *pFrame = NULL;
  AVFrame           *pFrameRGB = NULL;
  AVPacket          packet;
  int               frameFinished;
  int               numBytes;
  uint8_t           *buffer = NULL;
  struct SwsContext *sws_ctx = NULL;

  if(argc < 2) {
    printf("Please provide a movie file\n");
    return -1;
  }
  // Register all formats and codecs
  av_register_all();
  
  // Open video file
  if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0)
    return -1; // Couldn't open file
  
  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; // Couldn't find stream information
  
  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);
  
  // Find the first video stream
  videoStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++)
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
      videoStream=i;
      break;
    }
  if(videoStream==-1)
    return -1; // Didn't find a video stream
  
  // Get a pointer to the codec context for the video stream
  pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;
  // Find the decoder for the video stream
  pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
  if(pCodec==NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; // Codec not found
  }
  // Copy context
  pCodecCtx = avcodec_alloc_context3(pCodec);
  if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }

  // Open codec
  if(avcodec_open2(pCodecCtx, pCodec, NULL)<0)
    return -1; // Could not open codec
  
  // Allocate video frame
  pFrame=av_frame_alloc();
  
  // Allocate an AVFrame structure
  pFrameRGB=av_frame_alloc();
  if(pFrameRGB==NULL)
    return -1;

  // Determine required buffer size and allocate buffer
  numBytes=avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width,
			      pCodecCtx->height);
  buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
  
  // Assign appropriate parts of buffer to image planes in pFrameRGB
  // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
  // of AVPicture
  avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24,
		 pCodecCtx->width, pCodecCtx->height);
  
  // initialize SWS context for software scaling
  sws_ctx = sws_getContext(pCodecCtx->width,
			   pCodecCtx->height,
			   pCodecCtx->pix_fmt,
			   pCodecCtx->width,
			   pCodecCtx->height,
			   PIX_FMT_RGB24,
			   SWS_BILINEAR,
			   NULL,
			   NULL,
			   NULL
			   );

  // Read frames and save first five frames to disk
  i=0;
  while(av_read_frame(pFormatCtx, &packet)>=0) {
    // Is this a packet from the video stream?
    if(packet.stream_index==videoStream) {
      // Decode video frame
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
      
      // Did we get a video frame?
      if(frameFinished) {
	// Convert the image from its native format to RGB
	sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
		  pFrame->linesize, 0, pCodecCtx->height,
		  pFrameRGB->data, pFrameRGB->linesize);
	
	// Save the frame to disk
	if(++i<=5)
	  SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, 
		    i);
      }
    }
    
    // Free the packet that was allocated by av_read_frame
    av_free_packet(&packet);
  }
  
  // Free the RGB image
  av_free(buffer);
  av_frame_free(&pFrameRGB);
  
  // Free the YUV frame
  av_frame_free(&pFrame);
  
  // Close the codecs
  avcodec_close(pCodecCtx);
  avcodec_close(pCodecCtxOrig);

  // Close the video file
  avformat_close_input(&pFormatCtx);
  
  return 0;
}

```


## Tutorial 02: Outputting to the Screen

Code: tutorial02.c

### SDL and Video

To draw to the screen, we're going to use SDL. SDL stands for Simple Direct
Layer, and is an excellent library for multimedia, is cross-platform, and is
used in several projects. You can get the library at the official website or
you can download the development package for your operating system if there is
one. You'll need the libraries to compile the code for this tutorial (and for
the rest of them, too).

SDL has many methods for drawing images to the screen, and it has one in
particular that is meant for displaying movies on the screen - what it calls a
YUV overlay. YUV (technically not YUV but YCbCr) *** A note: **There is a
great deal of annoyance from some people at the convention of calling "YCbCr"
"YUV". Generally speaking, YUV is an analog format and YCbCr is a digital
format. ffmpeg and SDL both refer to YCbCr as YUV in their code and macros. is
a way of storing raw image data like RGB. Roughly speaking, Y is the
brightness (or "luma") component, and U and V are the color components. (It's
more complicated than RGB because some of the color information is discarded,
and you might have only 1 U and V sample for every 2 Y samples.) SDL's YUV
overlay takes in a raw array of YUV data and displays it. It accepts 4
different kinds of YUV formats, but YV12 is the fastest. There is another YUV
format called YUV420P that is the same as YV12, except the U and V arrays are
switched. The 420 means it is subsampled at a ratio of 4:2:0, basically
meaning there is 1 color sample for every 4 luma samples, so the color
information is quartered. This is a good way of saving bandwidth, as the human
eye does not percieve this change. The "P" in the name means that the format
is "planar" -- simply meaning that the Y, U, and V components are in separate
arrays. ffmpeg can convert images to YUV420P, with the added bonus that many
video streams are in that format already, or are easily converted to that
format.

So our current plan is to replace the `SaveFrame()` function from Tutorial 1,
and instead output our frame to the screen. But first we have to start by
seeing how to use the SDL Library. First we have to include the libraries and
initalize SDL:

    
    
    #include <SDL.h>
    #include <SDL_thread.h>
    
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
      fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
      exit(1);
    }
    

`SDL_Init()` essentially tells the library what features we're going to use.
`SDL_GetError()`, of course, is a handy debugging function.

### Creating a Display

Now we need a place on the screen to put stuff. The basic area for displaying
images with SDL is called a **surface**:

    
    
    SDL_Surface *screen;
    
    screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
    if(!screen) {
      fprintf(stderr, "SDL: could not set video mode - exiting\n");
      exit(1);
    }
    

This sets up a screen with the given width and height. The next option is the
bit depth of the screen - 0 is a special value that means "same as the current
display". (This does not work on OS X; see source.)

Now we create a YUV overlay on that screen so we can input video to it, and
set up our SWSContext to convert the image data to YUV420:

    
    
    SDL_Overlay     *bmp = NULL;
    struct SWSContext *sws_ctx = NULL;
    
    bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height,
                               SDL_YV12_OVERLAY, screen);
    
    // initialize SWS context for software scaling
    sws_ctx = sws_getContext(pCodecCtx->width,
                             pCodecCtx->height,
    			 pCodecCtx->pix_fmt,
    			 pCodecCtx->width,
    			 pCodecCtx->height,
    			 PIX_FMT_YUV420P,
    			 SWS_BILINEAR,
    			 NULL,
    			 NULL,
    			 NULL
    			 );
    
    

As we said before, we are using YV12 to display the image, and getting YUV420
data from ffmpeg.

### Displaying the Image

Well that was simple enough! Now we just need to display the image. Let's go
all the way down to where we had our finished frame. We can get rid of all
that stuff we had for the RGB frame, and we're going to replace the
`SaveFrame()` with our display code. To display the image, we're going to make
an AVPicture struct and set its data pointers and linesize to our YUV overlay:

    
    
      if(frameFinished) {
        SDL_LockYUVOverlay(bmp);
    
        AVPicture pict;
        pict.data[0] = bmp->pixels[0];
        pict.data[1] = bmp->pixels[2];
        pict.data[2] = bmp->pixels[1];
    
        pict.linesize[0] = bmp->pitches[0];
        pict.linesize[1] = bmp->pitches[2];
        pict.linesize[2] = bmp->pitches[1];
    
        // Convert the image into YUV format that SDL uses
        sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
    	      pFrame->linesize, 0, pCodecCtx->height,
    	      pict.data, pict.linesize);
        
        SDL_UnlockYUVOverlay(bmp);
      }    
    

First, we lock the overlay because we are going to be writing to it. This is a
good habit to get into so you don't have problems later. The AVPicture struct,
as shown before, has a `data` pointer that is an array of 4 pointers. Since we
are dealing with YUV420P here, we only have 3 channels, and therefore only 3
sets of data. Other formats might have a fourth pointer for an alpha channel
or something. `linesize` is what it sounds like. The analogous structures in
our YUV overlay are the `pixels` and `pitches` variables. ("pitches" is the
term SDL uses to refer to the width of a given line of data.) So what we do is
point the three arrays of `pict.data` at our overlay, so when we write to
pict, we're actually writing into our overlay, which of course already has the
necessary space allocated. Similarly, we get the linesize information directly
from our overlay. We change the conversion format to `PIX_FMT_YUV420P`, and we
use `sws_scale` just like before.

### Drawing the Image

But we still need to tell SDL to actually show the data we've given it. We
also pass this function a rectangle that says where the movie should go and
what width and height it should be scaled to. This way, SDL does the scaling
for us, and it can be assisted by your graphics processor for faster scaling:

    
    
    SDL_Rect rect;
    
      if(frameFinished) {
        /* ... code ... */
        // Convert the image into YUV format that SDL uses
        sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
                  pFrame->linesize, 0, pCodecCtx->height,
    	      pict.data, pict.linesize);
        
        SDL_UnlockYUVOverlay(bmp);
    	rect.x = 0;
    	rect.y = 0;
    	rect.w = pCodecCtx->width;
    	rect.h = pCodecCtx->height;
    	SDL_DisplayYUVOverlay(bmp, &rect;);
      }
    

Now our video is displayed!

Let's take this time to show you another feature of SDL: its event system. SDL
is set up so that when you type, or move the mouse in the SDL application, or
send it a signal, it generates an **event**. Your program then checks for
these events if it wants to handle user input. Your program can also make up
events to send the SDL event system. This is especially useful when
multithread programming with SDL, which we'll see in Tutorial 4. In our
program, we're going to poll for events right after we finish processing a
packet. For now, we're just going to handle the `SDL_QUIT` event so we can
exit:

    
    
    SDL_Event       event;
    
        av_free_packet(&packet;);
        SDL_PollEvent(&event;);
        switch(event.type) {
        case SDL_QUIT:
          SDL_Quit();
          exit(0);
          break;
        default:
          break;
        }
    

And there we go! Get rid of all the old cruft, and you're ready to compile. If
you are using Linux or a variant, the best way to compile using the SDL libs
is this:

    
    
    gcc -o tutorial02 tutorial02.c -lavformat -lavcodec -lswscale -lz -lm \
    `sdl-config --cflags --libs`
    

sdl-config just prints out the proper flags for gcc to include the SDL
libraries properly. You may need to do something different to get it to
compile on your system; please check the SDL documentation for your system.
Once it compiles, go ahead and run it.

What happens when you run this program? The video is going crazy! In fact,
we're just displaying all the video frames as fast as we can extract them from
the movie file. We don't have any code right now for figuring out _when_ we
need to display video. Eventually (in Tutorial 5), we'll get around to syncing
the video. But first we're missing something even more important: sound!

```c

// tutorial02.c
// A pedagogical video player that will stream through every video frame as fast as it can.
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101, SDL 1.2.15
// on GCC 4.7.2 in Debian February 2015
//
// Use
// 
// gcc -o tutorial02 tutorial02.c -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
// to build (assuming libavformat and libavcodec are correctly installed, 
// and assuming you have sdl-config. Please refer to SDL docs for your installation.)
//
// Run using
// tutorial02 myvideofile.mpg
//
// to play the video stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

int main(int argc, char *argv[]) {
  AVFormatContext *pFormatCtx = NULL;
  int             i, videoStream;
  AVCodecContext  *pCodecCtxOrig = NULL;
  AVCodecContext  *pCodecCtx = NULL;
  AVCodec         *pCodec = NULL;
  AVFrame         *pFrame = NULL;
  AVPacket        packet;
  int             frameFinished;
  float           aspect_ratio;
  struct SwsContext *sws_ctx = NULL;

  SDL_Overlay     *bmp;
  SDL_Surface     *screen;
  SDL_Rect        rect;
  SDL_Event       event;

  if(argc < 2) {
    fprintf(stderr, "Usage: test <file>\n");
    exit(1);
  }
  // Register all formats and codecs
  av_register_all();
  
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Open video file
  if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0)
    return -1; // Couldn't open file
  
  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; // Couldn't find stream information
  
  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);
  
  // Find the first video stream
  videoStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++)
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
      videoStream=i;
      break;
    }
  if(videoStream==-1)
    return -1; // Didn't find a video stream
  
  // Get a pointer to the codec context for the video stream
  pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;
  // Find the decoder for the video stream
  pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
  if(pCodec==NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; // Codec not found
  }

  // Copy context
  pCodecCtx = avcodec_alloc_context3(pCodec);
  if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }

  // Open codec
  if(avcodec_open2(pCodecCtx, pCodec, NULL)<0)
    return -1; // Could not open codec
  
  // Allocate video frame
  pFrame=av_frame_alloc();

  // Make a screen to put our video
#ifndef __DARWIN__
        screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
#else
        screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);
#endif
  if(!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }
  
  // Allocate a place to put our YUV image on that screen
  bmp = SDL_CreateYUVOverlay(pCodecCtx->width,
				 pCodecCtx->height,
				 SDL_YV12_OVERLAY,
				 screen);

  // initialize SWS context for software scaling
  sws_ctx = sws_getContext(pCodecCtx->width,
			   pCodecCtx->height,
			   pCodecCtx->pix_fmt,
			   pCodecCtx->width,
			   pCodecCtx->height,
			   PIX_FMT_YUV420P,
			   SWS_BILINEAR,
			   NULL,
			   NULL,
			   NULL
			   );



  // Read frames and save first five frames to disk
  i=0;
  while(av_read_frame(pFormatCtx, &packet)>=0) {
    // Is this a packet from the video stream?
    if(packet.stream_index==videoStream) {
      // Decode video frame
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
      
      // Did we get a video frame?
      if(frameFinished) {
	SDL_LockYUVOverlay(bmp);

	AVPicture pict;
	pict.data[0] = bmp->pixels[0];
	pict.data[1] = bmp->pixels[2];
	pict.data[2] = bmp->pixels[1];

	pict.linesize[0] = bmp->pitches[0];
	pict.linesize[1] = bmp->pitches[2];
	pict.linesize[2] = bmp->pitches[1];

	// Convert the image into YUV format that SDL uses
	sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
		  pFrame->linesize, 0, pCodecCtx->height,
		  pict.data, pict.linesize);

	SDL_UnlockYUVOverlay(bmp);
	
	rect.x = 0;
	rect.y = 0;
	rect.w = pCodecCtx->width;
	rect.h = pCodecCtx->height;
	SDL_DisplayYUVOverlay(bmp, &rect);
      
      }
    }
    
    // Free the packet that was allocated by av_read_frame
    av_free_packet(&packet);
    SDL_PollEvent(&event);
    switch(event.type) {
    case SDL_QUIT:
      SDL_Quit();
      exit(0);
      break;
    default:
      break;
    }

  }
  
  // Free the YUV frame
  av_frame_free(&pFrame);
  
  // Close the codec
  avcodec_close(pCodecCtx);
  avcodec_close(pCodecCtxOrig);
  
  // Close the video file
  avformat_close_input(&pFormatCtx);
  
  return 0;
}

```

## Tutorial 03: Playing Sound

Code: tutorial03.c

### Audio

So now we want to play sound. SDL also gives us methods for outputting sound.
The `SDL_OpenAudio()` function is used to open the audio device itself. It
takes as arguments an `SDL_AudioSpec` struct, which contains all the
information about the audio we are going to output.

Before we show how you set this up, let's explain first about how audio is
handled by computers. Digital audio consists of a long stream of **samples**.
Each sample represents a value of the audio waveform. Sounds are recorded at a
certain **sample rate**, which simply says how fast to play each sample, and
is measured in number of samples per second. Example sample rates are 22,050
and 44,100 samples per second, which are the rates used for radio and CD
respectively. In addition, most audio can have more than one channel for
stereo or surround, so for example, if the sample is in stereo, the samples
will come 2 at a time. When we get data from a movie file, we don't know how
many samples we will get, but ffmpeg will not give us partial samples - that
also means that it will not split a stereo sample up, either.

SDL's method for playing audio is this: you set up your audio options: the
sample rate (called "freq" for **frequency** in the SDL struct), number of
channels, and so forth, and we also set a callback function and userdata. When
we begin playing audio, SDL will continually call this callback function and
ask it to fill the audio buffer with a certain number of bytes. After we put
this information in the `SDL_AudioSpec` struct, we call `SDL_OpenAudio()`,
which will open the audio device and give us back _another_ AudioSpec struct.
These are the specs we will _actually_ be using -- we are not guaranteed to
get what we asked for!

### Setting Up the Audio

Keep that all in your head for the moment, because we don't actually have any
information yet about the audio streams yet! Let's go back to the place in our
code where we found the video stream and find which stream is the audio
stream.

    
    
    // Find the first video stream
    videoStream=-1;
    audioStream=-1;
    for(i=0; i < pFormatCtx->nb_streams; i++) {
      if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO
         &&
           videoStream < 0) {
        videoStream=i;
      }
      if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
         audioStream < 0) {
        audioStream=i;
      }
    }
    if(videoStream==-1)
      return -1; // Didn't find a video stream
    if(audioStream==-1)
      return -1;
    

From here we can get all the info we want from the `AVCodecContext` from the
stream, just like we did with the video stream:

    
    
    AVCodecContext *aCodecCtxOrig;
    AVCodecContext *aCodecCtx;
    
    aCodecCtxOrig=pFormatCtx->streams[audioStream]->codec;
    

If you remember from the previous tutorials, we still need to open the audio
codec itself. This is straightforward:

    
    
    AVCodec         *aCodec;
    
    aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
    if(!aCodec) {
      fprintf(stderr, "Unsupported codec!\n");
      return -1;
    }
    // Copy context
    aCodecCtx = avcodec_alloc_context3(aCodec);
    if(avcodec_copy_context(aCodecCtx, aCodecCtxOrig) != 0) {
      fprintf(stderr, "Couldn't copy codec context");
      return -1; // Error copying codec context
    }
    /* set up SDL Audio here */
    
    avcodec_open2(aCodecCtx, aCodec, NULL);
    

Contained within the codec context is all the information we need to set up
our audio:

    
    
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;
    
    if(SDL_OpenAudio(&wanted;_spec, &spec;) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }
    

Let's go through these:

  * `freq`: The sample rate, as explained earlier.
  * `format`: This tells SDL what format we will be giving it. The "S" in "S16SYS" stands for "signed", the 16 says that each sample is 16 bits long, and "SYS" means that the endian-order will depend on the system you are on. This is the format that `avcodec_decode_audio2` will give us the audio in.
  * `channels`: Number of audio channels.
  * `silence`: This is the value that indicated silence. Since the audio is signed, 0 is of course the usual value.
  * `samples`: This is the size of the audio buffer that we would like SDL to give us when it asks for more audio. A good value here is between 512 and 8192; ffplay uses 1024.
  * `callback`: Here's where we pass the actual callback function. We'll talk more about the callback function later.
  * `userdata`: SDL will give our callback a void pointer to any user data that we want our callback function to have. We want to let it know about our codec context; you'll see why.
Finally, we open the audio with `SDL_OpenAudio`.

### Queues

There! Now we're ready to start pulling audio information from the stream. But
what do we do with that information? We are going to be continuously getting
packets from the movie file, but at the same time SDL is going to call the
callback function! The solution is going to be to create some kind of global
structure that we can stuff audio packets in so our `audio_callback` has
something to get audio data from! So what we're going to do is to create a
**queue** of packets. ffmpeg even comes with a structure to help us with this:
`AVPacketList`, which is just a linked list for packets. Here's our queue
structure:

    
    
    typedef struct PacketQueue {
      AVPacketList *first_pkt, *last_pkt;
      int nb_packets;
      int size;
      SDL_mutex *mutex;
      SDL_cond *cond;
    } PacketQueue;
    

First, we should point out that `nb_packets` is not the same as `size` --
`size` refers to a byte size that we get from `packet->size`. You'll notice
that we have a mutex and a condtion variable in there. This is because SDL is
running the audio process as a separate thread. If we don't lock the queue
properly, we could really mess up our data. We'll see how in the
implementation of the queue. Every programmer should know how to make a queue,
but we're including this so you can learn the SDL functions.

First we make a function to initialize the queue:

    
    
    void packet_queue_init(PacketQueue *q) {
      memset(q, 0, sizeof(PacketQueue));
      q->mutex = SDL_CreateMutex();
      q->cond = SDL_CreateCond();
    }
    

Then we will make a function to put stuff in our queue:

    
    
    int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    
      AVPacketList *pkt1;
      if(av_dup_packet(pkt) < 0) {
        return -1;
      }
      pkt1 = av_malloc(sizeof(AVPacketList));
      if (!pkt1)
        return -1;
      pkt1->pkt = *pkt;
      pkt1->next = NULL;
      
      
      SDL_LockMutex(q->mutex);
      
      if (!q->last_pkt)
        q->first_pkt = pkt1;
      else
        q->last_pkt->next = pkt1;
      q->last_pkt = pkt1;
      q->nb_packets++;
      q->size += pkt1->pkt.size;
      SDL_CondSignal(q->cond);
      
      SDL_UnlockMutex(q->mutex);
      return 0;
    }
    

`SDL_LockMutex()` locks the mutex in the queue so we can add something to it,
and then `SDL_CondSignal()` sends a signal to our get function (if it is
waiting) through our condition variable to tell it that there is data and it
can proceed, then unlocks the mutex to let it go.

Here's the corresponding get function. Notice how `SDL_CondWait()` makes the
function **block** (i.e. pause until we get data) if we tell it to.

    
    
    int quit = 0;
    
    static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
      AVPacketList *pkt1;
      int ret;
      
      SDL_LockMutex(q->mutex);
      
      for(;;) {
        
        if(quit) {
          ret = -1;
          break;
        }
    
        pkt1 = q->first_pkt;
        if (pkt1) {
          q->first_pkt = pkt1->next;
          if (!q->first_pkt)
    	q->last_pkt = NULL;
          q->nb_packets--;
          q->size -= pkt1->pkt.size;
          *pkt = pkt1->pkt;
          av_free(pkt1);
          ret = 1;
          break;
        } else if (!block) {
          ret = 0;
          break;
        } else {
          SDL_CondWait(q->cond, q->mutex);
        }
      }
      SDL_UnlockMutex(q->mutex);
      return ret;
    }
    

As you can see, we've wrapped the function in a forever loop so we will be
sure to get some data if we want to block. We avoid looping forever by making
use of SDL's SDL_CondWait()` function. Basically, all CondWait does is wait
for a signal from `SDL_CondSignal()` (or `SDL_CondBroadcast()`) and then
continue. However, it looks as though we've trapped it within our mutex -- if
we hold the lock, our put function can't put anything in the queue! However,
what `SDL_CondWait()` also does for us is to unlock the mutex we give it and
then attempt to lock it again once we get the signal.

### In Case of Fire

You'll also notice that we have a global `quit` variable that we check to make
sure that we haven't set the program a quit signal (SDL automatically handles
TERM signals and the like). Otherwise, the thread will continue forever and
we'll have to `kill -9` the program.

    
    
      SDL_PollEvent(&event;);
      switch(event.type) {
      case SDL_QUIT:
        quit = 1;
    

We make sure to set the `quit` flag to 1.

### Feeding Packets

The only thing left is to set up our queue:

    
    
    PacketQueue audioq;
    main() {
    ...
      avcodec_open2(aCodecCtx, aCodec, NULL);
    
      packet_queue_init(&audioq;);
      SDL_PauseAudio(0);
    

`SDL_PauseAudio()` finally starts the audio device. It plays silence if it
doesn't get data; which it won't right away.

So, we've got our queue set up, now we're ready to start feeding it packets.
We go to our packet-reading loop:

    
    
    while(av_read_frame(pFormatCtx, &packet;)>=0) {
      // Is this a packet from the video stream?
      if(packet.stream_index==videoStream) {
        // Decode video frame
        ....
        }
      } else if(packet.stream_index==audioStream) {
        packet_queue_put(&audioq;, &packet;);
      } else {
        av_free_packet(&packet;);
      }
    

Note that we don't free the packet after we put it in the queue. We'll free it
later when we decode it.

### Fetching Packets

Now let's finally make our `audio_callback` function to fetch the packets on
the queue. The callback has to be of the form `void callback(void *userdata,
Uint8 *stream, int len)`, where `userdata` of course is the pointer we gave to
SDL, `stream` is the buffer we will be writing audio data to, and `len` is the
size of that buffer. Here's the code:

    
    
    void audio_callback(void *userdata, Uint8 *stream, int len) {
    
      AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
      int len1, audio_size;
    
      static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
      static unsigned int audio_buf_size = 0;
      static unsigned int audio_buf_index = 0;
    
      while(len > 0) {
        if(audio_buf_index >= audio_buf_size) {
          /* We have already sent all our data; get more */
          audio_size = audio_decode_frame(aCodecCtx, audio_buf,
                                          sizeof(audio_buf));
          if(audio_size < 0) {
    	/* If error, output silence */
    	audio_buf_size = 1024;
    	memset(audio_buf, 0, audio_buf_size);
          } else {
    	audio_buf_size = audio_size;
          }
          audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if(len1 > len)
          len1 = len;
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
      }
    }
    

This is basically a simple loop that will pull in data from another function
we will write, `audio_decode_frame()`, store the result in an intermediary
buffer, attempt to write `len` bytes to `stream`, and get more data if we
don't have enough yet, or save it for later if we have some left over. The
size of `audio_buf` is 1.5 times the size of the largest audio frame that
ffmpeg will give us, which gives us a nice cushion.

### Finally Decoding the Audio

Let's get to the real meat of the decoder, `audio_decode_frame`:

    
    
    int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf,
                           int buf_size) {
    
      static AVPacket pkt;
      static uint8_t *audio_pkt_data = NULL;
      static int audio_pkt_size = 0;
      static AVFrame frame;
    
      int len1, data_size = 0;
    
      for(;;) {
        while(audio_pkt_size > 0) {
          int got_frame = 0;
          len1 = avcodec_decode_audio4(aCodecCtx, &frame;, &got;_frame, &pkt;);
          if(len1 < 0) {
    	/* if error, skip frame */
    	audio_pkt_size = 0;
    	break;
          }
          audio_pkt_data += len1;
          audio_pkt_size -= len1;
          data_size = 0;
          if(got_frame) {
    	data_size = av_samples_get_buffer_size(NULL, 
    					       aCodecCtx->channels,
    					       frame.nb_samples,
    					       aCodecCtx->sample_fmt,
    					       1);
    	assert(data_size <= buf_size);
    	memcpy(audio_buf, frame.data[0], data_size);
          }
          if(data_size <= 0) {
    	/* No data yet, get more frames */
    	continue;
          }
          /* We have data, return it and come back for more later */
          return data_size;
        }
        if(pkt.data)
          av_free_packet(&pkt;);
    
        if(quit) {
          return -1;
        }
    
        if(packet_queue_get(&audioq;, &pkt;, 1) < 0) {
          return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
      }
    }
    

This whole process actually starts towards the end of the function, where we
call `packet_queue_get()`. We pick the packet up off the queue, and save its
information. Then, once we have a packet to work with, we call
`avcodec_decode_audio4()`, which acts a lot like its sister function,
`avcodec_decode_video()`, except in this case, a packet might have more than
one frame, so you may have to call it several times to get all the data out of
the packet. Once we have the frame, we simply copy it to our audio buffer,
making sure the data_size is smaller than our audio buffer. Also, remember the
cast to audio_buf, because SDL gives an 8 bit int buffer, and ffmpeg gives us
data in a 16 bit int buffer. You should also notice the difference between
`len1` and `data_size`. `len1` is how much of the packet we've used, and
`data_size` is the amount of raw data returned.

When we've got some data, we immediately return to see if we still need to get
more data from the queue, or if we are done. If we still had more of the
packet to process, we save it for later. If we finish up a packet, we finally
get to free that packet.

So that's it! We've got audio being carried from the main read loop to the
queue, which is then read by the `audio_callback` function, which hands that
data to SDL, which SDL beams to your sound card. Go ahead and compile:

    
    
    gcc -o tutorial03 tutorial03.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
    `sdl-config --cflags --libs`
    

Hooray! The video is still going as fast as possible, but the audio is playing
in time. Why is this? That's because the audio information has a sample rate
-- we're pumping out audio information as fast as we can, but the audio simply
plays from that stream at its leisure according to the sample rate.

We're almost ready to start syncing video and audio ourselves, but first we
need to do a little program reorganization. The method of queueing up audio
and playing it using a separate thread worked very well: it made the code more
managable and more modular. Before we start syncing the video to the audio, we
need to make our code easier to deal with. Next time: Spawning Threads!

```c

// tutorial03.c
// A pedagogical video player that will stream through every video frame as fast as it can
// and play audio (out of sync).
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101, SDL 1.2.15
// on GCC 4.7.2 in Debian February 2015
//
// Use
//
// gcc -o tutorial03 tutorial03.c -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
// to build (assuming libavformat and libavcodec are correctly installed, 
// and assuming you have sdl-config. Please refer to SDL docs for your installation.)
//
// Run using
// tutorial03 myvideofile.mpg
//
// to play the stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <assert.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;

int quit = 0;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

  AVPacketList *pkt1;
  if(av_dup_packet(pkt) < 0) {
    return -1;
  }
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;
  
  
  SDL_LockMutex(q->mutex);
  
  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  SDL_CondSignal(q->cond);
  
  SDL_UnlockMutex(q->mutex);
  return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  AVPacketList *pkt1;
  int ret;
  
  SDL_LockMutex(q->mutex);
  
  for(;;) {
    
    if(quit) {
      ret = -1;
      break;
    }

    pkt1 = q->first_pkt;
    if (pkt1) {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt)
	q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) {

  static AVPacket pkt;
  static uint8_t *audio_pkt_data = NULL;
  static int audio_pkt_size = 0;
  static AVFrame frame;

  int len1, data_size = 0;

  for(;;) {
    while(audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
      if(len1 < 0) {
	/* if error, skip frame */
	audio_pkt_size = 0;
	break;
      }
      audio_pkt_data += len1;
      audio_pkt_size -= len1;
      data_size = 0;
      if(got_frame) {
	data_size = av_samples_get_buffer_size(NULL, 
					       aCodecCtx->channels,
					       frame.nb_samples,
					       aCodecCtx->sample_fmt,
					       1);
	assert(data_size <= buf_size);
	memcpy(audio_buf, frame.data[0], data_size);
      }
      if(data_size <= 0) {
	/* No data yet, get more frames */
	continue;
      }
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if(pkt.data)
      av_free_packet(&pkt);

    if(quit) {
      return -1;
    }

    if(packet_queue_get(&audioq, &pkt, 1) < 0) {
      return -1;
    }
    audio_pkt_data = pkt.data;
    audio_pkt_size = pkt.size;
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

  AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
  int len1, audio_size;

  static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  static unsigned int audio_buf_size = 0;
  static unsigned int audio_buf_index = 0;

  while(len > 0) {
    if(audio_buf_index >= audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
      if(audio_size < 0) {
	/* If error, output silence */
	audio_buf_size = 1024; // arbitrary?
	memset(audio_buf, 0, audio_buf_size);
      } else {
	audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
    }
    len1 = audio_buf_size - audio_buf_index;
    if(len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
    len -= len1;
    stream += len1;
    audio_buf_index += len1;
  }
}

int main(int argc, char *argv[]) {
  AVFormatContext *pFormatCtx = NULL;
  int             i, videoStream, audioStream;
  AVCodecContext  *pCodecCtxOrig = NULL;
  AVCodecContext  *pCodecCtx = NULL;
  AVCodec         *pCodec = NULL;
  AVFrame         *pFrame = NULL;
  AVPacket        packet;
  int             frameFinished;
  struct SwsContext *sws_ctx = NULL;
  
  AVCodecContext  *aCodecCtxOrig = NULL;
  AVCodecContext  *aCodecCtx = NULL;
  AVCodec         *aCodec = NULL;

  SDL_Overlay     *bmp;
  SDL_Surface     *screen;
  SDL_Rect        rect;
  SDL_Event       event;
  SDL_AudioSpec   wanted_spec, spec;

  if(argc < 2) {
    fprintf(stderr, "Usage: test <file>\n");
    exit(1);
  }
  // Register all formats and codecs
  av_register_all();
  
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Open video file
  if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0)
    return -1; // Couldn't open file
  
  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; // Couldn't find stream information
  
  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);
    
  // Find the first video stream
  videoStream=-1;
  audioStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++) {
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO &&
       videoStream < 0) {
      videoStream=i;
    }
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
       audioStream < 0) {
      audioStream=i;
    }
  }
  if(videoStream==-1)
    return -1; // Didn't find a video stream
  if(audioStream==-1)
    return -1;
   
  aCodecCtxOrig=pFormatCtx->streams[audioStream]->codec;
  aCodec = avcodec_find_decoder(aCodecCtxOrig->codec_id);
  if(!aCodec) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  // Copy context
  aCodecCtx = avcodec_alloc_context3(aCodec);
  if(avcodec_copy_context(aCodecCtx, aCodecCtxOrig) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }

  // Set audio settings from codec info
  wanted_spec.freq = aCodecCtx->sample_rate;
  wanted_spec.format = AUDIO_S16SYS;
  wanted_spec.channels = aCodecCtx->channels;
  wanted_spec.silence = 0;
  wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
  wanted_spec.callback = audio_callback;
  wanted_spec.userdata = aCodecCtx;
  
  if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
    fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
    return -1;
  }

  avcodec_open2(aCodecCtx, aCodec, NULL);

  // audio_st = pFormatCtx->streams[index]
  packet_queue_init(&audioq);
  SDL_PauseAudio(0);

  // Get a pointer to the codec context for the video stream
  pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;
  
  // Find the decoder for the video stream
  pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
  if(pCodec==NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; // Codec not found
  }

  // Copy context
  pCodecCtx = avcodec_alloc_context3(pCodec);
  if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }

  // Open codec
  if(avcodec_open2(pCodecCtx, pCodec, NULL)<0)
    return -1; // Could not open codec
  
  // Allocate video frame
  pFrame=av_frame_alloc();

  // Make a screen to put our video

#ifndef __DARWIN__
        screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
#else
        screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);
#endif
  if(!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }
  
  // Allocate a place to put our YUV image on that screen
  bmp = SDL_CreateYUVOverlay(pCodecCtx->width,
				 pCodecCtx->height,
				 SDL_YV12_OVERLAY,
				 screen);

  // initialize SWS context for software scaling
  sws_ctx = sws_getContext(pCodecCtx->width,
			   pCodecCtx->height,
			   pCodecCtx->pix_fmt,
			   pCodecCtx->width,
			   pCodecCtx->height,
			   PIX_FMT_YUV420P,
			   SWS_BILINEAR,
			   NULL,
			   NULL,
			   NULL
			   );

  // Read frames and save first five frames to disk
  i=0;
  while(av_read_frame(pFormatCtx, &packet)>=0) {
    // Is this a packet from the video stream?
    if(packet.stream_index==videoStream) {
      // Decode video frame
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
      
      // Did we get a video frame?
      if(frameFinished) {
	SDL_LockYUVOverlay(bmp);

	AVPicture pict;
	pict.data[0] = bmp->pixels[0];
	pict.data[1] = bmp->pixels[2];
	pict.data[2] = bmp->pixels[1];

	pict.linesize[0] = bmp->pitches[0];
	pict.linesize[1] = bmp->pitches[2];
	pict.linesize[2] = bmp->pitches[1];

	// Convert the image into YUV format that SDL uses	
	sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
		  pFrame->linesize, 0, pCodecCtx->height,
		  pict.data, pict.linesize);
	
	SDL_UnlockYUVOverlay(bmp);
	
	rect.x = 0;
	rect.y = 0;
	rect.w = pCodecCtx->width;
	rect.h = pCodecCtx->height;
	SDL_DisplayYUVOverlay(bmp, &rect);
	av_free_packet(&packet);
      }
    } else if(packet.stream_index==audioStream) {
      packet_queue_put(&audioq, &packet);
    } else {
      av_free_packet(&packet);
    }
    // Free the packet that was allocated by av_read_frame
    SDL_PollEvent(&event);
    switch(event.type) {
    case SDL_QUIT:
      quit = 1;
      SDL_Quit();
      exit(0);
      break;
    default:
      break;
    }

  }

  // Free the YUV frame
  av_frame_free(&pFrame);
  
  // Close the codecs
  avcodec_close(pCodecCtxOrig);
  avcodec_close(pCodecCtx);
  avcodec_close(aCodecCtxOrig);
  avcodec_close(aCodecCtx);
  
  // Close the video file
  avformat_close_input(&pFormatCtx);
  
  return 0;
}

```

## Tutorial 04: Spawning Threads

Code: tutorial04.c

### Overview

Last time we added audio support by taking advantage of SDL's audio functions.
SDL started a thread that made callbacks to a function we defined every time
it needed audio. Now we're going to do the same sort of thing with the video
display. This makes the code more modular and easier to work with - especially
when we want to add syncing. So where do we start?

First we notice that our main function is handling an awful lot: it's running
through the event loop, reading in packets, and decoding the video. So what
we're going to do is split all those apart: we're going to have a thread that
will be responsible for decoding the packets; these packets will then be added
to the queue and read by the corresponding audio and video threads. The audio
thread we have already set up the way we want it; the video thread will be a
little more complicated since we have to display the video ourselves. We will
add the actual display code to the main loop. But instead of just displaying
video every time we loop, we will integrate the video display into the event
loop. The idea is to decode the video, save the resulting frame in _another_
queue, then create a custom event (`FF_REFRESH_EVENT`) that we add to the
event system, then when our event loop sees this event, it will display the
next frame in the queue. Here's a handy ASCII art illustration of what is
going on:

    
    
     ________ audio  _______      _____
    |        | pkts |       |    |     | to spkr
    | DECODE |----->| AUDIO |--->| SDL |-->
    |________|      |_______|    |_____|
        |  video     _______
        |   pkts    |       |
        +---------->| VIDEO |
     ________       |_______|   _______
    |       |          |       |       |
    | EVENT |          +------>| VIDEO | to mon.
    | LOOP  |----------------->| DISP. |-->
    |_______|<---FF_REFRESH----|_______|
    

The main purpose of moving controlling the video display via the event loop is
that using an SDL_Delay thread, we can control exactly when the next video
frame shows up on the screen. When we finally sync the video in the next
tutorial, it will be a simple matter to add the code that will schedule the
next video refresh so the right picture is being shown on the screen at the
right time.

### Simplifying Code

We're also going to clean up the code a bit. We have all this audio and video
codec information, and we're going to be adding queues and buffers and who
knows what else. All this stuff is for one logical unit, _viz._ the movie. So
we're going to make a large struct that will hold all that information called
the `VideoState`.

    
    
    typedef struct VideoState {
    
      AVFormatContext *pFormatCtx;
      int             videoStream, audioStream;
      AVStream        *audio_st;
      AVCodecContext  *audio_ctx;
      PacketQueue     audioq;
      uint8_t         audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
      unsigned int    audio_buf_size;
      unsigned int    audio_buf_index;
      AVPacket        audio_pkt;
      uint8_t         *audio_pkt_data;
      int             audio_pkt_size;
      AVStream        *video_st;
      AVCodecContext  *video_ctx;
      PacketQueue     videoq;
    
      VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
      int             pictq_size, pictq_rindex, pictq_windex;
      SDL_mutex       *pictq_mutex;
      SDL_cond        *pictq_cond;
      
      SDL_Thread      *parse_tid;
      SDL_Thread      *video_tid;
    
      char            filename[1024];
      int             quit;
    } VideoState;
    

Here we see a glimpse of what we're going to get to. First we see the basic
information - the format context and the indices of the audio and video
stream, and the corresponding AVStream objects. Then we can see that we've
moved some of those audio buffers into this structure. These (audio_buf,
audio_buf_size, etc.) were all for information about audio that was still
lying around (or the lack thereof). We've added another queue for the video,
and a buffer (which will be used as a queue; we don't need any fancy queueing
stuff for this) for the decoded frames (saved as an overlay). The VideoPicture
struct is of our own creations (we'll see what's in it when we come to it). We
also notice that we've allocated pointers for the two extra threads we will
create, and the quit flag and the filename of the movie.

So now we take it all the way back to the main function to see how this
changes our program. Let's set up our `VideoState` struct:

    
    
    int main(int argc, char *argv[]) {
    
      SDL_Event       event;
    
      VideoState      *is;
    
      is = av_mallocz(sizeof(VideoState));
    

`av_mallocz()` is a nice function that will allocate memory for us and zero it
out.

Then we'll initialize our locks for the display buffer (`pictq`), because
since the event loop calls our display function - the display function,
remember, will be pulling pre-decoded frames from `pictq`. At the same time,
our video decoder will be putting information into it - we don't know who will
get there first. Hopefully you recognize that this is a classic **race
condition**. So we allocate it now before we start any threads. Let's also
copy the filename of our movie into our `VideoState`.

    
    
    av_strlcpy(is->filename, argv[1], sizeof(is->filename));
    
    is->pictq_mutex = SDL_CreateMutex();
    is->pictq_cond = SDL_CreateCond();
    

`av_strlcpy` is a function from ffmpeg that does some extra bounds checking
beyond strncpy.

### Our First Thread

Now let's finally launch our threads and get the real work done:

    
    
    schedule_refresh(is, 40);
    
    is->parse_tid = SDL_CreateThread(decode_thread, is);
    if(!is->parse_tid) {
      av_free(is);
      return -1;
    }
    

`schedule_refresh` is a function we will define later. What it basically does
is tell the system to push a `FF_REFRESH_EVENT` after the specified number of
milliseconds. This will in turn call the video refresh function when we see it
in the event queue. But for now, let's look at `SDL_CreateThread()`.

`SDL_CreateThread()` does just that - it spawns a new thread that has complete
access to all the memory of the original process, and starts the thread
running on the function we give it. It will also pass that function user-
defined data. In this case, we're calling `decode_thread()` and with our
`VideoState` struct attached. The first half of the function has nothing new;
it simply does the work of opening the file and finding the index of the audio
and video streams. The only thing we do different is save the format context
in our big struct. After we've found our stream indices, we call another
function that we will define, `stream_component_open()`. This is a pretty
natural way to split things up, and since we do a lot of similar things to set
up the video and audio codec, we reuse some code by making this a function.

The `stream_component_open()` function is where we will find our codec
decoder, set up our audio options, save important information to our big
struct, and launch our audio and video threads. This is where we would also
insert other options, such as forcing the codec instead of autodetecting it
and so forth. Here it is:

    
    
    int stream_component_open(VideoState *is, int stream_index) {
    
      AVFormatContext *pFormatCtx = is->pFormatCtx;
      AVCodecContext *codecCtx;
      AVCodec *codec;
      SDL_AudioSpec wanted_spec, spec;
    
      if(stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
        return -1;
      }
    
      codec = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codec->codec_id);
      if(!codec) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
      }
    
      codecCtx = avcodec_alloc_context3(codec);
      if(avcodec_copy_context(codecCtx, pFormatCtx->streams[stream_index]->codec) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        return -1; // Error copying codec context
      }
    
    
      if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
        // Set audio settings from codec info
        wanted_spec.freq = codecCtx->sample_rate;
        /* ...etc... */
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = is;
        
        if(SDL_OpenAudio(&wanted;_spec, &spec;) < 0) {
          fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
          return -1;
        }
      }
      if(avcodec_open2(codecCtx, codec, NULL) < 0) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
      }
    
      switch(codecCtx->codec_type) {
      case AVMEDIA_TYPE_AUDIO:
        is->audioStream = stream_index;
        is->audio_st = pFormatCtx->streams[stream_index];
        is->audio_ctx = codecCtx;
        is->audio_buf_size = 0;
        is->audio_buf_index = 0;
        memset(&is-;>audio_pkt, 0, sizeof(is->audio_pkt));
        packet_queue_init(&is-;>audioq);
        SDL_PauseAudio(0);
        break;
      case AVMEDIA_TYPE_VIDEO:
        is->videoStream = stream_index;
        is->video_st = pFormatCtx->streams[stream_index];
        is->video_ctx = codecCtx;
        
        packet_queue_init(&is-;>videoq);
        is->video_tid = SDL_CreateThread(video_thread, is);
        is->sws_ctx = sws_getContext(is->video_st->codec->width, is->video_st->codec->height,
    				 is->video_st->codec->pix_fmt, is->video_st->codec->width,
    				 is->video_st->codec->height, PIX_FMT_YUV420P,
    				 SWS_BILINEAR, NULL, NULL, NULL
    				 );
        break;
      default:
        break;
      }
    }
    
    

This is pretty much the same as the code we had before, except now it's
generalized for audio and video. Notice that instead of aCodecCtx, we've set
up our big struct as the userdata for our audio callback. We've also saved the
streams themselves as `audio_st` and `video_st`. We also have added our video
queue and set it up in the same way we set up our audio queue. Most of the
point is to launch the video and audio threads. These bits do it:

    
    
        SDL_PauseAudio(0);
        break;
    
    /* ...... */
    
        is->video_tid = SDL_CreateThread(video_thread, is);
    

We remember `SDL_PauseAudio()` from last time, and `SDL_CreateThread()` is
used as in the exact same way as before. We'll get back to our
`video_thread()` function.

Before that, let's go back to the second half of our `decode_thread()`
function. It's basically just a for loop that will read in a packet and put it
on the right queue:

    
    
      for(;;) {
        if(is->quit) {
          break;
        }
        // seek stuff goes here
        if(is->audioq.size > MAX_AUDIOQ_SIZE ||
           is->videoq.size > MAX_VIDEOQ_SIZE) {
          SDL_Delay(10);
          continue;
        }
        if(av_read_frame(is->pFormatCtx, packet) < 0) {
          if((is->pFormatCtx->pb->error) == 0) {
    	SDL_Delay(100); /* no error; wait for user input */
    	continue;
          } else {
    	break;
          }
        }
        // Is this a packet from the video stream?
        if(packet->stream_index == is->videoStream) {
          packet_queue_put(&is-;>videoq, packet);
        } else if(packet->stream_index == is->audioStream) {
          packet_queue_put(&is-;>audioq, packet);
        } else {
          av_free_packet(packet);
        }
      }
    

Nothing really new here, except that we now have a max size for our audio and
video queue, and we've added a check for read errors. The format context has a
`ByteIOContext` struct inside it called `pb`. `ByteIOContext` is the structure
that basically keeps all the low-level file information in it.

After our for loop, we have all the code for waiting for the rest of the
program to end or informing it that we've ended. This code is instructive
because it shows us how we push events - something we'll have to later to
display the video.

    
    
      while(!is->quit) {
        SDL_Delay(100);
      }
    
     fail:
      if(1){
        SDL_Event event;
        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event;);
      }
      return 0;
    

We get values for user events by using the SDL constant `SDL_USEREVENT`. The
first user event should be assigned the value `SDL_USEREVENT`, the next
`SDL_USEREVENT + 1`, and so on. `FF_QUIT_EVENT` is defined in our program as
`SDL_USEREVENT + 1`. We can also pass user data if we like, too, and here we
pass our pointer to the big struct. Finally we call `SDL_PushEvent()`. In our
event loop switch, we just put this by the `SDL_QUIT_EVENT` section we had
before. We'll see our event loop in more detail; for now, just be assured that
when we push the `FF_QUIT_EVENT`, we'll catch it later and raise our `quit`
flag.

### Getting the Frame: `video_thread`

After we have our codec prepared, we start our video thread. This thread reads
in packets from the video queue, decodes the video into frames, and then calls
a `queue_picture` function to put the processed frame onto a picture queue:

    
    
    int video_thread(void *arg) {
      VideoState *is = (VideoState *)arg;
      AVPacket pkt1, *packet = &pkt1;
      int frameFinished;
      AVFrame *pFrame;
    
      pFrame = av_frame_alloc();
    
      for(;;) {
        if(packet_queue_get(&is-;>videoq, packet, 1) < 0) {
          // means we quit getting packets
          break;
        }
        // Decode video frame
        avcodec_decode_video2(is->video_st->codec, pFrame, &frameFinished, packet);
    
        // Did we get a video frame?
        if(frameFinished) {
          if(queue_picture(is, pFrame) < 0) {
    	break;
          }
        }
        av_free_packet(packet);
      }
      av_free(pFrame);
      return 0;
    }
    

Most of this function should be familiar by this point. We've moved our
`avcodec_decode_video2` function here, just replaced some of the arguments;
for example, we have the AVStream stored in our big struct, so we get our
codec from there. We just keep getting packets from our video queue until
someone tells us to quit or we encounter an error.

### Queueing the Frame

Let's look at the function that stores our decoded frame, `pFrame` in our
picture queue. Since our picture queue is an SDL overlay (presumably to allow
the video display function to have as little calculation as possible), we need
to convert our frame into that. The data we store in the picture queue is a
struct of our making:

    
    
    typedef struct VideoPicture {
      SDL_Overlay *bmp;
      int width, height; /* source height & width */
      int allocated;
    } VideoPicture;
    

Our big struct has a buffer of these in it where we can store them. However,
we need to allocate the `SDL_Overlay` ourselves (notice the `allocated` flag
that will indicate whether we have done so or not).

To use this queue, we have two pointers - the writing index and the reading
index. We also keep track of how many actual pictures are in the buffer. To
write to the queue, we're going to first wait for our buffer to clear out so
we have space to store our `VideoPicture`. Then we check and see if we have
already allocated the overlay at our writing index. If not, we'll have to
allocate some space. We also have to reallocate the buffer if the size of the
window has changed!

    
    
    int queue_picture(VideoState *is, AVFrame *pFrame) {
    
      VideoPicture *vp;
      int dst_pix_fmt;
      AVPicture pict;
    
      /* wait until we have space for a new pic */
      SDL_LockMutex(is->pictq_mutex);
      while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
    	!is->quit) {
        SDL_CondWait(is->pictq_cond, is->pictq_mutex);
      }
      SDL_UnlockMutex(is->pictq_mutex);
    
      if(is->quit)
        return -1;
    
      // windex is set to 0 initially
      vp = &is-;>pictq[is->pictq_windex];
    
      /* allocate or resize the buffer! */
      if(!vp->bmp ||
         vp->width != is->video_st->codec->width ||
         vp->height != is->video_st->codec->height) {
        SDL_Event event;
    
        vp->allocated = 0;
        alloc_picture(is);
        if(is->quit) {
          return -1;
        }
      }
    

Let's look at the `alloc_picture()` function:

    
    
    void alloc_picture(void *userdata) {
    
      VideoState *is = (VideoState *)userdata;
      VideoPicture *vp;
    
      vp = &is-;>pictq[is->pictq_windex];
      if(vp->bmp) {
        // we already have one make another, bigger/smaller
        SDL_FreeYUVOverlay(vp->bmp);
      }
      // Allocate a place to put our YUV image on that screen
      SDL_LockMutex(screen_mutex);
      vp->bmp = SDL_CreateYUVOverlay(is->video_st->codec->width,
    				 is->video_st->codec->height,
    				 SDL_YV12_OVERLAY,
    				 screen);
      SDL_UnlockMutex(screen_mutex);
      vp->width = is->video_st->codec->width;
      vp->height = is->video_st->codec->height;  
      vp->allocated = 1;
    }
    

You should recognize the `SDL_CreateYUVOverlay` function that we've moved from
our main loop to this section. This code should be fairly self-explanitory by
now. However, now we have a mutex lock around it because two threads cannot
write information to the screen at the same time! This will prevent our
`alloc_picture` function from stepping on the toes of the function that will
display the picture. (We've created this lock as a global variable and
initialized it in `main()`; see code.) Remember that we save the width and
height in the `VideoPicture` structure because we need to make sure that our
video size doesn't change for some reason.

Okay, we're all settled and we have our YUV overlay allocated and ready to
receive a picture. Let's go back to `queue_picture` and look at the code to
copy the frame into the overlay. You should recognize that part of it:

    
    
    int queue_picture(VideoState *is, AVFrame *pFrame) {
    
      /* Allocate a frame if we need it... */
      /* ... */
      /* We have a place to put our picture on the queue */
    
      if(vp->bmp) {
    
        SDL_LockYUVOverlay(vp->bmp);
        
        dst_pix_fmt = PIX_FMT_YUV420P;
        /* point pict at the queue */
    
        pict.data[0] = vp->bmp->pixels[0];
        pict.data[1] = vp->bmp->pixels[2];
        pict.data[2] = vp->bmp->pixels[1];
        
        pict.linesize[0] = vp->bmp->pitches[0];
        pict.linesize[1] = vp->bmp->pitches[2];
        pict.linesize[2] = vp->bmp->pitches[1];
        
        // Convert the image into YUV format that SDL uses
        sws_scale(is->sws_ctx, (uint8_t const * const *)pFrame->data,
    	      pFrame->linesize, 0, is->video_st->codec->height,
    	      pict.data, pict.linesize);
        
        SDL_UnlockYUVOverlay(vp->bmp);
        /* now we inform our display thread that we have a pic ready */
        if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
          is->pictq_windex = 0;
        }
        SDL_LockMutex(is->pictq_mutex);
        is->pictq_size++;
        SDL_UnlockMutex(is->pictq_mutex);
      }
      return 0;
    }
    

The majority of this part is simply the code we used earlier to fill the YUV
overlay with our frame. The last bit is simply "adding" our value onto the
queue. The queue works by adding onto it until it is full, and reading from it
as long as there is something on it. Therefore everything depends upon the
`is->pictq_size` value, requiring us to lock it. So what we do here is
increment the write pointer (and rollover if necessary), then lock the queue
and increase its size. Now our reader will know there is more information on
the queue, and if this makes our queue full, our writer will know about it.

### Displaying the Video

That's it for our video thread! Now we've wrapped up all the loose threads
except for one -- remember that we called the `schedule_refresh()` function
way back? Let's see what that actually did:

    
    
    /* schedule a video refresh in 'delay' ms */
    static void schedule_refresh(VideoState *is, int delay) {
      SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
    }
    

`SDL_AddTimer()` is an SDL function that simply makes a callback to the user-
specfied function after a certain number of milliseconds (and optionally
carrying some user data). We're going to use this function to schedule video
updates - every time we call this function, it will set the timer, which will
trigger an event, which will have our `main()` function in turn call a
function that pulls a frame from our picture queue and displays it! Phew!

But first thing's first. Let's trigger that event. That sends us over to:

    
    
    static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
      SDL_Event event;
      event.type = FF_REFRESH_EVENT;
      event.user.data1 = opaque;
      SDL_PushEvent(&event;);
      return 0; /* 0 means stop timer */
    }
    

Here is the now-familiar event push. `FF_REFRESH_EVENT` is defined here as
`SDL_USEREVENT + 1`. One thing to notice is that when we `return 0`, SDL stops
the timer so the callback is not made again.

Now we've pushed an `FF_REFRESH_EVENT`, we need to handle it in our event
loop:

    
    
    for(;;) {
    
      SDL_WaitEvent(&event;);
      switch(event.type) {
      /* ... */
      case FF_REFRESH_EVENT:
        video_refresh_timer(event.user.data1);
        break;
    

and _that_ sends us to this function, which will actually pull the data from
our picture queue:

    
    
    void video_refresh_timer(void *userdata) {
    
      VideoState *is = (VideoState *)userdata;
      VideoPicture *vp;
      
      if(is->video_st) {
        if(is->pictq_size == 0) {
          schedule_refresh(is, 1);
        } else {
          vp = &is-;>pictq[is->pictq_rindex];
          /* Timing code goes here */
    
          schedule_refresh(is, 80);
          
          /* show the picture! */
          video_display(is);
          
          /* update queue for next picture! */
          if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
    	is->pictq_rindex = 0;
          }
          SDL_LockMutex(is->pictq_mutex);
          is->pictq_size--;
          SDL_CondSignal(is->pictq_cond);
          SDL_UnlockMutex(is->pictq_mutex);
        }
      } else {
        schedule_refresh(is, 100);
      }
    }
    

For now, this is a pretty simple function: it pulls from the queue when we
have something, sets our timer for when the next video frame should be shown,
calls video_display to actually show the video on the screen, then increments
the counter on the queue, and decreases its size. You may notice that we don't
actually do anything with `vp` in this function, and here's why: we will.
Later. We're going to use it to access timing information when we start
syncing the video to the audio. See where it says "timing code here"? In that
section, we're going to figure out how soon we should show the next video
frame, and then input that value into the `schedule_refresh()` function. For
now we're just putting in a dummy value of 80. Technically, you could guess
and check this value, and recompile it for every movie you watch, but 1) it
would drift after a while and 2) it's quite silly. We'll come back to it
later, though.

We're almost done; we just have one last thing to do: display the video!
Here's that `video_display` function:

    
    
    
    
    
    void video_display(VideoState *is) {
    
      SDL_Rect rect;
      VideoPicture *vp;
      float aspect_ratio;
      int w, h, x, y;
      int i;
    
      vp = &is-;>pictq[is->pictq_rindex];
      if(vp->bmp) {
        if(is->video_st->codec->sample_aspect_ratio.num == 0) {
          aspect_ratio = 0;
        } else {
          aspect_ratio = av_q2d(is->video_st->codec->sample_aspect_ratio) *
    	is->video_st->codec->width / is->video_st->codec->height;
        }
        if(aspect_ratio <= 0.0) {
          aspect_ratio = (float)is->video_st->codec->width /
    	(float)is->video_st->codec->height;
        }
        h = screen->h;
        w = ((int)rint(h * aspect_ratio)) & -3;
        if(w > screen->w) {
          w = screen->w;
          h = ((int)rint(w / aspect_ratio)) & -3;
        }
        x = (screen->w - w) / 2;
        y = (screen->h - h) / 2;
        
        rect.x = x;
        rect.y = y;
        rect.w = w;
        rect.h = h;
        SDL_LockMutex(screen_mutex);
        SDL_DisplayYUVOverlay(vp->bmp, &rect;);
        SDL_UnlockMutex(screen_mutex);
      }
    }
    

Since our screen can be of any size (we set ours to 640x480 and there are ways
to set it so it is resizable by the user), we need to dynamically figure out
how big we want our movie rectangle to be. So first we need to figure out our
movie's **aspect ratio**, which is just the width divided by the height. Some
codecs will have an odd **sample aspect ratio**, which is simply the
width/height radio of a single pixel, or sample. Since the height and width
values in our codec context are measured in pixels, the actual aspect ratio is
equal to the aspect ratio times the sample aspect ratio. Some codecs will show
an aspect ratio of 0, and this indicates that each pixel is simply of size
1x1. Then we scale the movie to fit as big in our `screen` as we can. The `&
-3` bit-twiddling in there simply rounds the value to the nearest multiple of
4. Then we center the movie, and call `SDL_DisplayYUVOverlay()`, making sure
we use the screen mutex to access it.

So is that it? Are we done? Well, we still have to rewrite the audio code to
use the new `VideoStruct`, but those are trivial changes, and you can look at
those in the sample code. The last thing we have to do is to change our
callback for ffmpeg's internal "quit" callback function:

    
    
    VideoState *global_video_state;
    
    int decode_interrupt_cb(void) {
      return (global_video_state && global_video_state->quit);
    }
    

We set `global_video_state` to the big struct in `main()`.

So that's it! Go ahead and compile it:

    
    
    gcc -o tutorial04 tutorial04.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
    `sdl-config --cflags --libs`
    

and enjoy your unsynced movie! Next time we'll finally build a video player
that actually works!

```c

// tutorial04.c
// A pedagogical video player that will stream through every video frame as fast as it can,
// and play audio (out of sync).
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101, SDL 1.2.15
// on GCC 4.7.2 in Debian February 2015
// Use
//
// gcc -o tutorial04 tutorial04.c -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
// to build (assuming libavformat and libavcodec are correctly installed, 
// and assuming you have sdl-config. Please refer to SDL docs for your installation.)
//
// Run using
// tutorial04 myvideofile.mpg
//
// to play the video stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <assert.h>
#include <math.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;


typedef struct VideoPicture {
  SDL_Overlay *bmp;
  int width, height; /* source height & width */
  int allocated;
} VideoPicture;

typedef struct VideoState {

  AVFormatContext *pFormatCtx;
  int             videoStream, audioStream;
  AVStream        *audio_st;
  AVCodecContext  *audio_ctx;
  PacketQueue     audioq;
  uint8_t         audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
  unsigned int    audio_buf_size;
  unsigned int    audio_buf_index;
  AVFrame         audio_frame;
  AVPacket        audio_pkt;
  uint8_t         *audio_pkt_data;
  int             audio_pkt_size;
  AVStream        *video_st;
  AVCodecContext  *video_ctx;
  PacketQueue     videoq;
  struct SwsContext *sws_ctx;

  VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int             pictq_size, pictq_rindex, pictq_windex;
  SDL_mutex       *pictq_mutex;
  SDL_cond        *pictq_cond;
  
  SDL_Thread      *parse_tid;
  SDL_Thread      *video_tid;

  char            filename[1024];
  int             quit;
} VideoState;

SDL_Surface     *screen;
SDL_mutex       *screen_mutex;

/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

  AVPacketList *pkt1;
  if(av_dup_packet(pkt) < 0) {
    return -1;
  }
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;
  
  SDL_LockMutex(q->mutex);

  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  SDL_CondSignal(q->cond);
  
  SDL_UnlockMutex(q->mutex);
  return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  AVPacketList *pkt1;
  int ret;

  SDL_LockMutex(q->mutex);
  
  for(;;) {
    
    if(global_video_state->quit) {
      ret = -1;
      break;
    }

    pkt1 = q->first_pkt;
    if (pkt1) {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt)
	q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size) {

  int len1, data_size = 0;
  AVPacket *pkt = &is->audio_pkt;

  for(;;) {
    while(is->audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(is->audio_ctx, &is->audio_frame, &got_frame, pkt);
      if(len1 < 0) {
	/* if error, skip frame */
	is->audio_pkt_size = 0;
	break;
      }
      data_size = 0;
      if(got_frame) {
	data_size = av_samples_get_buffer_size(NULL, 
					       is->audio_ctx->channels,
					       is->audio_frame.nb_samples,
					       is->audio_ctx->sample_fmt,
					       1);
	assert(data_size <= buf_size);
	memcpy(audio_buf, is->audio_frame.data[0], data_size);
      }
      is->audio_pkt_data += len1;
      is->audio_pkt_size -= len1;
      if(data_size <= 0) {
	/* No data yet, get more frames */
	continue;
      }
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if(pkt->data)
      av_free_packet(pkt);

    if(is->quit) {
      return -1;
    }
    /* next packet */
    if(packet_queue_get(&is->audioq, pkt, 1) < 0) {
      return -1;
    }
    is->audio_pkt_data = pkt->data;
    is->audio_pkt_size = pkt->size;
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

  VideoState *is = (VideoState *)userdata;
  int len1, audio_size;

  while(len > 0) {
    if(is->audio_buf_index >= is->audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf));
      if(audio_size < 0) {
	/* If error, output silence */
	is->audio_buf_size = 1024;
	memset(is->audio_buf, 0, is->audio_buf_size);
      } else {
	is->audio_buf_size = audio_size;
      }
      is->audio_buf_index = 0;
    }
    len1 = is->audio_buf_size - is->audio_buf_index;
    if(len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
    len -= len1;
    stream += len1;
    is->audio_buf_index += len1;
  }
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay) {
  SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_display(VideoState *is) {

  SDL_Rect rect;
  VideoPicture *vp;
  float aspect_ratio;
  int w, h, x, y;
  int i;

  vp = &is->pictq[is->pictq_rindex];
  if(vp->bmp) {
    if(is->video_ctx->sample_aspect_ratio.num == 0) {
      aspect_ratio = 0;
    } else {
      aspect_ratio = av_q2d(is->video_ctx->sample_aspect_ratio) *
	is->video_ctx->width / is->video_ctx->height;
    }
    if(aspect_ratio <= 0.0) {
      aspect_ratio = (float)is->video_ctx->width /
	(float)is->video_ctx->height;
    }
    h = screen->h;
    w = ((int)rint(h * aspect_ratio)) & -3;
    if(w > screen->w) {
      w = screen->w;
      h = ((int)rint(w / aspect_ratio)) & -3;
    }
    x = (screen->w - w) / 2;
    y = (screen->h - h) / 2;
    
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_LockMutex(screen_mutex);
    SDL_DisplayYUVOverlay(vp->bmp, &rect);
    SDL_UnlockMutex(screen_mutex);

  }
}

void video_refresh_timer(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;
  
  if(is->video_st) {
    if(is->pictq_size == 0) {
      schedule_refresh(is, 1);
    } else {
      vp = &is->pictq[is->pictq_rindex];
      /* Now, normally here goes a ton of code
	 about timing, etc. we're just going to
	 guess at a delay for now. You can
	 increase and decrease this value and hard code
	 the timing - but I don't suggest that ;)
	 We'll learn how to do it for real later.
      */
      schedule_refresh(is, 40);
      
      /* show the picture! */
      video_display(is);
      
      /* update queue for next picture! */
      if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
	is->pictq_rindex = 0;
      }
      SDL_LockMutex(is->pictq_mutex);
      is->pictq_size--;
      SDL_CondSignal(is->pictq_cond);
      SDL_UnlockMutex(is->pictq_mutex);
    }
  } else {
    schedule_refresh(is, 100);
  }
}
      
void alloc_picture(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;

  vp = &is->pictq[is->pictq_windex];
  if(vp->bmp) {
    // we already have one make another, bigger/smaller
    SDL_FreeYUVOverlay(vp->bmp);
  }
  // Allocate a place to put our YUV image on that screen
  SDL_LockMutex(screen_mutex);
  vp->bmp = SDL_CreateYUVOverlay(is->video_ctx->width,
				 is->video_ctx->height,
				 SDL_YV12_OVERLAY,
				 screen);
  SDL_UnlockMutex(screen_mutex);

  vp->width = is->video_ctx->width;
  vp->height = is->video_ctx->height;
  vp->allocated = 1;

}

int queue_picture(VideoState *is, AVFrame *pFrame) {

  VideoPicture *vp;
  int dst_pix_fmt;
  AVPicture pict;

  /* wait until we have space for a new pic */
  SDL_LockMutex(is->pictq_mutex);
  while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
	!is->quit) {
    SDL_CondWait(is->pictq_cond, is->pictq_mutex);
  }
  SDL_UnlockMutex(is->pictq_mutex);

  if(is->quit)
    return -1;

  // windex is set to 0 initially
  vp = &is->pictq[is->pictq_windex];

  /* allocate or resize the buffer! */
  if(!vp->bmp ||
     vp->width != is->video_ctx->width ||
     vp->height != is->video_ctx->height) {
    SDL_Event event;

    vp->allocated = 0;
    alloc_picture(is);
    if(is->quit) {
      return -1;
    }
  }

  /* We have a place to put our picture on the queue */

  if(vp->bmp) {

    SDL_LockYUVOverlay(vp->bmp);
    
    dst_pix_fmt = PIX_FMT_YUV420P;
    /* point pict at the queue */

    pict.data[0] = vp->bmp->pixels[0];
    pict.data[1] = vp->bmp->pixels[2];
    pict.data[2] = vp->bmp->pixels[1];
    
    pict.linesize[0] = vp->bmp->pitches[0];
    pict.linesize[1] = vp->bmp->pitches[2];
    pict.linesize[2] = vp->bmp->pitches[1];
    
    // Convert the image into YUV format that SDL uses
    sws_scale(is->sws_ctx, (uint8_t const * const *)pFrame->data,
	      pFrame->linesize, 0, is->video_ctx->height,
	      pict.data, pict.linesize);
    
    SDL_UnlockYUVOverlay(vp->bmp);
    /* now we inform our display thread that we have a pic ready */
    if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
      is->pictq_windex = 0;
    }
    SDL_LockMutex(is->pictq_mutex);
    is->pictq_size++;
    SDL_UnlockMutex(is->pictq_mutex);
  }
  return 0;
}

int video_thread(void *arg) {
  VideoState *is = (VideoState *)arg;
  AVPacket pkt1, *packet = &pkt1;
  int frameFinished;
  AVFrame *pFrame;

  pFrame = av_frame_alloc();

  for(;;) {
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
      // means we quit getting packets
      break;
    }
    // Decode video frame
    avcodec_decode_video2(is->video_ctx, pFrame, &frameFinished, packet);
    // Did we get a video frame?
    if(frameFinished) {
      if(queue_picture(is, pFrame) < 0) {
	break;
      }      
    }
    av_free_packet(packet);
  }
  av_frame_free(&pFrame);
  return 0;
}

int stream_component_open(VideoState *is, int stream_index) {

  AVFormatContext *pFormatCtx = is->pFormatCtx;
  AVCodecContext *codecCtx = NULL;
  AVCodec *codec = NULL;
  SDL_AudioSpec wanted_spec, spec;

  if(stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
    return -1;
  }

  codec = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codec->codec_id);
  if(!codec) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  codecCtx = avcodec_alloc_context3(codec);
  if(avcodec_copy_context(codecCtx, pFormatCtx->streams[stream_index]->codec) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }


  if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
    // Set audio settings from codec info
    wanted_spec.freq = codecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = codecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = is;
    
    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }
  }
  if(avcodec_open2(codecCtx, codec, NULL) < 0) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  switch(codecCtx->codec_type) {
  case AVMEDIA_TYPE_AUDIO:
    is->audioStream = stream_index;
    is->audio_st = pFormatCtx->streams[stream_index];
    is->audio_ctx = codecCtx;
    is->audio_buf_size = 0;
    is->audio_buf_index = 0;
    memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
    packet_queue_init(&is->audioq);
    SDL_PauseAudio(0);
    break;
  case AVMEDIA_TYPE_VIDEO:
    is->videoStream = stream_index;
    is->video_st = pFormatCtx->streams[stream_index];
    is->video_ctx = codecCtx;
    packet_queue_init(&is->videoq);
    is->video_tid = SDL_CreateThread(video_thread, is);
    is->sws_ctx = sws_getContext(is->video_ctx->width, is->video_ctx->height,
				 is->video_ctx->pix_fmt, is->video_ctx->width,
				 is->video_ctx->height, PIX_FMT_YUV420P,
				 SWS_BILINEAR, NULL, NULL, NULL
				 );
    break;
  default:
    break;
  }
}

int decode_thread(void *arg) {

  VideoState *is = (VideoState *)arg;
  AVFormatContext *pFormatCtx;
  AVPacket pkt1, *packet = &pkt1;

  int video_index = -1;
  int audio_index = -1;
  int i;

  is->videoStream=-1;
  is->audioStream=-1;

  global_video_state = is;

  // Open video file
  if(avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)!=0)
    return -1; // Couldn't open file

  is->pFormatCtx = pFormatCtx;
  
  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; // Couldn't find stream information
  
  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, is->filename, 0);
  
  // Find the first video stream

  for(i=0; i<pFormatCtx->nb_streams; i++) {
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO &&
       video_index < 0) {
      video_index=i;
    }
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
       audio_index < 0) {
      audio_index=i;
    }
  }
  if(audio_index >= 0) {
    stream_component_open(is, audio_index);
  }
  if(video_index >= 0) {
    stream_component_open(is, video_index);
  }   

  if(is->videoStream < 0 || is->audioStream < 0) {
    fprintf(stderr, "%s: could not open codecs\n", is->filename);
    goto fail;
  }

  // main decode loop

  for(;;) {
    if(is->quit) {
      break;
    }
    // seek stuff goes here
    if(is->audioq.size > MAX_AUDIOQ_SIZE ||
       is->videoq.size > MAX_VIDEOQ_SIZE) {
      SDL_Delay(10);
      continue;
    }
    if(av_read_frame(is->pFormatCtx, packet) < 0) {
      if(is->pFormatCtx->pb->error == 0) {
	SDL_Delay(100); /* no error; wait for user input */
	continue;
      } else {
	break;
      }
    }
    // Is this a packet from the video stream?
    if(packet->stream_index == is->videoStream) {
      packet_queue_put(&is->videoq, packet);
    } else if(packet->stream_index == is->audioStream) {
      packet_queue_put(&is->audioq, packet);
    } else {
      av_free_packet(packet);
    }
  }
  /* all done - wait for it */
  while(!is->quit) {
    SDL_Delay(100);
  }

 fail:
  if(1){
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    event.user.data1 = is;
    SDL_PushEvent(&event);
  }
  return 0;
}

int main(int argc, char *argv[]) {

  SDL_Event       event;

  VideoState      *is;

  is = av_mallocz(sizeof(VideoState));

  if(argc < 2) {
    fprintf(stderr, "Usage: test <file>\n");
    exit(1);
  }
  // Register all formats and codecs
  av_register_all();
  
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Make a screen to put our video
#ifndef __DARWIN__
        screen = SDL_SetVideoMode(640, 480, 0, 0);
#else
        screen = SDL_SetVideoMode(640, 480, 24, 0);
#endif
  if(!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }

  screen_mutex = SDL_CreateMutex();

  av_strlcpy(is->filename, argv[1], sizeof(is->filename));

  is->pictq_mutex = SDL_CreateMutex();
  is->pictq_cond = SDL_CreateCond();

  schedule_refresh(is, 40);

  is->parse_tid = SDL_CreateThread(decode_thread, is);
  if(!is->parse_tid) {
    av_free(is);
    return -1;
  }
  for(;;) {

    SDL_WaitEvent(&event);
    switch(event.type) {
    case FF_QUIT_EVENT:
    case SDL_QUIT:
      is->quit = 1;
      SDL_Quit();
      return 0;
      break;
    case FF_REFRESH_EVENT:
      video_refresh_timer(event.user.data1);
      break;
    default:
      break;
    }
  }
  return 0;

}

```

## Tutorial 05: Synching Video

When I first made this tutorial, all of my syncing code was pulled from
ffplay.c. Today, it is a totally different program, and improvements in the
ffmpeg libraries (and in ffplay.c itself) have caused some strategies to
change. While this code still works, it doesn't look good, and there are many
more improvements that this tutorial could use.

### How Video Syncs

So this whole time, we've had an essentially useless movie player. It plays
the video, yeah, and it plays the audio, yeah, but it's not quite yet what we
would call a _movie_. So what do we do?

#### PTS and DTS

Fortunately, both the audio and video streams have the information about how
fast and when you are supposed to play them inside of them. Audio streams have
a sample rate, and the video streams have a frames per second value. However,
if we simply synced the video by just counting frames and multiplying by frame
rate, there is a chance that it will go out of sync with the audio. Instead,
packets from the stream might have what is called a **decoding time stamp
(DTS)** and a **presentation time stamp (PTS)**. To understand these two
values, you need to know about the way movies are stored. Some formats, like
MPEG, use what they call "B" frames (B stands for "bidirectional"). The two
other kinds of frames are called "I" frames and "P" frames ("I" for "intra"
and "P" for "predicted"). I frames contain a full image. P frames depend upon
previous I and P frames and are like diffs or deltas. B frames are the same as
P frames, but depend upon information found in frames that are displayed both
_before_ and _after_ them! This explains why we might not have a finished
frame after we call `avcodec_decode_video2`.

So let's say we had a movie, and the frames were displayed like: I B B P. Now,
we need to know the information in P before we can display either B frame.
Because of this, the frames might be stored like this: I P B B. This is why we
have a decoding timestamp and a presentation timestamp on each frame. The
decoding timestamp tells us when we need to decode something, and the
presentation time stamp tells us when we need to display something. So, in
this case, our stream might look like this:

```
   PTS: 1 4 2 3
   DTS: 1 2 3 4
Stream: I P B B
```    
    

Generally the PTS and DTS will only differ when the stream we are playing has
B frames in it.

When we get a packet from `av_read_frame()`, it will contain the PTS and DTS
values for the information inside that packet. But what we really want is the
PTS of our newly decoded raw frame, so we know when to display it.

Fortunately, FFMpeg supplies us with a "best effort" timestamp, which you can
get via, av_frame_get_best_effort_timestamp()

#### Synching

Now, while it's all well and good to know when we're supposed to show a
particular video frame, but how do we actually do so? Here's the idea: after
we show a frame, we figure out when the next frame should be shown. Then we
simply set a new timeout to refresh the video again after that amount of time.
As you might expect, we check the value of the PTS of the next frame against
the system clock to see how long our timeout should be. This approach works,
but there are two issues that need to be dealt with.

First is the issue of knowing when the next PTS will be. Now, you might think
that we can just add the video rate to the current PTS -- and you'd be mostly
right. However, some kinds of video call for frames to be repeated. This means
that we're supposed to repeat the current frame a certain number of times.
This could cause the program to display the next frame too soon. So we need to
account for that.

The second issue is that as the program stands now, the video and the audio
chugging away happily, not bothering to sync at all. We wouldn't have to worry
about that if everything worked perfectly. But your computer isn't perfect,
and a lot of video files aren't, either. So we have three choices: sync the
audio to the video, sync the video to the audio, or sync both to an external
clock (like your computer). For now, we're going to sync the video to the
audio.

### Coding it: getting the frame PTS

Now let's get into the code to do all this. We're going to need to add some
more members to our big struct, but we'll do this as we need to. First let's
look at our video thread. Remember, this is where we pick up the packets that
were put on the queue by our decode thread. What we need to do in this part of
the code is get the PTS of the frame given to us by `avcodec_decode_video2`.
The first way we talked about was getting the DTS of the last packet
processed, which is pretty easy:

    
```c    
double pts;
    
for(;;) {
  if(packet_queue_get(&is-;>videoq, packet, 1) < 0) {
    // means we quit getting packets
    break;
  }
  pts = 0;
  // Decode video frame
  len1 = avcodec_decode_video2(is->video_st->codec,
                               pFrame, &frameFinished;, packet);
  if(packet->dts != AV_NOPTS_VALUE) {
    pts = av_frame_get_best_effort_timestamp(pFrame);
  } else {
    pts = 0;
  }
  pts *= av_q2d(is->video_st->time_base);

```    

We set the PTS to 0 if we can't figure out what it is.

Well, that was easy. A technical note: You may have noticed we're using
`int64` for the PTS. This is because the PTS is stored as an integer. This
value is a timestamp that corresponds to a measurement of time in that
stream's time_base unit. For example, if a stream has 24 frames per second, a
PTS of 42 is going to indicate that the frame should go where the 42nd frame
would be if there we had a frame every 1/24 of a second (certainly not
necessarily true).

We can convert this value to seconds by dividing by the framerate. The
`time_base` value of the stream is going to be 1/framerate (for fixed-fps
content), so to get the PTS in seconds, we multiply by the `time_base`.

### Coding: Synching and using the PTS

So now we've got our PTS all set. Now we've got to take care of the two
synchronization problems we talked about above. We're going to define a
function called `synchronize_video` that will update the PTS to be in sync
with everything. This function will also finally deal with cases where we
don't get a PTS value for our frame. At the same time we need to keep track of
when the next frame is expected so we can set our refresh rate properly. We
can accomplish this by using an internal video_clock value which keeps track
of how much time has passed according to the video. We add this value to our
big struct.

    
    
    typedef struct VideoState {
      double          video_clock; // pts of last decoded frame / predicted pts of next decoded frame
    

Here's the synchronize_video function, which is pretty self-explanatory:

    
```c    
double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {
    
  double frame_delay;
    
  if(pts != 0) {
    /* if we have pts, set video clock to it */
    is->video_clock = pts;
  } else {
    /* if we aren't given a pts, set it to the clock */
    pts = is->video_clock;
  }
  /* update the video clock */
  frame_delay = av_q2d(is->video_st->codec->time_base);
  /* if we are repeating a frame, adjust clock accordingly */
  frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
  is->video_clock += frame_delay;
  return pts;
}
 
```

You'll notice we account for repeated frames in this function, too.

Now let's get our proper PTS and queue up the frame using `queue_picture`,
adding a new pts argument:

```c
        // Did we get a video frame?
        if(frameFinished) {
          pts = synchronize_video(is, pFrame, pts);
          if(queue_picture(is, pFrame, pts) < 0) {
    	break;
          }
        }
```

The only thing that changes about `queue_picture` is that we save that pts
value to the VideoPicture structure that we queue up. So we have to add a pts
variable to the struct and add a line of code:

    
```c
    typedef struct VideoPicture {
      ...
      double pts;
    }
    int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {
      ... stuff ...
      if(vp->bmp) {
        ... convert picture ...
        vp->pts = pts;
        ... alert queue ...
      }

```

So now we've got pictures lining up onto our picture queue with proper PTS
values, so let's take a look at our video refreshing function. You may recall
from last time that we just faked it and put a refresh of 80ms. Well, now
we're going to find out how to actually figure it out.

Our strategy is going to be to predict the time of the next PTS by simply
measuring the time between the previous pts and this one. At the same time, we
need to sync the video to the audio. We're going to make an **audio clock**:
an internal value thatkeeps track of what position the audio we're playing is
at. It's like the digital readout on any mp3 player. Since we're synching the
video to the audio, the video thread uses this value to figure out if it's too
far ahead or too far behind.

We'll get to the implementation later; for now let's assume we have a
`get_audio_clock` function that will give us the time on the audio clock. Once
we have that value, though, what do we do if the video and audio are out of
sync? It would silly to simply try and leap to the correct packet through
seeking or something. Instead, we're just going to adjust the value we've
calculated for the next refresh: if the PTS is too far behind the audio time,
we double our calculated delay. if the PTS is too far ahead of the audio time,
we simply refresh as quickly as possible. Now that we have our adjusted
refresh time, or **delay**, we're going to compare that with our computer's
clock by keeping a running `frame_timer`. This frame timer will sum up all of
our calculated delays while playing the movie. In other words, this
frame_timer is _what time it should be when we display the next frame._ We
simply add the new delay to the frame timer, compare it to the time on our
computer's clock, and use that value to schedule the next refresh. This might
be a bit confusing, so study the code carefully:

```c
void video_refresh_timer(void *userdata) {
    
  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;
  double actual_delay, delay, sync_threshold, ref_clock, diff;
  
  if(is->video_st) {
    if(is->pictq_size == 0) {
      schedule_refresh(is, 1);
    } else {
      vp = &is-;>pictq[is->pictq_rindex];
    
      delay = vp->pts - is->frame_last_pts; /* the pts from last time */
      if(delay <= 0 || delay >= 1.0) {
	/* if incorrect delay, use previous one */
	delay = is->frame_last_delay;
      }
      /* save for next time */
      is->frame_last_delay = delay;
      is->frame_last_pts = vp->pts;
    
      /* update delay to sync to audio */
      ref_clock = get_audio_clock(is);
      diff = vp->pts - ref_clock;
    
      /* Skip or repeat the frame. Take delay into account
	 FFPlay still doesn't "know if this is the best guess." */
      sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
      if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
	if(diff <= -sync_threshold) {
	  delay = 0;
	} else if(diff >= sync_threshold) {
	  delay = 2 * delay;
	}
      }
      is->frame_timer += delay;
      /* computer the REAL delay */
      actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
      if(actual_delay < 0.010) {
	/* Really it should skip the picture instead */
	actual_delay = 0.010;
      }
      schedule_refresh(is, (int)(actual_delay * 1000 + 0.5));
      /* show the picture! */
      video_display(is);
      
      /* update queue for next picture! */
      if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
	is->pictq_rindex = 0;
      }
      SDL_LockMutex(is->pictq_mutex);
      is->pictq_size--;
      SDL_CondSignal(is->pictq_cond);
      SDL_UnlockMutex(is->pictq_mutex);
    }
  } else {
    schedule_refresh(is, 100);
  }
}
```    

There are a few checks we make: first, we make sure that the delay between the
PTS and the previous PTS make sense. If it doesn't we just guess and use the
last delay. Next, we make sure we have a synch threshold because things are
never going to be perfectly in synch. ffplay uses 0.01 for its value. We also
make sure that the synch threshold is never smaller than the gaps in between
PTS values. Finally, we make the minimum refresh value 10 milliseconds*.*****
Really here we should skip the frame, but we're not going to bother.

We added a bunch of variables to the big struct so don't forget to check the
code. Also, don't forget to initialize the frame timer and the initial
previous frame delay in `stream_component_open`:

```c
is->frame_timer = (double)av_gettime() / 1000000.0;
is->frame_last_delay = 40e-3;
```

### Synching: The Audio Clock

Now it's time for us to implement the audio clock. We can update the clock
time in our `audio_decode_frame` function, which is where we decode the audio.
Now, remember that we don't always process a new packet every time we call
this function, so there are two places we have to update the clock at. The
first place is where we get the new packet: we simply set the audio clock to
the packet's PTS. Then if a packet has multiple frames, we keep time the audio
play by counting the number of samples and multiplying them by the given
samples-per-second rate. So once we have the packet:

    
    
        /* if update, update the audio clock w/pts */
        if(pkt->pts != AV_NOPTS_VALUE) {
          is->audio_clock = av_q2d(is->audio_st->time_base)*pkt->pts;
        }
    

And once we are processing the packet:

    
    
          /* Keep audio_clock up-to-date */
          pts = is->audio_clock;
          *pts_ptr = pts;
          n = 2 * is->audio_st->codec->channels;
          is->audio_clock += (double)data_size /
    	(double)(n * is->audio_st->codec->sample_rate);
    

A few fine details: the template of the function has changed to include
`pts_ptr`, so make sure you change that. `pts_ptr` is a pointer we use to
inform `audio_callback` the pts of the audio packet. This will be used next
time for synchronizing the audio with the video.

Now we can finally implement our `get_audio_clock` function. It's not as
simple as getting the `is->audio_clock` value, thought. Notice that we set the
audio PTS every time we process it, but if you look at the `audio_callback`
function, it takes time to move all the data from our audio packet into our
output buffer. That means that the value in our audio clock could be too far
ahead. So we have to check how much we have left to write. Here's the complete
code:

    
    
    double get_audio_clock(VideoState *is) {
      double pts;
      int hw_buf_size, bytes_per_sec, n;
      
      pts = is->audio_clock; /* maintained in the audio thread */
      hw_buf_size = is->audio_buf_size - is->audio_buf_index;
      bytes_per_sec = 0;
      n = is->audio_st->codec->channels * 2;
      if(is->audio_st) {
        bytes_per_sec = is->audio_st->codec->sample_rate * n;
      }
      if(bytes_per_sec) {
        pts -= (double)hw_buf_size / bytes_per_sec;
      }
      return pts;
    }
    

You should be able to tell why this function works by now ;)

So that's it! Go ahead and compile it:

    
    
    gcc -o tutorial05 tutorial05.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
    `sdl-config --cflags --libs`
    

and **finally!** you can watch a movie on your own movie player. Next time
we'll look at audio synching, and then the tutorial after that we'll talk
about seeking.

```c

// tutorial05.c
// A pedagogical video player that really works!
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101, SDL 1.2.15
// on GCC 4.7.2 in Debian February 2015
// Use
//
// gcc -o tutorial05 tutorial05.c -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
// to build (assuming libavformat and libavcodec are correctly installed, 
// and assuming you have sdl-config. Please refer to SDL docs for your installation.)
//
// Run using
// tutorial04 myvideofile.mpg
//
// to play the video stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <assert.h>
#include <math.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;


typedef struct VideoPicture {
  SDL_Overlay *bmp;
  int width, height; /* source height & width */
  int allocated;
  double pts;
} VideoPicture;

typedef struct VideoState {

  AVFormatContext *pFormatCtx;
  int             videoStream, audioStream;

  double          audio_clock;
  AVStream        *audio_st;
  AVCodecContext  *audio_ctx;
  PacketQueue     audioq;
  uint8_t         audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
  unsigned int    audio_buf_size;
  unsigned int    audio_buf_index;
  AVFrame         audio_frame;
  AVPacket        audio_pkt;
  uint8_t         *audio_pkt_data;
  int             audio_pkt_size;
  int             audio_hw_buf_size;  
  double          frame_timer;
  double          frame_last_pts;
  double          frame_last_delay;
  double          video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
  AVStream        *video_st;
  AVCodecContext  *video_ctx;
  PacketQueue     videoq;
  struct SwsContext *sws_ctx;

  VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int             pictq_size, pictq_rindex, pictq_windex;
  SDL_mutex       *pictq_mutex;
  SDL_cond        *pictq_cond;
  
  SDL_Thread      *parse_tid;
  SDL_Thread      *video_tid;

  char            filename[1024];
  int             quit;
} VideoState;

SDL_Surface     *screen;
SDL_mutex       *screen_mutex;

/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

  AVPacketList *pkt1;
  if(av_dup_packet(pkt) < 0) {
    return -1;
  }
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;
  
  SDL_LockMutex(q->mutex);

  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  SDL_CondSignal(q->cond);
  
  SDL_UnlockMutex(q->mutex);
  return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  AVPacketList *pkt1;
  int ret;

  SDL_LockMutex(q->mutex);
  
  for(;;) {
    
    if(global_video_state->quit) {
      ret = -1;
      break;
    }

    pkt1 = q->first_pkt;
    if (pkt1) {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt)
	q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

double get_audio_clock(VideoState *is) {
  double pts;
  int hw_buf_size, bytes_per_sec, n;
  
  pts = is->audio_clock; /* maintained in the audio thread */
  hw_buf_size = is->audio_buf_size - is->audio_buf_index;
  bytes_per_sec = 0;
  n = is->audio_ctx->channels * 2;
  if(is->audio_st) {
    bytes_per_sec = is->audio_ctx->sample_rate * n;
  }
  if(bytes_per_sec) {
    pts -= (double)hw_buf_size / bytes_per_sec;
  }
  return pts;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr) {

  int len1, data_size = 0;
  AVPacket *pkt = &is->audio_pkt;
  double pts;
  int n;

  for(;;) {
    while(is->audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(is->audio_ctx, &is->audio_frame, &got_frame, pkt);
      if(len1 < 0) {
	/* if error, skip frame */
	is->audio_pkt_size = 0;
	break;
      }
      data_size = 0;
      if(got_frame) {
	data_size = av_samples_get_buffer_size(NULL, 
					       is->audio_ctx->channels,
					       is->audio_frame.nb_samples,
					       is->audio_ctx->sample_fmt,
					       1);
	assert(data_size <= buf_size);
	memcpy(audio_buf, is->audio_frame.data[0], data_size);
      }
      is->audio_pkt_data += len1;
      is->audio_pkt_size -= len1;
      if(data_size <= 0) {
	/* No data yet, get more frames */
	continue;
      }
      pts = is->audio_clock;
      *pts_ptr = pts;
      n = 2 * is->audio_ctx->channels;
      is->audio_clock += (double)data_size /
	(double)(n * is->audio_ctx->sample_rate);
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if(pkt->data)
      av_free_packet(pkt);

    if(is->quit) {
      return -1;
    }
    /* next packet */
    if(packet_queue_get(&is->audioq, pkt, 1) < 0) {
      return -1;
    }
    is->audio_pkt_data = pkt->data;
    is->audio_pkt_size = pkt->size;
    /* if update, update the audio clock w/pts */
    if(pkt->pts != AV_NOPTS_VALUE) {
      is->audio_clock = av_q2d(is->audio_st->time_base)*pkt->pts;
    }
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

  VideoState *is = (VideoState *)userdata;
  int len1, audio_size;
  double pts;

  while(len > 0) {
    if(is->audio_buf_index >= is->audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);
      if(audio_size < 0) {
	/* If error, output silence */
	is->audio_buf_size = 1024;
	memset(is->audio_buf, 0, is->audio_buf_size);
      } else {
	is->audio_buf_size = audio_size;
      }
      is->audio_buf_index = 0;
    }
    len1 = is->audio_buf_size - is->audio_buf_index;
    if(len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
    len -= len1;
    stream += len1;
    is->audio_buf_index += len1;
  }
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay) {
  SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_display(VideoState *is) {

  SDL_Rect rect;
  VideoPicture *vp;
  float aspect_ratio;
  int w, h, x, y;
  int i;

  vp = &is->pictq[is->pictq_rindex];
  if(vp->bmp) {
    if(is->video_ctx->sample_aspect_ratio.num == 0) {
      aspect_ratio = 0;
    } else {
      aspect_ratio = av_q2d(is->video_ctx->sample_aspect_ratio) *
	is->video_ctx->width / is->video_ctx->height;
    }
    if(aspect_ratio <= 0.0) {
      aspect_ratio = (float)is->video_ctx->width /
	(float)is->video_ctx->height;
    }
    h = screen->h;
    w = ((int)rint(h * aspect_ratio)) & -3;
    if(w > screen->w) {
      w = screen->w;
      h = ((int)rint(w / aspect_ratio)) & -3;
    }
    x = (screen->w - w) / 2;
    y = (screen->h - h) / 2;
    
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_LockMutex(screen_mutex);
    SDL_DisplayYUVOverlay(vp->bmp, &rect);
    SDL_UnlockMutex(screen_mutex);

  }
}

void video_refresh_timer(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;
  double actual_delay, delay, sync_threshold, ref_clock, diff;
  
  if(is->video_st) {
    if(is->pictq_size == 0) {
      schedule_refresh(is, 1);
    } else {
      vp = &is->pictq[is->pictq_rindex];

      delay = vp->pts - is->frame_last_pts; /* the pts from last time */
      if(delay <= 0 || delay >= 1.0) {
	/* if incorrect delay, use previous one */
	delay = is->frame_last_delay;
      }
      /* save for next time */
      is->frame_last_delay = delay;
      is->frame_last_pts = vp->pts;

      /* update delay to sync to audio */
      ref_clock = get_audio_clock(is);
      diff = vp->pts - ref_clock;

      /* Skip or repeat the frame. Take delay into account
	 FFPlay still doesn't "know if this is the best guess." */
      sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
      if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
	if(diff <= -sync_threshold) {
	  delay = 0;
	} else if(diff >= sync_threshold) {
	  delay = 2 * delay;
	}
      }
      is->frame_timer += delay;
      /* computer the REAL delay */
      actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
      if(actual_delay < 0.010) {
	/* Really it should skip the picture instead */
	actual_delay = 0.010;
      }
      schedule_refresh(is, (int)(actual_delay * 1000 + 0.5));
      
      /* show the picture! */
      video_display(is);
      
      /* update queue for next picture! */
      if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
	is->pictq_rindex = 0;
      }
      SDL_LockMutex(is->pictq_mutex);
      is->pictq_size--;
      SDL_CondSignal(is->pictq_cond);
      SDL_UnlockMutex(is->pictq_mutex);
    }
  } else {
    schedule_refresh(is, 100);
  }
}
      
void alloc_picture(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;

  vp = &is->pictq[is->pictq_windex];
  if(vp->bmp) {
    // we already have one make another, bigger/smaller
    SDL_FreeYUVOverlay(vp->bmp);
  }
  // Allocate a place to put our YUV image on that screen
  SDL_LockMutex(screen_mutex);
  vp->bmp = SDL_CreateYUVOverlay(is->video_ctx->width,
				 is->video_ctx->height,
				 SDL_YV12_OVERLAY,
				 screen);
  SDL_UnlockMutex(screen_mutex);

  vp->width = is->video_ctx->width;
  vp->height = is->video_ctx->height;
  vp->allocated = 1;

}

int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {

  VideoPicture *vp;
  int dst_pix_fmt;
  AVPicture pict;

  /* wait until we have space for a new pic */
  SDL_LockMutex(is->pictq_mutex);
  while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
	!is->quit) {
    SDL_CondWait(is->pictq_cond, is->pictq_mutex);
  }
  SDL_UnlockMutex(is->pictq_mutex);

  if(is->quit)
    return -1;

  // windex is set to 0 initially
  vp = &is->pictq[is->pictq_windex];

  /* allocate or resize the buffer! */
  if(!vp->bmp ||
     vp->width != is->video_ctx->width ||
     vp->height != is->video_ctx->height) {
    SDL_Event event;

    vp->allocated = 0;
    alloc_picture(is);
    if(is->quit) {
      return -1;
    }
  }

  /* We have a place to put our picture on the queue */

  if(vp->bmp) {

    SDL_LockYUVOverlay(vp->bmp);
    vp->pts = pts;
    
    dst_pix_fmt = PIX_FMT_YUV420P;
    /* point pict at the queue */

    pict.data[0] = vp->bmp->pixels[0];
    pict.data[1] = vp->bmp->pixels[2];
    pict.data[2] = vp->bmp->pixels[1];
    
    pict.linesize[0] = vp->bmp->pitches[0];
    pict.linesize[1] = vp->bmp->pitches[2];
    pict.linesize[2] = vp->bmp->pitches[1];
    
    // Convert the image into YUV format that SDL uses
    sws_scale(is->sws_ctx, (uint8_t const * const *)pFrame->data,
	      pFrame->linesize, 0, is->video_ctx->height,
	      pict.data, pict.linesize);
    
    SDL_UnlockYUVOverlay(vp->bmp);
    /* now we inform our display thread that we have a pic ready */
    if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
      is->pictq_windex = 0;
    }
    SDL_LockMutex(is->pictq_mutex);
    is->pictq_size++;
    SDL_UnlockMutex(is->pictq_mutex);
  }
  return 0;
}

double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

  double frame_delay;

  if(pts != 0) {
    /* if we have pts, set video clock to it */
    is->video_clock = pts;
  } else {
    /* if we aren't given a pts, set it to the clock */
    pts = is->video_clock;
  }
  /* update the video clock */
  frame_delay = av_q2d(is->video_ctx->time_base);
  /* if we are repeating a frame, adjust clock accordingly */
  frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
  is->video_clock += frame_delay;
  return pts;
}

int video_thread(void *arg) {
  VideoState *is = (VideoState *)arg;
  AVPacket pkt1, *packet = &pkt1;
  int frameFinished;
  AVFrame *pFrame;
  double pts;

  pFrame = av_frame_alloc();

  for(;;) {
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
      // means we quit getting packets
      break;
    }
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
      // means we quit getting packets
      break;
    }
    pts = 0;

    // Decode video frame
    avcodec_decode_video2(is->video_ctx, pFrame, &frameFinished, packet);

    if((pts = av_frame_get_best_effort_timestamp(pFrame)) == AV_NOPTS_VALUE) {
      pts = 0;
    }
    pts *= av_q2d(is->video_st->time_base);

    // Did we get a video frame?
    if(frameFinished) {
      pts = synchronize_video(is, pFrame, pts);
      if(queue_picture(is, pFrame, pts) < 0) {
	break;
      }
    }
    av_free_packet(packet);
  }
  av_frame_free(&pFrame);
  return 0;
}

int stream_component_open(VideoState *is, int stream_index) {

  AVFormatContext *pFormatCtx = is->pFormatCtx;
  AVCodecContext *codecCtx = NULL;
  AVCodec *codec = NULL;
  SDL_AudioSpec wanted_spec, spec;

  if(stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
    return -1;
  }

  codec = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codec->codec_id);
  if(!codec) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  codecCtx = avcodec_alloc_context3(codec);
  if(avcodec_copy_context(codecCtx, pFormatCtx->streams[stream_index]->codec) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }


  if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
    // Set audio settings from codec info
    wanted_spec.freq = codecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = codecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = is;
    
    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }
    is->audio_hw_buf_size = spec.size;
  }
  if(avcodec_open2(codecCtx, codec, NULL) < 0) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  switch(codecCtx->codec_type) {
  case AVMEDIA_TYPE_AUDIO:
    is->audioStream = stream_index;
    is->audio_st = pFormatCtx->streams[stream_index];
    is->audio_ctx = codecCtx;
    is->audio_buf_size = 0;
    is->audio_buf_index = 0;
    memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
    packet_queue_init(&is->audioq);
    SDL_PauseAudio(0);
    break;
  case AVMEDIA_TYPE_VIDEO:
    is->videoStream = stream_index;
    is->video_st = pFormatCtx->streams[stream_index];
    is->video_ctx = codecCtx;

    is->frame_timer = (double)av_gettime() / 1000000.0;
    is->frame_last_delay = 40e-3;
    
    packet_queue_init(&is->videoq);
    is->video_tid = SDL_CreateThread(video_thread, is);
    is->sws_ctx = sws_getContext(is->video_ctx->width, is->video_ctx->height,
				 is->video_ctx->pix_fmt, is->video_ctx->width,
				 is->video_ctx->height, PIX_FMT_YUV420P,
				 SWS_BILINEAR, NULL, NULL, NULL
				 );
    break;
  default:
    break;
  }
}

int decode_thread(void *arg) {

  VideoState *is = (VideoState *)arg;
  AVFormatContext *pFormatCtx;
  AVPacket pkt1, *packet = &pkt1;

  int video_index = -1;
  int audio_index = -1;
  int i;

  is->videoStream=-1;
  is->audioStream=-1;

  global_video_state = is;

  // Open video file
  if(avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)!=0)
    return -1; // Couldn't open file

  is->pFormatCtx = pFormatCtx;
  
  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; // Couldn't find stream information
  
  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, is->filename, 0);
  
  // Find the first video stream

  for(i=0; i<pFormatCtx->nb_streams; i++) {
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO &&
       video_index < 0) {
      video_index=i;
    }
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
       audio_index < 0) {
      audio_index=i;
    }
  }
  if(audio_index >= 0) {
    stream_component_open(is, audio_index);
  }
  if(video_index >= 0) {
    stream_component_open(is, video_index);
  }   

  if(is->videoStream < 0 || is->audioStream < 0) {
    fprintf(stderr, "%s: could not open codecs\n", is->filename);
    goto fail;
  }

  // main decode loop

  for(;;) {
    if(is->quit) {
      break;
    }
    // seek stuff goes here
    if(is->audioq.size > MAX_AUDIOQ_SIZE ||
       is->videoq.size > MAX_VIDEOQ_SIZE) {
      SDL_Delay(10);
      continue;
    }
    if(av_read_frame(is->pFormatCtx, packet) < 0) {
      if(is->pFormatCtx->pb->error == 0) {
	SDL_Delay(100); /* no error; wait for user input */
	continue;
      } else {
	break;
      }
    }
    // Is this a packet from the video stream?
    if(packet->stream_index == is->videoStream) {
      packet_queue_put(&is->videoq, packet);
    } else if(packet->stream_index == is->audioStream) {
      packet_queue_put(&is->audioq, packet);
    } else {
      av_free_packet(packet);
    }
  }
  /* all done - wait for it */
  while(!is->quit) {
    SDL_Delay(100);
  }

 fail:
  if(1){
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    event.user.data1 = is;
    SDL_PushEvent(&event);
  }
  return 0;
}

int main(int argc, char *argv[]) {

  SDL_Event       event;

  VideoState      *is;

  is = av_mallocz(sizeof(VideoState));

  if(argc < 2) {
    fprintf(stderr, "Usage: test <file>\n");
    exit(1);
  }
  // Register all formats and codecs
  av_register_all();
  
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Make a screen to put our video
#ifndef __DARWIN__
        screen = SDL_SetVideoMode(640, 480, 0, 0);
#else
        screen = SDL_SetVideoMode(640, 480, 24, 0);
#endif
  if(!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }

  screen_mutex = SDL_CreateMutex();

  av_strlcpy(is->filename, argv[1], sizeof(is->filename));

  is->pictq_mutex = SDL_CreateMutex();
  is->pictq_cond = SDL_CreateCond();

  schedule_refresh(is, 40);

  is->parse_tid = SDL_CreateThread(decode_thread, is);
  if(!is->parse_tid) {
    av_free(is);
    return -1;
  }
  for(;;) {

    SDL_WaitEvent(&event);
    switch(event.type) {
    case FF_QUIT_EVENT:
    case SDL_QUIT:
      is->quit = 1;
      SDL_Quit();
      return 0;
      break;
    case FF_REFRESH_EVENT:
      video_refresh_timer(event.user.data1);
      break;
    default:
      break;
    }
  }
  return 0;

}

```

## Tutorial 06: Synching Audio

Code: tutorial06.c

### Synching Audio

So now we have a decent enough player to watch a movie, so let's see what kind
of loose ends we have lying around. Last time, we glossed over synchronization
a little bit, namely sychronizing audio to a video clock rather than the other
way around. We're going to do this the same way as with the video: make an
internal video clock to keep track of how far along the video thread is and
sync the audio to that. Later we'll look at how to generalize things to sync
both audio and video to an external clock, too.

### Implementing the video clock

Now we want to implement a video clock similar to the audio clock we had last
time: an internal value that gives the current time offset of the video
currently being played. At first, you would think that this would be as simple
as updating the timer with the current PTS of the last frame to be shown.
However, don't forget that the time between video frames can be pretty long
when we get down to the millisecond level. The solution is to keep track of
another value, the time at which we set the video clock to the PTS of the last
frame. That way the current value of the video clock will be
`PTS_of_last_frame + (current_time - time_elapsed_since_PTS_value_was_set)`.
This solution is very similar to what we did with `get_audio_clock`.

So, in our big struct, we're going to put a `double video_current_pts` and a
`int64_t video_current_pts_time`. The clock updating is going to take place in
the `video_refresh_timer` function:

    
    
    void video_refresh_timer(void *userdata) {
    
      /* ... */
    
      if(is->video_st) {
        if(is->pictq_size == 0) {
          schedule_refresh(is, 1);
        } else {
          vp = &is-;>pictq[is->pictq_rindex];
    
          is->video_current_pts = vp->pts;
          is->video_current_pts_time = av_gettime();
    

Don't forget to initialize it in `stream_component_open`:

    
    
        is->video_current_pts_time = av_gettime();
    

And now all we need is a way to get the information:

    
    
    double get_video_clock(VideoState *is) {
      double delta;
    
      delta = (av_gettime() - is->video_current_pts_time) / 1000000.0;
      return is->video_current_pts + delta;
    }
    

### Abstracting the clock

But why force ourselves to use the video clock? We'd have to go and alter our
video sync code so that the audio and video aren't trying to sync to each
other. Imagine the mess if we tried to make it a command line option like it
is in ffplay. So let's abstract things: we're going to make a new wrapper
function, `get_master_clock` that checks an av_sync_type variable and then
call `get_audio_clock`, `get_video_clock`, or whatever other clock we want to
use. We could even use the computer clock, which we'll call
`get_external_clock`:

    
    
    enum {
      AV_SYNC_AUDIO_MASTER,
      AV_SYNC_VIDEO_MASTER,
      AV_SYNC_EXTERNAL_MASTER,
    };
    
    #define DEFAULT_AV_SYNC_TYPE AV_SYNC_VIDEO_MASTER
    
    double get_master_clock(VideoState *is) {
      if(is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        return get_video_clock(is);
      } else if(is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        return get_audio_clock(is);
      } else {
        return get_external_clock(is);
      }
    }
    main() {
    ...
      is->av_sync_type = DEFAULT_AV_SYNC_TYPE;
    ...
    }
    

### Synchronizing the Audio

Now the hard part: synching the audio to the video clock. Our strategy is
going to be to measure where the audio is, compare it to the video clock, and
then figure out how many samples we need to adjust by, that is, do we need to
speed up by dropping samples or do we need to slow down by adding them?

We're going to run a `synchronize_audio` function each time we process each
set of audio samples we get to shrink or expand them properly. However, we
don't want to sync every single time it's off because process audio a lot more
often than video packets. So we're going to set a minimum number of
consecutive calls to the `synchronize_audio` function that have to be out of
sync before we bother doing anything. Of course, just like last time, "out of
sync" means that the audio clock and the video clock differ by more than our
sync threshold.

**Note:** What the heck is going on here? This equation looks like magic! Well, it's basically a weighted mean using a geometric series as weights. I don't know if there's a name for this (I even checked Wikipedia!) but for more info, here's an explanation (or at weightedmean.txt) So we're going to use a fractional coefficient, say `c`, and So now let's say we've gotten N audio sample sets that have been out of sync. The amount we are out of sync can also vary a good deal, so we're going to take an average of how far each of those have been out of sync. So for example, the first call might have shown we were out of sync by 40ms, the next by 50ms, and so on. But we're not going to take a simple average because the most recent values are more important than the previous ones. So we're going to use a fractional coefficient, say `c`, and sum the differences like this: `diff_sum = new_diff + diff_sum*c`. When we are ready to find the average difference, we simply calculate `avg_diff = diff_sum * (1-c)`. 

Here's what our function looks like so far:

    
    
    /* Add or subtract samples to get a better sync, return new
       audio buffer size */
    int synchronize_audio(VideoState *is, short *samples,
    		      int samples_size, double pts) {
      int n;
      double ref_clock;
      
      n = 2 * is->audio_st->codec->channels;
      
      if(is->av_sync_type != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int wanted_size, min_size, max_size, nb_samples;
        
        ref_clock = get_master_clock(is);
        diff = get_audio_clock(is) - ref_clock;
    
        if(diff < AV_NOSYNC_THRESHOLD) {
          // accumulate the diffs
          is->audio_diff_cum = diff + is->audio_diff_avg_coef
    	* is->audio_diff_cum;
          if(is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
    	is->audio_diff_avg_count++;
          } else {
    	avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);
    
           /* Shrinking/expanding buffer code.... */
    
          }
        } else {
          /* difference is TOO big; reset diff stuff */
          is->audio_diff_avg_count = 0;
          is->audio_diff_cum = 0;
        }
      }
      return samples_size;
    }
    

So we're doing pretty well; we know approximately how off the audio is from
the video or whatever we're using for a clock. So let's now calculate how many
samples we need to add or lop off by putting this code where the
"Shrinking/expanding buffer code" section is:

    
    
    if(fabs(avg_diff) >= is->audio_diff_threshold) {
      wanted_size = samples_size + 
      ((int)(diff * is->audio_st->codec->sample_rate) * n);
      min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX)
                                 / 100);
      max_size = samples_size * ((100 + SAMPLE_CORRECTION_PERCENT_MAX) 
                                 / 100);
      if(wanted_size < min_size) {
        wanted_size = min_size;
      } else if (wanted_size > max_size) {
        wanted_size = max_size;
      }
    

Remember that `audio_length * (sample_rate * # of channels * 2)` is the number
of samples in `audio_length` seconds of audio. Therefore, number of samples we
want is going to be the number of samples we already have plus or minus the
number of samples that correspond to the amount of time the audio has drifted.
We'll also set a limit on how big or small our correction can be because if we
change our buffer too much, it'll be too jarring to the user.

### Correcting the number of samples

Now we have to actually correct the audio. You may have noticed that our
`synchronize_audio` function returns a sample size, which will then tell us
how many bytes to send to the stream. So we just have to adjust the sample
size to the `wanted_size`. This works for making the sample size smaller. But
if we want to make it bigger, we can't just make the sample size larger
because there's no more data in the buffer! So we have to add it. But what
should we add? It would be foolish to try and extrapolate audio, so let's just
use the audio we already have by padding out the buffer with the value of the
last sample.

    
    
    if(wanted_size < samples_size) {
      /* remove samples */
      samples_size = wanted_size;
    } else if(wanted_size > samples_size) {
      uint8_t *samples_end, *q;
      int nb;
    
      /* add samples by copying final samples */
      nb = (samples_size - wanted_size);
      samples_end = (uint8_t *)samples + samples_size - n;
      q = samples_end + n;
      while(nb > 0) {
        memcpy(q, samples_end, n);
        q += n;
        nb -= n;
      }
      samples_size = wanted_size;
    }
    

Now we return the sample size, and we're done with that function. All we need
to do now is use it:

    
    
    void audio_callback(void *userdata, Uint8 *stream, int len) {
    
      VideoState *is = (VideoState *)userdata;
      int len1, audio_size;
      double pts;
    
      while(len > 0) {
        if(is->audio_buf_index >= is->audio_buf_size) {
          /* We have already sent all our data; get more */
          audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts;);
          if(audio_size < 0) {
    	/* If error, output silence */
    	is->audio_buf_size = 1024;
    	memset(is->audio_buf, 0, is->audio_buf_size);
          } else {
    	audio_size = synchronize_audio(is, (int16_t *)is->audio_buf,
    				       audio_size, pts);
    	is->audio_buf_size = audio_size;
    

All we did is inserted the call to `synchronize_audio`. (Also, make sure to
check the source code where we initalize the above variables I didn't bother
to define.)

One last thing before we finish: we need to add an if clause to make sure we
don't sync the video if it is the master clock:

    
    
    if(is->av_sync_type != AV_SYNC_VIDEO_MASTER) {
      ref_clock = get_master_clock(is);
      diff = vp->pts - ref_clock;
    
      /* Skip or repeat the frame. Take delay into account
         FFPlay still doesn't "know if this is the best guess." */
      sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay :
                        AV_SYNC_THRESHOLD;
      if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
        if(diff <= -sync_threshold) {
          delay = 0;
        } else if(diff >= sync_threshold) {
          delay = 2 * delay;
        }
      }
    }
    

And that does it! Make sure you check through the source file to initialize
any variables that I didn't bother defining or initializing. Then compile it:

    
    
    gcc -o tutorial06 tutorial06.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
    `sdl-config --cflags --libs`
    

and you'll be good to go.

Next time we'll make it so you can rewind and fast forward your movie.

```c

// tutorial05.c
// A pedagogical video player that really works!
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101, SDL 1.2.15
// on GCC 4.7.2 in Debian February 2015
// Use
//
// gcc -o tutorial05 tutorial05.c -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
// to build (assuming libavformat and libavcodec are correctly installed, 
// and assuming you have sdl-config. Please refer to SDL docs for your installation.)
//
// Run using
// tutorial04 myvideofile.mpg
//
// to play the video stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <assert.h>
#include <math.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

#define DEFAULT_AV_SYNC_TYPE AV_SYNC_VIDEO_MASTER

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;


typedef struct VideoPicture {
  SDL_Overlay *bmp;
  int width, height; /* source height & width */
  int allocated;
  double pts;
} VideoPicture;

typedef struct VideoState {

  AVFormatContext *pFormatCtx;
  int             videoStream, audioStream;

  int             av_sync_type;
  double          external_clock; /* external clock base */
  int64_t         external_clock_time;

  double          audio_clock;
  AVStream        *audio_st;
  AVCodecContext  *audio_ctx;
  PacketQueue     audioq;
  uint8_t         audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
  unsigned int    audio_buf_size;
  unsigned int    audio_buf_index;
  AVFrame         audio_frame;
  AVPacket        audio_pkt;
  uint8_t         *audio_pkt_data;
  int             audio_pkt_size;
  int             audio_hw_buf_size;
  double          audio_diff_cum; /* used for AV difference average computation */
  double          audio_diff_avg_coef;
  double          audio_diff_threshold;
  int             audio_diff_avg_count;
  double          frame_timer;
  double          frame_last_pts;
  double          frame_last_delay;
  double          video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
  double          video_current_pts; ///<current displayed pts (different from video_clock if frame fifos are used)
  int64_t         video_current_pts_time;  ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts
  AVStream        *video_st;
  AVCodecContext  *video_ctx;
  PacketQueue     videoq;
  struct SwsContext *sws_ctx;

  VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int             pictq_size, pictq_rindex, pictq_windex;
  SDL_mutex       *pictq_mutex;
  SDL_cond        *pictq_cond;
  
  SDL_Thread      *parse_tid;
  SDL_Thread      *video_tid;

  char            filename[1024];
  int             quit;
} VideoState;

enum {
  AV_SYNC_AUDIO_MASTER,
  AV_SYNC_VIDEO_MASTER,
  AV_SYNC_EXTERNAL_MASTER,
};

SDL_Surface     *screen;
SDL_mutex       *screen_mutex;

/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

  AVPacketList *pkt1;
  if(av_dup_packet(pkt) < 0) {
    return -1;
  }
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;
  
  SDL_LockMutex(q->mutex);

  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  SDL_CondSignal(q->cond);
  
  SDL_UnlockMutex(q->mutex);
  return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  AVPacketList *pkt1;
  int ret;

  SDL_LockMutex(q->mutex);
  
  for(;;) {
    
    if(global_video_state->quit) {
      ret = -1;
      break;
    }

    pkt1 = q->first_pkt;
    if (pkt1) {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt)
	q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

double get_audio_clock(VideoState *is) {
  double pts;
  int hw_buf_size, bytes_per_sec, n;
  
  pts = is->audio_clock; /* maintained in the audio thread */
  hw_buf_size = is->audio_buf_size - is->audio_buf_index;
  bytes_per_sec = 0;
  n = is->audio_ctx->channels * 2;
  if(is->audio_st) {
    bytes_per_sec = is->audio_ctx->sample_rate * n;
  }
  if(bytes_per_sec) {
    pts -= (double)hw_buf_size / bytes_per_sec;
  }
  return pts;
}
double get_video_clock(VideoState *is) {
  double delta;

  delta = (av_gettime() - is->video_current_pts_time) / 1000000.0;
  return is->video_current_pts + delta;
}
double get_external_clock(VideoState *is) {
  return av_gettime() / 1000000.0;
}

double get_master_clock(VideoState *is) {
  if(is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
    return get_video_clock(is);
  } else if(is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
    return get_audio_clock(is);
  } else {
    return get_external_clock(is);
  }
}


/* Add or subtract samples to get a better sync, return new
   audio buffer size */
int synchronize_audio(VideoState *is, short *samples,
		      int samples_size, double pts) {
  int n;
  double ref_clock;

  n = 2 * is->audio_ctx->channels;
  
  if(is->av_sync_type != AV_SYNC_AUDIO_MASTER) {
    double diff, avg_diff;
    int wanted_size, min_size, max_size /*, nb_samples */;
    
    ref_clock = get_master_clock(is);
    diff = get_audio_clock(is) - ref_clock;

    if(diff < AV_NOSYNC_THRESHOLD) {
      // accumulate the diffs
      is->audio_diff_cum = diff + is->audio_diff_avg_coef
	* is->audio_diff_cum;
      if(is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
	is->audio_diff_avg_count++;
      } else {
	avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);
	if(fabs(avg_diff) >= is->audio_diff_threshold) {
	  wanted_size = samples_size + ((int)(diff * is->audio_ctx->sample_rate) * n);
	  min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100);
	  max_size = samples_size * ((100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100);
	  if(wanted_size < min_size) {
	    wanted_size = min_size;
	  } else if (wanted_size > max_size) {
	    wanted_size = max_size;
	  }
	  if(wanted_size < samples_size) {
	    /* remove samples */
	    samples_size = wanted_size;
	  } else if(wanted_size > samples_size) {
	    uint8_t *samples_end, *q;
	    int nb;

	    /* add samples by copying final sample*/
	    nb = (samples_size - wanted_size);
	    samples_end = (uint8_t *)samples + samples_size - n;
	    q = samples_end + n;
	    while(nb > 0) {
	      memcpy(q, samples_end, n);
	      q += n;
	      nb -= n;
	    }
	    samples_size = wanted_size;
	  }
	}
      }
    } else {
      /* difference is TOO big; reset diff stuff */
      is->audio_diff_avg_count = 0;
      is->audio_diff_cum = 0;
    }
  }
  return samples_size;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr) {

  int len1, data_size = 0;
  AVPacket *pkt = &is->audio_pkt;
  double pts;
  int n;

  for(;;) {
    while(is->audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(is->audio_ctx, &is->audio_frame, &got_frame, pkt);
      if(len1 < 0) {
	/* if error, skip frame */
	is->audio_pkt_size = 0;
	break;
      }
      data_size = 0;
      if(got_frame) {
	data_size = av_samples_get_buffer_size(NULL, 
					       is->audio_ctx->channels,
					       is->audio_frame.nb_samples,
					       is->audio_ctx->sample_fmt,
					       1);
	assert(data_size <= buf_size);
	memcpy(audio_buf, is->audio_frame.data[0], data_size);
      }
      is->audio_pkt_data += len1;
      is->audio_pkt_size -= len1;
      if(data_size <= 0) {
	/* No data yet, get more frames */
	continue;
      }
      pts = is->audio_clock;
      *pts_ptr = pts;
      n = 2 * is->audio_ctx->channels;
      is->audio_clock += (double)data_size /
	(double)(n * is->audio_ctx->sample_rate);
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if(pkt->data)
      av_free_packet(pkt);

    if(is->quit) {
      return -1;
    }
    /* next packet */
    if(packet_queue_get(&is->audioq, pkt, 1) < 0) {
      return -1;
    }
    is->audio_pkt_data = pkt->data;
    is->audio_pkt_size = pkt->size;
    /* if update, update the audio clock w/pts */
    if(pkt->pts != AV_NOPTS_VALUE) {
      is->audio_clock = av_q2d(is->audio_st->time_base)*pkt->pts;
    }
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

  VideoState *is = (VideoState *)userdata;
  int len1, audio_size;
  double pts;

  while(len > 0) {
    if(is->audio_buf_index >= is->audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);
      if(audio_size < 0) {
	/* If error, output silence */
	is->audio_buf_size = 1024;
	memset(is->audio_buf, 0, is->audio_buf_size);
      } else {
	audio_size = synchronize_audio(is, (int16_t *)is->audio_buf,
				       audio_size, pts);
	is->audio_buf_size = audio_size;
      }
      is->audio_buf_index = 0;
    }
    len1 = is->audio_buf_size - is->audio_buf_index;
    if(len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
    len -= len1;
    stream += len1;
    is->audio_buf_index += len1;
  }
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay) {
  SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_display(VideoState *is) {

  SDL_Rect rect;
  VideoPicture *vp;
  float aspect_ratio;
  int w, h, x, y;
  int i;

  vp = &is->pictq[is->pictq_rindex];
  if(vp->bmp) {
    if(is->video_ctx->sample_aspect_ratio.num == 0) {
      aspect_ratio = 0;
    } else {
      aspect_ratio = av_q2d(is->video_ctx->sample_aspect_ratio) *
	is->video_ctx->width / is->video_ctx->height;
    }
    if(aspect_ratio <= 0.0) {
      aspect_ratio = (float)is->video_ctx->width /
	(float)is->video_ctx->height;
    }
    h = screen->h;
    w = ((int)rint(h * aspect_ratio)) & -3;
    if(w > screen->w) {
      w = screen->w;
      h = ((int)rint(w / aspect_ratio)) & -3;
    }
    x = (screen->w - w) / 2;
    y = (screen->h - h) / 2;
    
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_LockMutex(screen_mutex);
    SDL_DisplayYUVOverlay(vp->bmp, &rect);
    SDL_UnlockMutex(screen_mutex);
  }
}

void video_refresh_timer(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;
  double actual_delay, delay, sync_threshold, ref_clock, diff;
  
  if(is->video_st) {
    if(is->pictq_size == 0) {
      schedule_refresh(is, 1);
    } else {
      vp = &is->pictq[is->pictq_rindex];
      
      is->video_current_pts = vp->pts;
      is->video_current_pts_time = av_gettime();
      delay = vp->pts - is->frame_last_pts; /* the pts from last time */
      if(delay <= 0 || delay >= 1.0) {
	/* if incorrect delay, use previous one */
	delay = is->frame_last_delay;
      }
      /* save for next time */
      is->frame_last_delay = delay;
      is->frame_last_pts = vp->pts;



      /* update delay to sync to audio if not master source */
      if(is->av_sync_type != AV_SYNC_VIDEO_MASTER) {
	ref_clock = get_master_clock(is);
	diff = vp->pts - ref_clock;
	
	/* Skip or repeat the frame. Take delay into account
	   FFPlay still doesn't "know if this is the best guess." */
	sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
	if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
	  if(diff <= -sync_threshold) {
	    delay = 0;
	  } else if(diff >= sync_threshold) {
	    delay = 2 * delay;
	  }
	}
      }
      is->frame_timer += delay;
      /* computer the REAL delay */
      actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
      if(actual_delay < 0.010) {
	/* Really it should skip the picture instead */
	actual_delay = 0.010;
      }
      schedule_refresh(is, (int)(actual_delay * 1000 + 0.5));
      
      /* show the picture! */
      video_display(is);
      
      /* update queue for next picture! */
      if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
	is->pictq_rindex = 0;
      }
      SDL_LockMutex(is->pictq_mutex);
      is->pictq_size--;
      SDL_CondSignal(is->pictq_cond);
      SDL_UnlockMutex(is->pictq_mutex);
    }
  } else {
    schedule_refresh(is, 100);
  }
}
      
void alloc_picture(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;

  vp = &is->pictq[is->pictq_windex];
  if(vp->bmp) {
    // we already have one make another, bigger/smaller
    SDL_FreeYUVOverlay(vp->bmp);
  }
  // Allocate a place to put our YUV image on that screen
  SDL_LockMutex(screen_mutex);
  vp->bmp = SDL_CreateYUVOverlay(is->video_ctx->width,
				 is->video_ctx->height,
				 SDL_YV12_OVERLAY,
				 screen);
  SDL_UnlockMutex(screen_mutex);

  vp->width = is->video_ctx->width;
  vp->height = is->video_ctx->height;
  vp->allocated = 1;

}

int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {

  VideoPicture *vp;
  int dst_pix_fmt;
  AVPicture pict;

  /* wait until we have space for a new pic */
  SDL_LockMutex(is->pictq_mutex);
  while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
	!is->quit) {
    SDL_CondWait(is->pictq_cond, is->pictq_mutex);
  }
  SDL_UnlockMutex(is->pictq_mutex);

  if(is->quit)
    return -1;

  // windex is set to 0 initially
  vp = &is->pictq[is->pictq_windex];

  /* allocate or resize the buffer! */
  if(!vp->bmp ||
     vp->width != is->video_ctx->width ||
     vp->height != is->video_ctx->height) {
    SDL_Event event;

    vp->allocated = 0;
    alloc_picture(is);
    if(is->quit) {
      return -1;
    }
  }

  /* We have a place to put our picture on the queue */

  if(vp->bmp) {

    SDL_LockYUVOverlay(vp->bmp);
    vp->pts = pts;
    
    dst_pix_fmt = PIX_FMT_YUV420P;
    /* point pict at the queue */

    pict.data[0] = vp->bmp->pixels[0];
    pict.data[1] = vp->bmp->pixels[2];
    pict.data[2] = vp->bmp->pixels[1];
    
    pict.linesize[0] = vp->bmp->pitches[0];
    pict.linesize[1] = vp->bmp->pitches[2];
    pict.linesize[2] = vp->bmp->pitches[1];
    
    // Convert the image into YUV format that SDL uses
    sws_scale(is->sws_ctx, (uint8_t const * const *)pFrame->data,
	      pFrame->linesize, 0, is->video_ctx->height,
	      pict.data, pict.linesize);
    
    SDL_UnlockYUVOverlay(vp->bmp);
    /* now we inform our display thread that we have a pic ready */
    if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
      is->pictq_windex = 0;
    }
    SDL_LockMutex(is->pictq_mutex);
    is->pictq_size++;
    SDL_UnlockMutex(is->pictq_mutex);
  }
  return 0;
}

double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

  double frame_delay;

  if(pts != 0) {
    /* if we have pts, set video clock to it */
    is->video_clock = pts;
  } else {
    /* if we aren't given a pts, set it to the clock */
    pts = is->video_clock;
  }
  /* update the video clock */
  frame_delay = av_q2d(is->video_ctx->time_base);
  /* if we are repeating a frame, adjust clock accordingly */
  frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
  is->video_clock += frame_delay;
  return pts;
}

int video_thread(void *arg) {
  VideoState *is = (VideoState *)arg;
  AVPacket pkt1, *packet = &pkt1;
  int frameFinished;
  AVFrame *pFrame;
  double pts;

  pFrame = av_frame_alloc();

  for(;;) {
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
      // means we quit getting packets
      break;
    }
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
      // means we quit getting packets
      break;
    }
    pts = 0;

    // Decode video frame
    avcodec_decode_video2(is->video_ctx, pFrame, &frameFinished, packet);

    if((pts = av_frame_get_best_effort_timestamp(pFrame)) == AV_NOPTS_VALUE) {
    } else {
      pts = 0;
    }
    pts *= av_q2d(is->video_st->time_base);

    // Did we get a video frame?
    if(frameFinished) {
      pts = synchronize_video(is, pFrame, pts);
      if(queue_picture(is, pFrame, pts) < 0) {
	break;
      }
    }
    av_free_packet(packet);
  }
  av_frame_free(&pFrame);
  return 0;
}

int stream_component_open(VideoState *is, int stream_index) {

  AVFormatContext *pFormatCtx = is->pFormatCtx;
  AVCodecContext *codecCtx = NULL;
  AVCodec *codec = NULL;
  SDL_AudioSpec wanted_spec, spec;

  if(stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
    return -1;
  }

  codec = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codec->codec_id);
  if(!codec) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  codecCtx = avcodec_alloc_context3(codec);
  if(avcodec_copy_context(codecCtx, pFormatCtx->streams[stream_index]->codec) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }


  if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
    // Set audio settings from codec info
    wanted_spec.freq = codecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = codecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = is;
    
    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }
    is->audio_hw_buf_size = spec.size;
  }
  if(avcodec_open2(codecCtx, codec, NULL) < 0) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  switch(codecCtx->codec_type) {
  case AVMEDIA_TYPE_AUDIO:
    is->audioStream = stream_index;
    is->audio_st = pFormatCtx->streams[stream_index];
    is->audio_ctx = codecCtx;
    is->audio_buf_size = 0;
    is->audio_buf_index = 0;
    memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
    packet_queue_init(&is->audioq);
    SDL_PauseAudio(0);
    break;
  case AVMEDIA_TYPE_VIDEO:
    is->videoStream = stream_index;
    is->video_st = pFormatCtx->streams[stream_index];
    is->video_ctx = codecCtx;

    is->frame_timer = (double)av_gettime() / 1000000.0;
    is->frame_last_delay = 40e-3;
    is->video_current_pts_time = av_gettime();

    packet_queue_init(&is->videoq);
    is->video_tid = SDL_CreateThread(video_thread, is);
    is->sws_ctx = sws_getContext(is->video_ctx->width, is->video_ctx->height,
				 is->video_ctx->pix_fmt, is->video_ctx->width,
				 is->video_ctx->height, PIX_FMT_YUV420P,
				 SWS_BILINEAR, NULL, NULL, NULL
				 );
    break;
  default:
    break;
  }
}

int decode_thread(void *arg) {

  VideoState *is = (VideoState *)arg;
  AVFormatContext *pFormatCtx;
  AVPacket pkt1, *packet = &pkt1;

  int video_index = -1;
  int audio_index = -1;
  int i;

  is->videoStream=-1;
  is->audioStream=-1;

  global_video_state = is;

  // Open video file
  if(avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)!=0)
    return -1; // Couldn't open file

  is->pFormatCtx = pFormatCtx;
  
  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; // Couldn't find stream information
  
  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, is->filename, 0);
  
  // Find the first video stream

  for(i=0; i<pFormatCtx->nb_streams; i++) {
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO &&
       video_index < 0) {
      video_index=i;
    }
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
       audio_index < 0) {
      audio_index=i;
    }
  }
  if(audio_index >= 0) {
    stream_component_open(is, audio_index);
  }
  if(video_index >= 0) {
    stream_component_open(is, video_index);
  }   

  if(is->videoStream < 0 || is->audioStream < 0) {
    fprintf(stderr, "%s: could not open codecs\n", is->filename);
    goto fail;
  }

  // main decode loop

  for(;;) {
    if(is->quit) {
      break;
    }
    // seek stuff goes here
    if(is->audioq.size > MAX_AUDIOQ_SIZE ||
       is->videoq.size > MAX_VIDEOQ_SIZE) {
      SDL_Delay(10);
      continue;
    }
    if(av_read_frame(is->pFormatCtx, packet) < 0) {
      if(is->pFormatCtx->pb->error == 0) {
	SDL_Delay(100); /* no error; wait for user input */
	continue;
      } else {
	break;
      }
    }
    // Is this a packet from the video stream?
    if(packet->stream_index == is->videoStream) {
      packet_queue_put(&is->videoq, packet);
    } else if(packet->stream_index == is->audioStream) {
      packet_queue_put(&is->audioq, packet);
    } else {
      av_free_packet(packet);
    }
  }
  /* all done - wait for it */
  while(!is->quit) {
    SDL_Delay(100);
  }

 fail:
  if(1){
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    event.user.data1 = is;
    SDL_PushEvent(&event);
  }
  return 0;
}

int main(int argc, char *argv[]) {

  SDL_Event       event;

  VideoState      *is;

  is = av_mallocz(sizeof(VideoState));

  if(argc < 2) {
    fprintf(stderr, "Usage: test <file>\n");
    exit(1);
  }
  // Register all formats and codecs
  av_register_all();
  
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Make a screen to put our video
#ifndef __DARWIN__
        screen = SDL_SetVideoMode(640, 480, 0, 0);
#else
        screen = SDL_SetVideoMode(640, 480, 24, 0);
#endif
  if(!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }

  screen_mutex = SDL_CreateMutex();

  av_strlcpy(is->filename, argv[1], sizeof(is->filename));

  is->pictq_mutex = SDL_CreateMutex();
  is->pictq_cond = SDL_CreateCond();

  schedule_refresh(is, 40);

  is->av_sync_type = DEFAULT_AV_SYNC_TYPE;
  is->parse_tid = SDL_CreateThread(decode_thread, is);
  if(!is->parse_tid) {
    av_free(is);
    return -1;
  }
  for(;;) {

    SDL_WaitEvent(&event);
    switch(event.type) {
    case FF_QUIT_EVENT:
    case SDL_QUIT:
      is->quit = 1;
      SDL_Quit();
      return 0;
      break;
    case FF_REFRESH_EVENT:
      video_refresh_timer(event.user.data1);
      break;
    default:
      break;
    }
  }
  return 0;

}

```

## An ffmpeg and SDL Tutorial

Page 1 2 3 4 5 6 7 End Prev Home Next &nbsp_place_holder;

Text version

## Tutorial 07: Seeking

Code: tutorial07.c

### Handling the seek command

Now we're going to add some seeking capabilities to our player, because it's
really annoying when you can't rewind a movie. Plus, this will show you how
easy the `av_seek_frame` function is to use.

We're going to make the left and right arrows go back and forth in the movie
by a little and the up and down arrows a lot, where "a little" is 10 seconds,
and "a lot" is 60 seconds. So we need to set up our main loop so it catches
the keystrokes. However, when we do get a keystroke, we can't call
`av_seek_frame` directly. We have to do that in our main decode loop, the
`decode_thread` loop. So instead, we're going to add some values to the big
struct that will contain the new position to seek to and some seeking flags:

    
    
      int             seek_req;
      int             seek_flags;
      int64_t         seek_pos;
    

Now we need to set up our main loop to catch the key presses:

    
    
      for(;;) {
        double incr, pos;
    
        SDL_WaitEvent(&event;);
        switch(event.type) {
        case SDL_KEYDOWN:
          switch(event.key.keysym.sym) {
          case SDLK_LEFT:
    	incr = -10.0;
    	goto do_seek;
          case SDLK_RIGHT:
    	incr = 10.0;
    	goto do_seek;
          case SDLK_UP:
    	incr = 60.0;
    	goto do_seek;
          case SDLK_DOWN:
    	incr = -60.0;
    	goto do_seek;
          do_seek:
    	if(global_video_state) {
    	  pos = get_master_clock(global_video_state);
    	  pos += incr;
    	  stream_seek(global_video_state, 
                          (int64_t)(pos * AV_TIME_BASE), incr);
    	}
    	break;
          default:
    	break;
          }
          break;
    

To detect keypresses, we first look and see if we get an `SDL_KEYDOWN` event.
Then we check and see which key got hit using `event.key.keysym.sym`. Once we
know which way we want to seek, we calculate the new time by adding the
increment to the value from our new `get_master_clock` function. Then we call
a `stream_seek` function to set the seek_pos, etc., values. We convert our new
time to avcodec's internal timestamp unit. Recall that timestamps in streams
are measured in frames rather than seconds, with the formula `seconds = frames
* time_base (fps)`. avcodec defaults to a value of 1,000,000 fps (so a `pos`
of 2 seconds will be timestamp of 2000000). We'll see why we need to convert
this value later.

Here's our `stream_seek` function. Notice we set a flag if we are going
backwards:

    
    
    void stream_seek(VideoState *is, int64_t pos, int rel) {
    
      if(!is->seek_req) {
        is->seek_pos = pos;
        is->seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;
        is->seek_req = 1;
      }
    }
    

Now let's go over to our `decode_thread` where we will actually perform our
seek. You'll notice in the source files that we've marked an area "seek stuff
goes here". Well, we're going to put it there now.

Seeking centers around the `av_seek_frame` function. This function takes a
format context, a stream, a timestamp, and a set of flags as an argument. The
function will seek to the timestamp you give it. The unit of the timestamp is
the `time_base` of the stream you pass the function. _However_, you do not
have to pass it a stream (indicated by passing a value of -1). If you do that,
the `time_base` will be in avcodec's internal timestamp unit, or 1000000fps.
This is why we multiplied our position by `AV_TIME_BASE` when we set
`seek_pos`.

However, sometimes you can (rarely) run into problems with some files if you
pass `av_seek_frame` -1 for a stream, so we're going to pick the first stream
in our file and pass it to `av_seek_frame`. Don't forget we have to rescale
our timestamp to be in the new unit too.

    
    
    if(is->seek_req) {
      int stream_index= -1;
      int64_t seek_target = is->seek_pos;
    
      if     (is->videoStream >= 0) stream_index = is->videoStream;
      else if(is->audioStream >= 0) stream_index = is->audioStream;
    
      if(stream_index>=0){
        seek_target= av_rescale_q(seek_target, AV_TIME_BASE_Q,
                          pFormatCtx->streams[stream_index]->time_base);
      }
      if(av_seek_frame(is->pFormatCtx, stream_index, 
                        seek_target, is->seek_flags) < 0) {
        fprintf(stderr, "%s: error while seeking\n",
                is->pFormatCtx->filename);
      } else {
         /* handle packet queues... more later... */
    
    

`av_rescale_q(a,b,c)` is a function that will rescale a timestamp from one
base to another. It basically computes `a*b/c` but this function is required
because that calculation could overflow. `AV_TIME_BASE_Q` is the fractional
version of `AV_TIME_BASE`. They're quite different: `AV_TIME_BASE *
time_in_seconds = avcodec_timestamp` and `AV_TIME_BASE_Q * avcodec_timestamp =
time_in_seconds` (but note that `AV_TIME_BASE_Q` is actually an `AVRational`
object, so you have to use special q functions in avcodec to handle it).

### Flushing our buffers

So we've set our seek correctly, but we aren't finished quite yet. Remember
that we have a queue set up to accumulate packets. Now that we're in a
different place, we have to flush that queue or the movie ain't gonna seek!
Not only that, but avcodec has its own internal buffers that need to be
flushed too by each thread.

To do this, we need to first write a function to clear our packet queue. Then,
we need to have some way of instructing the audio and video thread that they
need to flush avcodec's internal buffers. We can do this by putting a special
packet on the queue after we flush it, and when they detect that special
packet, they'll just flush their buffers.

Let's start with the flush function. It's really quite simple, so I'll just
show you the code:

    
    
    static void packet_queue_flush(PacketQueue *q) {
      AVPacketList *pkt, *pkt1;
    
      SDL_LockMutex(q->mutex);
      for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
        pkt1 = pkt->next;
        av_free_packet(&pkt-;>pkt);
        av_freep(&pkt;);
      }
      q->last_pkt = NULL;
      q->first_pkt = NULL;
      q->nb_packets = 0;
      q->size = 0;
      SDL_UnlockMutex(q->mutex);
    }
    

Now that the queue is flushed, let's put on our "flush packet." But first
we're going to want to define what that is and create it:

    
    
    AVPacket flush_pkt;
    
    main() {
      ...
      av_init_packet(&flush;_pkt);
      flush_pkt.data = "FLUSH";
      ...
    }
    

Now we put this packet on the queue:

    
    
      } else {
        if(is->audioStream >= 0) {
          packet_queue_flush(&is-;>audioq);
          packet_queue_put(&is-;>audioq, &flush;_pkt);
        }
        if(is->videoStream >= 0) {
          packet_queue_flush(&is-;>videoq);
          packet_queue_put(&is-;>videoq, &flush;_pkt);
        }
      }
      is->seek_req = 0;
    }
    

(This code snippet also continues the code snippet above for `decode_thread`.)
We also need to change `packet_queue_put` so that we don't duplicate the
special flush packet:

    
    
    int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    
      AVPacketList *pkt1;
      if(pkt != &flush;_pkt && av_dup_packet(pkt) < 0) {
        return -1;
      }
    

And then in the audio thread and the video thread, we put this call to
`avcodec_flush_buffers` immediately after `packet_queue_get`:

    
    
        if(packet_queue_get(&is-;>audioq, pkt, 1) < 0) {
          return -1;
        }
        if(pkt->data == flush_pkt.data) {
          avcodec_flush_buffers(is->audio_st->codec);
          continue;
        }
    

The above code snippet is exactly the same for the video thread, with "audio"
being replaced by "video".

That's it! We're done! Go ahead and compile your player:

    
    
    gcc -o tutorial07 tutorial07.c -lavutil -lavformat -lavcodec -lswscale -lz -lm \
    `sdl-config --cflags --libs`
    

and enjoy your movie player made in less than 1000 lines of C!

Of course, there's a lot of things we glanced over that we could add.

```c

// tutorial05.c
// A pedagogical video player that really works!
//
// Code based on FFplay, Copyright (c) 2003 Fabrice Bellard, 
// and a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1
// With updates from https://github.com/chelyaev/ffmpeg-tutorial
// Updates tested on:
// LAVC 54.59.100, LAVF 54.29.104, LSWS 2.1.101, SDL 1.2.15
// on GCC 4.7.2 in Debian February 2015
// Use
//
// gcc -o tutorial05 tutorial05.c -lavformat -lavcodec -lswscale -lz -lm `sdl-config --cflags --libs`
// to build (assuming libavformat and libavcodec are correctly installed, 
// and assuming you have sdl-config. Please refer to SDL docs for your installation.)
//
// Run using
// tutorial04 myvideofile.mpg
//
// to play the video stream on your screen.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <assert.h>
#include <math.h>

// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

#define DEFAULT_AV_SYNC_TYPE AV_SYNC_VIDEO_MASTER

typedef struct PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;


typedef struct VideoPicture {
  SDL_Overlay *bmp;
  int width, height; /* source height & width */
  int allocated;
  double pts;
} VideoPicture;

typedef struct VideoState {

  AVFormatContext *pFormatCtx;
  int             videoStream, audioStream;

  int             av_sync_type;
  double          external_clock; /* external clock base */
  int64_t         external_clock_time;
  int             seek_req;
  int             seek_flags;
  int64_t         seek_pos;

  double          audio_clock;
  AVStream        *audio_st;
  AVCodecContext  *audio_ctx;
  PacketQueue     audioq;
  uint8_t         audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
  unsigned int    audio_buf_size;
  unsigned int    audio_buf_index;
  AVFrame         audio_frame;
  AVPacket        audio_pkt;
  uint8_t         *audio_pkt_data;
  int             audio_pkt_size;
  int             audio_hw_buf_size;
  double          audio_diff_cum; /* used for AV difference average computation */
  double          audio_diff_avg_coef;
  double          audio_diff_threshold;
  int             audio_diff_avg_count;
  double          frame_timer;
  double          frame_last_pts;
  double          frame_last_delay;
  double          video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
  double          video_current_pts; ///<current displayed pts (different from video_clock if frame fifos are used)
  int64_t         video_current_pts_time;  ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts
  AVStream        *video_st;
  AVCodecContext  *video_ctx;
  PacketQueue     videoq;
  struct SwsContext *sws_ctx;

  VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int             pictq_size, pictq_rindex, pictq_windex;
  SDL_mutex       *pictq_mutex;
  SDL_cond        *pictq_cond;
  
  SDL_Thread      *parse_tid;
  SDL_Thread      *video_tid;

  char            filename[1024];
  int             quit;
} VideoState;

enum {
  AV_SYNC_AUDIO_MASTER,
  AV_SYNC_VIDEO_MASTER,
  AV_SYNC_EXTERNAL_MASTER,
};

SDL_Surface     *screen;
SDL_mutex       *screen_mutex;

/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;
AVPacket flush_pkt;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

  AVPacketList *pkt1;
  if(pkt != &flush_pkt && av_dup_packet(pkt) < 0) {
    return -1;
  }
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;
  
  SDL_LockMutex(q->mutex);

  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  SDL_CondSignal(q->cond);
  
  SDL_UnlockMutex(q->mutex);
  return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
  AVPacketList *pkt1;
  int ret;

  SDL_LockMutex(q->mutex);
  
  for(;;) {
    
    if(global_video_state->quit) {
      ret = -1;
      break;
    }

    pkt1 = q->first_pkt;
    if (pkt1) {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt)
	q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

static void packet_queue_flush(PacketQueue *q) {
  AVPacketList *pkt, *pkt1;

  SDL_LockMutex(q->mutex);
  for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
    pkt1 = pkt->next;
    av_free_packet(&pkt->pkt);
    av_freep(&pkt);
  }
  q->last_pkt = NULL;
  q->first_pkt = NULL;
  q->nb_packets = 0;
  q->size = 0;
  SDL_UnlockMutex(q->mutex);
}

double get_audio_clock(VideoState *is) {
  double pts;
  int hw_buf_size, bytes_per_sec, n;
  
  pts = is->audio_clock; /* maintained in the audio thread */
  hw_buf_size = is->audio_buf_size - is->audio_buf_index;
  bytes_per_sec = 0;
  n = is->audio_ctx->channels * 2;
  if(is->audio_st) {
    bytes_per_sec = is->audio_ctx->sample_rate * n;
  }
  if(bytes_per_sec) {
    pts -= (double)hw_buf_size / bytes_per_sec;
  }
  return pts;
}
double get_video_clock(VideoState *is) {
  double delta;

  delta = (av_gettime() - is->video_current_pts_time) / 1000000.0;
  return is->video_current_pts + delta;
}
double get_external_clock(VideoState *is) {
  return av_gettime() / 1000000.0;
}

double get_master_clock(VideoState *is) {
  if(is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
    return get_video_clock(is);
  } else if(is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
    return get_audio_clock(is);
  } else {
    return get_external_clock(is);
  }
}


/* Add or subtract samples to get a better sync, return new
   audio buffer size */
int synchronize_audio(VideoState *is, short *samples,
		      int samples_size, double pts) {
  int n;
  double ref_clock;

  n = 2 * is->audio_ctx->channels;
  
  if(is->av_sync_type != AV_SYNC_AUDIO_MASTER) {
    double diff, avg_diff;
    int wanted_size, min_size, max_size /*, nb_samples */;
    
    ref_clock = get_master_clock(is);
    diff = get_audio_clock(is) - ref_clock;

    if(diff < AV_NOSYNC_THRESHOLD) {
      // accumulate the diffs
      is->audio_diff_cum = diff + is->audio_diff_avg_coef
	* is->audio_diff_cum;
      if(is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
	is->audio_diff_avg_count++;
      } else {
	avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);
	if(fabs(avg_diff) >= is->audio_diff_threshold) {
	  wanted_size = samples_size + ((int)(diff * is->audio_ctx->sample_rate) * n);
	  min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100);
	  max_size = samples_size * ((100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100);
	  if(wanted_size < min_size) {
	    wanted_size = min_size;
	  } else if (wanted_size > max_size) {
	    wanted_size = max_size;
	  }
	  if(wanted_size < samples_size) {
	    /* remove samples */
	    samples_size = wanted_size;
	  } else if(wanted_size > samples_size) {
	    uint8_t *samples_end, *q;
	    int nb;

	    /* add samples by copying final sample*/
	    nb = (samples_size - wanted_size);
	    samples_end = (uint8_t *)samples + samples_size - n;
	    q = samples_end + n;
	    while(nb > 0) {
	      memcpy(q, samples_end, n);
	      q += n;
	      nb -= n;
	    }
	    samples_size = wanted_size;
	  }
	}
      }
    } else {
      /* difference is TOO big; reset diff stuff */
      is->audio_diff_avg_count = 0;
      is->audio_diff_cum = 0;
    }
  }
  return samples_size;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr) {

  int len1, data_size = 0;
  AVPacket *pkt = &is->audio_pkt;
  double pts;
  int n;

  for(;;) {
    while(is->audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(is->audio_ctx, &is->audio_frame, &got_frame, pkt);
      if(len1 < 0) {
	/* if error, skip frame */
	is->audio_pkt_size = 0;
	break;
      }
      data_size = 0;
      if(got_frame) {
	data_size = av_samples_get_buffer_size(NULL, 
					       is->audio_ctx->channels,
					       is->audio_frame.nb_samples,
					       is->audio_ctx->sample_fmt,
					       1);
	assert(data_size <= buf_size);
	memcpy(audio_buf, is->audio_frame.data[0], data_size);
      }
      is->audio_pkt_data += len1;
      is->audio_pkt_size -= len1;
      if(data_size <= 0) {
	/* No data yet, get more frames */
	continue;
      }
      pts = is->audio_clock;
      *pts_ptr = pts;
      n = 2 * is->audio_ctx->channels;
      is->audio_clock += (double)data_size /
	(double)(n * is->audio_ctx->sample_rate);
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if(pkt->data)
      av_free_packet(pkt);

    if(is->quit) {
      return -1;
    }
    /* next packet */
    if(packet_queue_get(&is->audioq, pkt, 1) < 0) {
      return -1;
    }
    if(pkt->data == flush_pkt.data) {
      avcodec_flush_buffers(is->audio_ctx);
      continue;
    }
    is->audio_pkt_data = pkt->data;
    is->audio_pkt_size = pkt->size;
    /* if update, update the audio clock w/pts */
    if(pkt->pts != AV_NOPTS_VALUE) {
      is->audio_clock = av_q2d(is->audio_st->time_base)*pkt->pts;
    }
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

  VideoState *is = (VideoState *)userdata;
  int len1, audio_size;
  double pts;

  while(len > 0) {
    if(is->audio_buf_index >= is->audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);
      if(audio_size < 0) {
	/* If error, output silence */
	is->audio_buf_size = 1024;
	memset(is->audio_buf, 0, is->audio_buf_size);
      } else {
	audio_size = synchronize_audio(is, (int16_t *)is->audio_buf,
				       audio_size, pts);
	is->audio_buf_size = audio_size;
      }
      is->audio_buf_index = 0;
    }
    len1 = is->audio_buf_size - is->audio_buf_index;
    if(len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
    len -= len1;
    stream += len1;
    is->audio_buf_index += len1;
  }
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay) {
  SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_display(VideoState *is) {

  SDL_Rect rect;
  VideoPicture *vp;
  float aspect_ratio;
  int w, h, x, y;
  int i;

  vp = &is->pictq[is->pictq_rindex];
  if(vp->bmp) {
    if(is->video_ctx->sample_aspect_ratio.num == 0) {
      aspect_ratio = 0;
    } else {
      aspect_ratio = av_q2d(is->video_ctx->sample_aspect_ratio) *
	is->video_ctx->width / is->video_ctx->height;
    }
    if(aspect_ratio <= 0.0) {
      aspect_ratio = (float)is->video_ctx->width /
	(float)is->video_ctx->height;
    }
    h = screen->h;
    w = ((int)rint(h * aspect_ratio)) & -3;
    if(w > screen->w) {
      w = screen->w;
      h = ((int)rint(w / aspect_ratio)) & -3;
    }
    x = (screen->w - w) / 2;
    y = (screen->h - h) / 2;
    
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_LockMutex(screen_mutex);
    SDL_DisplayYUVOverlay(vp->bmp, &rect);
    SDL_UnlockMutex(screen_mutex);
  }
}

void video_refresh_timer(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;
  double actual_delay, delay, sync_threshold, ref_clock, diff;
  
  if(is->video_st) {
    if(is->pictq_size == 0) {
      schedule_refresh(is, 1);
    } else {
      vp = &is->pictq[is->pictq_rindex];
      
      is->video_current_pts = vp->pts;
      is->video_current_pts_time = av_gettime();
      delay = vp->pts - is->frame_last_pts; /* the pts from last time */
      if(delay <= 0 || delay >= 1.0) {
	/* if incorrect delay, use previous one */
	delay = is->frame_last_delay;
      }
      /* save for next time */
      is->frame_last_delay = delay;
      is->frame_last_pts = vp->pts;



      /* update delay to sync to audio if not master source */
      if(is->av_sync_type != AV_SYNC_VIDEO_MASTER) {
	ref_clock = get_master_clock(is);
	diff = vp->pts - ref_clock;
	
	/* Skip or repeat the frame. Take delay into account
	   FFPlay still doesn't "know if this is the best guess." */
	sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
	if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
	  if(diff <= -sync_threshold) {
	    delay = 0;
	  } else if(diff >= sync_threshold) {
	    delay = 2 * delay;
	  }
	}
      }
      is->frame_timer += delay;
      /* computer the REAL delay */
      actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
      if(actual_delay < 0.010) {
	/* Really it should skip the picture instead */
	actual_delay = 0.010;
      }
      schedule_refresh(is, (int)(actual_delay * 1000 + 0.5));
      
      /* show the picture! */
      video_display(is);
      
      /* update queue for next picture! */
      if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
	is->pictq_rindex = 0;
      }
      SDL_LockMutex(is->pictq_mutex);
      is->pictq_size--;
      SDL_CondSignal(is->pictq_cond);
      SDL_UnlockMutex(is->pictq_mutex);
    }
  } else {
    schedule_refresh(is, 100);
  }
}
      
void alloc_picture(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;

  vp = &is->pictq[is->pictq_windex];
  if(vp->bmp) {
    // we already have one make another, bigger/smaller
    SDL_FreeYUVOverlay(vp->bmp);
  }
  // Allocate a place to put our YUV image on that screen
  SDL_LockMutex(screen_mutex);
  vp->bmp = SDL_CreateYUVOverlay(is->video_ctx->width,
				 is->video_ctx->height,
				 SDL_YV12_OVERLAY,
				 screen);
  SDL_UnlockMutex(screen_mutex);

  vp->width = is->video_ctx->width;
  vp->height = is->video_ctx->height;
  vp->allocated = 1;

}

int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {

  VideoPicture *vp;
  int dst_pix_fmt;
  AVPicture pict;

  /* wait until we have space for a new pic */
  SDL_LockMutex(is->pictq_mutex);
  while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
	!is->quit) {
    SDL_CondWait(is->pictq_cond, is->pictq_mutex);
  }
  SDL_UnlockMutex(is->pictq_mutex);

  if(is->quit)
    return -1;

  // windex is set to 0 initially
  vp = &is->pictq[is->pictq_windex];

  /* allocate or resize the buffer! */
  if(!vp->bmp ||
     vp->width != is->video_ctx->width ||
     vp->height != is->video_ctx->height) {
    SDL_Event event;

    vp->allocated = 0;
    alloc_picture(is);
    if(is->quit) {
      return -1;
    }
  }
  /* We have a place to put our picture on the queue */

  if(vp->bmp) {

    SDL_LockYUVOverlay(vp->bmp);
    vp->pts = pts;
    
    dst_pix_fmt = PIX_FMT_YUV420P;
    /* point pict at the queue */

    pict.data[0] = vp->bmp->pixels[0];
    pict.data[1] = vp->bmp->pixels[2];
    pict.data[2] = vp->bmp->pixels[1];
    
    pict.linesize[0] = vp->bmp->pitches[0];
    pict.linesize[1] = vp->bmp->pitches[2];
    pict.linesize[2] = vp->bmp->pitches[1];
    
    // Convert the image into YUV format that SDL uses
    sws_scale(is->sws_ctx, (uint8_t const * const *)pFrame->data,
	      pFrame->linesize, 0, is->video_ctx->height,
	      pict.data, pict.linesize);
    
    SDL_UnlockYUVOverlay(vp->bmp);
    /* now we inform our display thread that we have a pic ready */
    if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
      is->pictq_windex = 0;
    }
    SDL_LockMutex(is->pictq_mutex);
    is->pictq_size++;
    SDL_UnlockMutex(is->pictq_mutex);
  }
  return 0;
}

double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

  double frame_delay;

  if(pts != 0) {
    /* if we have pts, set video clock to it */
    is->video_clock = pts;
  } else {
    /* if we aren't given a pts, set it to the clock */
    pts = is->video_clock;
  }
  /* update the video clock */
  frame_delay = av_q2d(is->video_ctx->time_base);
  /* if we are repeating a frame, adjust clock accordingly */
  frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
  is->video_clock += frame_delay;
  return pts;
}

int video_thread(void *arg) {
  VideoState *is = (VideoState *)arg;
  AVPacket pkt1, *packet = &pkt1;
  int frameFinished;
  AVFrame *pFrame;
  double pts;

  pFrame = av_frame_alloc();

  for(;;) {
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
      // means we quit getting packets
      break;
    }
    if(packet_queue_get(&is->videoq, packet, 1) < 0) {
      // means we quit getting packets
      break;
    }
    pts = 0;

    // Decode video frame
    avcodec_decode_video2(is->video_ctx, pFrame, &frameFinished, packet);

    if((pts = av_frame_get_best_effort_timestamp(pFrame)) == AV_NOPTS_VALUE) {
      pts = av_frame_get_best_effort_timestamp(pFrame);
    } else {
      pts = 0;
    }
    pts *= av_q2d(is->video_st->time_base);

    // Did we get a video frame?
    if(frameFinished) {
      pts = synchronize_video(is, pFrame, pts);
      if(queue_picture(is, pFrame, pts) < 0) {
	break;
      }
    }
    av_free_packet(packet);
  }
  av_frame_free(&pFrame);
  return 0;
}

int stream_component_open(VideoState *is, int stream_index) {

  AVFormatContext *pFormatCtx = is->pFormatCtx;
  AVCodecContext *codecCtx = NULL;
  AVCodec *codec = NULL;
  SDL_AudioSpec wanted_spec, spec;

  if(stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
    return -1;
  }

  codec = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codec->codec_id);
  if(!codec) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  codecCtx = avcodec_alloc_context3(codec);
  if(avcodec_copy_context(codecCtx, pFormatCtx->streams[stream_index]->codec) != 0) {
    fprintf(stderr, "Couldn't copy codec context");
    return -1; // Error copying codec context
  }


  if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
    // Set audio settings from codec info
    wanted_spec.freq = codecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = codecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = is;
    
    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
      fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
      return -1;
    }
    is->audio_hw_buf_size = spec.size;
  }
  if(avcodec_open2(codecCtx, codec, NULL) < 0) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1;
  }

  switch(codecCtx->codec_type) {
  case AVMEDIA_TYPE_AUDIO:
    is->audioStream = stream_index;
    is->audio_st = pFormatCtx->streams[stream_index];
    is->audio_ctx = codecCtx;
    is->audio_buf_size = 0;
    is->audio_buf_index = 0;
    memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
    packet_queue_init(&is->audioq);
    SDL_PauseAudio(0);
    break;
  case AVMEDIA_TYPE_VIDEO:
    is->videoStream = stream_index;
    is->video_st = pFormatCtx->streams[stream_index];
    is->video_ctx = codecCtx;

    is->frame_timer = (double)av_gettime() / 1000000.0;
    is->frame_last_delay = 40e-3;
    is->video_current_pts_time = av_gettime();

    packet_queue_init(&is->videoq);
    is->video_tid = SDL_CreateThread(video_thread, is);
    is->sws_ctx = sws_getContext(is->video_ctx->width, is->video_ctx->height,
				 is->video_ctx->pix_fmt, is->video_ctx->width,
				 is->video_ctx->height, PIX_FMT_YUV420P,
				 SWS_BILINEAR, NULL, NULL, NULL
				 );
    break;
  default:
    break;
  }
}

int decode_thread(void *arg) {

  VideoState *is = (VideoState *)arg;
  AVFormatContext *pFormatCtx;
  AVPacket pkt1, *packet = &pkt1;

  int video_index = -1;
  int audio_index = -1;
  int i;

  is->videoStream=-1;
  is->audioStream=-1;

  global_video_state = is;

  // Open video file
  if(avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)!=0)
    return -1; // Couldn't open file

  is->pFormatCtx = pFormatCtx;
  
  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0)
    return -1; // Couldn't find stream information
  
  // Dump information about file onto standard error
  av_dump_format(pFormatCtx, 0, is->filename, 0);
  
  // Find the first video stream

  for(i=0; i<pFormatCtx->nb_streams; i++) {
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO &&
       video_index < 0) {
      video_index=i;
    }
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
       audio_index < 0) {
      audio_index=i;
    }
  }
  if(audio_index >= 0) {
    stream_component_open(is, audio_index);
  }
  if(video_index >= 0) {
    stream_component_open(is, video_index);
  }   

  if(is->videoStream < 0 || is->audioStream < 0) {
    fprintf(stderr, "%s: could not open codecs\n", is->filename);
    goto fail;
  }

  // main decode loop

  for(;;) {
    if(is->quit) {
      break;
    }
    // seek stuff goes here
    if(is->seek_req) {
      int stream_index= -1;
      int64_t seek_target = is->seek_pos;

      if     (is->videoStream >= 0) stream_index = is->videoStream;
      else if(is->audioStream >= 0) stream_index = is->audioStream;

      if(stream_index>=0){
	seek_target= av_rescale_q(seek_target, AV_TIME_BASE_Q,
				  pFormatCtx->streams[stream_index]->time_base);
      }
      if(av_seek_frame(is->pFormatCtx, stream_index, 
		       seek_target, is->seek_flags) < 0) {
	fprintf(stderr, "%s: error while seeking\n",
		is->pFormatCtx->filename);
      } else {

	if(is->audioStream >= 0) {
	  packet_queue_flush(&is->audioq);
	  packet_queue_put(&is->audioq, &flush_pkt);
	}
	if(is->videoStream >= 0) {
	  packet_queue_flush(&is->videoq);
	  packet_queue_put(&is->videoq, &flush_pkt);
	}
      }
      is->seek_req = 0;
    }

    if(is->audioq.size > MAX_AUDIOQ_SIZE ||
       is->videoq.size > MAX_VIDEOQ_SIZE) {
      SDL_Delay(10);
      continue;
    }
    if(av_read_frame(is->pFormatCtx, packet) < 0) {
      if(is->pFormatCtx->pb->error == 0) {
	SDL_Delay(100); /* no error; wait for user input */
	continue;
      } else {
	break;
      }
    }
    // Is this a packet from the video stream?
    if(packet->stream_index == is->videoStream) {
      packet_queue_put(&is->videoq, packet);
    } else if(packet->stream_index == is->audioStream) {
      packet_queue_put(&is->audioq, packet);
    } else {
      av_free_packet(packet);
    }
  }
  /* all done - wait for it */
  while(!is->quit) {
    SDL_Delay(100);
  }

 fail:
  if(1){
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    event.user.data1 = is;
    SDL_PushEvent(&event);
  }
  return 0;
}

void stream_seek(VideoState *is, int64_t pos, int rel) {

  if(!is->seek_req) {
    is->seek_pos = pos;
    is->seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;
    is->seek_req = 1;
  }
}

int main(int argc, char *argv[]) {

  SDL_Event       event;

  VideoState      *is;

  is = av_mallocz(sizeof(VideoState));

  if(argc < 2) {
    fprintf(stderr, "Usage: test <file>\n");
    exit(1);
  }
  // Register all formats and codecs
  av_register_all();
  
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  // Make a screen to put our video
#ifndef __DARWIN__
        screen = SDL_SetVideoMode(640, 480, 0, 0);
#else
        screen = SDL_SetVideoMode(640, 480, 24, 0);
#endif
  if(!screen) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }

  screen_mutex = SDL_CreateMutex();

  av_strlcpy(is->filename, argv[1], sizeof(is->filename));

  is->pictq_mutex = SDL_CreateMutex();
  is->pictq_cond = SDL_CreateCond();

  schedule_refresh(is, 40);

  is->av_sync_type = DEFAULT_AV_SYNC_TYPE;
  is->parse_tid = SDL_CreateThread(decode_thread, is);
  if(!is->parse_tid) {
    av_free(is);
    return -1;
  }

  av_init_packet(&flush_pkt);
  flush_pkt.data = "FLUSH";

  for(;;) {
    double incr, pos;
    SDL_WaitEvent(&event);
    switch(event.type) {
    case SDL_KEYDOWN:
      switch(event.key.keysym.sym) {
      case SDLK_LEFT:
	incr = -10.0;
	goto do_seek;
      case SDLK_RIGHT:
	incr = 10.0;
	goto do_seek;
      case SDLK_UP:
	incr = 60.0;
	goto do_seek;
      case SDLK_DOWN:
	incr = -60.0;
	goto do_seek;
      do_seek:
	if(global_video_state) {
	  pos = get_master_clock(global_video_state);
	  pos += incr;
	  stream_seek(global_video_state, (int64_t)(pos * AV_TIME_BASE), incr);
	}
	break;
      default:
	break;
      }
      break;
    case FF_QUIT_EVENT:
    case SDL_QUIT:
      is->quit = 1;
      /*
       * If the video has finished playing, then both the picture and
       * audio queues are waiting for more data.  Make them stop
       * waiting and terminate normally.
       */
      SDL_CondSignal(is->audioq.cond);
      SDL_CondSignal(is->videoq.cond);
      SDL_Quit();
      return 0;
      break;
    case FF_REFRESH_EVENT:
      video_refresh_timer(event.user.data1);
      break;
    default:
      break;
    }
  }
  return 0;

}

```