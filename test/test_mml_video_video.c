#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

#define IN_FILE "/Users/christian/Downloads/test.mp4"
#define OUT_FILE "/Users/christian/Downloads/test_out.mp4"

#define FILTER_DESC "drawbox=x=100:y=100:w=200:h=200:color=red@0.5"

int main(int argc, char *argv[]) {
  AVFormatContext *input_fmt_ctx = NULL, *output_fmt_ctx = NULL;
  AVCodecContext *dec_ctx = NULL, *enc_ctx = NULL;
  AVFilterGraph *filter_graph = NULL;
  AVFilterContext *buffersrc_ctx = NULL, *buffersink_ctx = NULL;
  const AVFilter *buffersrc, *buffersink;
  AVFilterInOut *outputs, *inputs;
  int video_stream_index, ret;

  AVPacket *packet = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  AVFrame *filt_frame = av_frame_alloc();

  const char *input_file = IN_FILE;

  avformat_open_input(&input_fmt_ctx, input_file, NULL, NULL);
  avformat_find_stream_info(input_fmt_ctx, NULL);
  video_stream_index = av_find_best_stream(input_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVStream *in_stream = input_fmt_ctx->streams[video_stream_index];

  const AVCodec *decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
  dec_ctx = avcodec_alloc_context3(decoder);
  avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
  avcodec_open2(dec_ctx, decoder, NULL);

  // Prepare output format context
  avformat_alloc_output_context2(&output_fmt_ctx, NULL, NULL, OUT_FILE);
  const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVStream *out_stream = avformat_new_stream(output_fmt_ctx, encoder);
  enc_ctx = avcodec_alloc_context3(encoder);
  enc_ctx->height = dec_ctx->height;
  enc_ctx->width = dec_ctx->width;
  enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = (AVRational){1, 25};
  avcodec_open2(enc_ctx, encoder, NULL);
  avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);

  if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_open(&output_fmt_ctx->pb, OUT_FILE, AVIO_FLAG_WRITE);
  avformat_write_header(output_fmt_ctx, NULL);

  // Initialize filtering
  char args[512];
  AVRational time_base = in_stream->time_base;
  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
           time_base.num, time_base.den,
           dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

  buffersrc = avfilter_get_by_name("buffer");
  buffersink = avfilter_get_by_name("buffersink");
  outputs = avfilter_inout_alloc();
  inputs = avfilter_inout_alloc();
  filter_graph = avfilter_graph_alloc();

  avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
  avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
  av_opt_set_bin(buffersink_ctx, "pix_fmts", (uint8_t *)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt), AV_OPT_SEARCH_CHILDREN);

  outputs->name = av_strdup("in");
  outputs->filter_ctx = buffersrc_ctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;
  inputs->name = av_strdup("out");
  inputs->filter_ctx = buffersink_ctx;
  inputs->pad_idx = 0;
  inputs->next = NULL;

  avfilter_graph_parse_ptr(filter_graph, FILTER_DESC, &inputs, &outputs, NULL);
  avfilter_graph_config(filter_graph, NULL);

  // Processing loop
  while (av_read_frame(input_fmt_ctx, packet) >= 0) {
    if (packet->stream_index == video_stream_index) {
      avcodec_send_packet(dec_ctx, packet);
      while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
        frame->pts = frame->best_effort_timestamp;
        av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        while (av_buffersink_get_frame(buffersink_ctx, filt_frame) >= 0) {
          AVPacket out_pkt;
          av_init_packet(&out_pkt);
          out_pkt.data = NULL;
          out_pkt.size = 0;

          avcodec_send_frame(enc_ctx, filt_frame);
          filt_frame->pict_type = AV_PICTURE_TYPE_I;
          while (avcodec_receive_packet(enc_ctx, &out_pkt) >= 0) {
            out_pkt.stream_index = out_stream->index;
            av_packet_rescale_ts(&out_pkt, enc_ctx->time_base, out_stream->time_base);
            av_interleaved_write_frame(output_fmt_ctx, &out_pkt);
            av_packet_unref(&out_pkt);
          }
          av_frame_unref(filt_frame);
        }
        av_frame_unref(frame);
      }
    }
    av_packet_unref(packet);
  }

  av_write_trailer(output_fmt_ctx);
  avcodec_free_context(&dec_ctx);
  avcodec_free_context(&enc_ctx);
  avformat_close_input(&input_fmt_ctx);
  if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&output_fmt_ctx->pb);
  avformat_free_context(output_fmt_ctx);
  av_frame_free(&frame);
  av_frame_free(&filt_frame);
  av_packet_free(&packet);
  avfilter_graph_free(&filter_graph);
  return 0;
}
