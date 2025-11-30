/**
 * FFmpeg C API: Slow Motion Transcoder (No Custom Structs)
 * 
 * Compile:
 * gcc slow_transcode.c -o slow_transcode -lavformat -lavcodec -lavutil
 */

 #include <stdio.h>
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 #include <libavutil/timestamp.h>
 #include <libavutil/opt.h>
 
 #define SLOW_FACTOR 6
 #define OUT_FILENAME "output_slow.mp4"
 
 /**
  * Encodes a frame and writes the resulting packet(s) to the output file.
  * Takes explicit pointers instead of a custom struct.
  */
 int encode_and_write(AVCodecContext* enc_ctx, 
                      AVFormatContext* out_fmt_ctx, 
                      AVStream* out_stream, 
                      AVFrame* frame) {
   int ret;
   
   // 1. Send raw frame to encoder
   ret = avcodec_send_frame(enc_ctx, frame);
   if (ret < 0) return ret;
 
   AVPacket* pkt = av_packet_alloc();
 
   // 2. Receive encoded packets
   while (ret >= 0) {
     ret = avcodec_receive_packet(enc_ctx, pkt);
     if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
       av_packet_free(&pkt);
       return 0; // Not an error, just need more data or done
     } else if (ret < 0) {
       av_packet_free(&pkt);
       return ret; // Real error
     }
 
     // 3. Rescale timestamps: Encoder Timebase -> Muxer Timebase
     av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
     pkt->stream_index = out_stream->index;
 
     // 4. Write to file
     ret = av_interleaved_write_frame(out_fmt_ctx, pkt);
     av_packet_unref(pkt);
     
     if (ret < 0) {
       av_packet_free(&pkt);
       return ret;
     }
   }
   
   av_packet_free(&pkt);
   return 0;
 }
 
 int main(int argc, char* argv[]) {
   const char* in_filename = "../../data/V3.10.mp4";
 
   // --------------------------------------------------------------
   // 1. INPUT SETUP
   // --------------------------------------------------------------
   AVFormatContext* in_fmt_ctx = NULL;
   if (avformat_open_input(&in_fmt_ctx, in_filename, NULL, NULL) < 0) {
     fprintf(stderr, "Cannot open input file\n");
     return 1;
   }
   if (avformat_find_stream_info(in_fmt_ctx, NULL) < 0) return 1;
 
   int video_idx = av_find_best_stream(in_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
   AVStream* in_stream = in_fmt_ctx->streams[video_idx];
   
   // Decoder Setup
   const AVCodec* dec = avcodec_find_decoder(in_stream->codecpar->codec_id);
   AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
   avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
   if (avcodec_open2(dec_ctx, dec, NULL) < 0) return 1;
 
   // --------------------------------------------------------------
   // 2. OUTPUT SETUP
   // --------------------------------------------------------------
   AVFormatContext* out_fmt_ctx = NULL;
   avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, OUT_FILENAME);
   
   // Find H.264 Encoder
   const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
   AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
   
   // Configure Encoder
   // Note: We assume the input is YUV420P. If not, sws_scale is needed.
   enc_ctx->height = dec_ctx->height;
   enc_ctx->width = dec_ctx->width;
   enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
   enc_ctx->time_base = (AVRational){1, 30}; // Encoder works at 30fps base
   enc_ctx->framerate = (AVRational){30, 1}; // Metadata framerate
 
   // Try to adopt input framerate if valid
   if (in_stream->avg_frame_rate.num > 0) {
     enc_ctx->framerate = in_stream->avg_frame_rate;
     enc_ctx->time_base = av_inv_q(in_stream->avg_frame_rate);
   }
   
   // H.264 specifics
   if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
     enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
 
   if (avcodec_open2(enc_ctx, encoder, NULL) < 0) {
     fprintf(stderr, "Cannot open output encoder\n");
     return 1;
   }
 
   // Add Stream to Output File
   AVStream* out_stream = avformat_new_stream(out_fmt_ctx, NULL);
   avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
   out_stream->time_base = enc_ctx->time_base;
 
   // Open output file on disk
   if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
     if (avio_open(&out_fmt_ctx->pb, OUT_FILENAME, AVIO_FLAG_WRITE) < 0) return 1;
   }
 
   if (avformat_write_header(out_fmt_ctx, NULL) < 0) return 1;
 
   // --------------------------------------------------------------
   // 3. PROCESS LOOP
   // --------------------------------------------------------------
   AVPacket* pkt = av_packet_alloc();
   AVFrame* frame = av_frame_alloc();
   int64_t next_pts = 0; // Manual PTS counter for the output
 
   printf("Transcoding... (Slow Factor: %d)\n", SLOW_FACTOR);
 
   while (av_read_frame(in_fmt_ctx, pkt) >= 0) {
     if (pkt->stream_index == video_idx) {
       
       // Send to Decoder
       if (avcodec_send_packet(dec_ctx, pkt) == 0) {
         while (avcodec_receive_frame(dec_ctx, frame) == 0) {
           
           // Ensure we can modify the frame
           av_frame_make_writable(frame);
 
           // === FIX FOR PTS < DTS ERROR ===
           // The frame coming from the decoder has the original video's DTS.
           // Since we are creating a NEW timeline starting at 0, that old DTS 
           // (which might be 130000) will be larger than our new PTS (0), causing an error.
           // We reset these fields so the encoder generates fresh ones.
           frame->pict_type = AV_PICTURE_TYPE_NONE;
           frame->pkt_dts = AV_NOPTS_VALUE; 
 
           // Duplicate Frame Loop
           for (int i = 0; i < SLOW_FACTOR; i++) {
             
             // Assign sequential PTS based on Encoder Timebase
             frame->pts = next_pts++;
 
             // Encode
             if (encode_and_write(enc_ctx, out_fmt_ctx, out_stream, frame) < 0) {
               fprintf(stderr, "Error during encoding\n");
               goto end;
             }
           }
         }
       }
     }
     av_packet_unref(pkt);
   }
 
   // --------------------------------------------------------------
   // 4. FLUSH DECODER
   // --------------------------------------------------------------
   avcodec_send_packet(dec_ctx, NULL);
   while (avcodec_receive_frame(dec_ctx, frame) == 0) {
     av_frame_make_writable(frame);
     frame->pict_type = AV_PICTURE_TYPE_NONE;
     frame->pkt_dts = AV_NOPTS_VALUE;
 
     for (int i = 0; i < SLOW_FACTOR; i++) {
       frame->pts = next_pts++;
       encode_and_write(enc_ctx, out_fmt_ctx, out_stream, frame);
     }
   }
 
   // --------------------------------------------------------------
   // 5. FLUSH ENCODER & CLEANUP
   // --------------------------------------------------------------
   encode_and_write(enc_ctx, out_fmt_ctx, out_stream, NULL);
   av_write_trailer(out_fmt_ctx);
 
   printf("Done. File saved to %s\n", OUT_FILENAME);
 
 end:
   av_packet_free(&pkt);
   av_frame_free(&frame);
   if (in_fmt_ctx) avformat_close_input(&in_fmt_ctx);
   if (out_fmt_ctx && !(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
     avio_closep(&out_fmt_ctx->pb);
   if (out_fmt_ctx) avformat_free_context(out_fmt_ctx);
   if (dec_ctx) avcodec_free_context(&dec_ctx);
   if (enc_ctx) avcodec_free_context(&enc_ctx);
 
   return 0;
 }