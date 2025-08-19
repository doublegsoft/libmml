### 手动编译x264

```bash

cd 3rd/x264

mkdir -p build/darwin

cd build/darwin

../../configure --enable-shared --disable-static --enable-pic --enable-asm

```

### 手动编译FFMPEG

```bash

cd 3rd/ffmpeg

mkdir -p build/darwin

cd build/darwin

../../configure \
  --enable-shared --disable-static \
  --disable-programs \
  --disable-doc \
  --enable-gpl 

make

```

To concatenate two videos using `ffmpeg`, you can use either the concat demuxer method or the concat protocol method, depending on the format and codec of your videos.

### Method 1: Using the Concat Demuxer

1. **Create a text file with the list of videos to concatenate.** Let's name this file `filelist.txt`.

    ```
    file 'video1.mp4'
    file 'video2.mp4'
    ```

2. **Run the ffmpeg command to concatenate the videos.**

    ```sh
    ffmpeg -f concat -safe 0 -i filelist.txt -c copy output.mp4
    ```

### Method 2: Using the Concat Protocol

This method is more straightforward but only works if the videos are of the same codec and format.

```sh
ffmpeg -i "concat:video1.mp4|video2.mp4" -c copy output.mp4
```

### Detailed Steps:

1. **Prepare the input videos.** Ensure both videos have the same codec, format, and resolution. If they differ, you might need to re-encode them to match.

2. **Create the file list (for method 1):**

    - Create a text file `filelist.txt` and add the paths of the videos to be concatenated. Each line should start with `file` followed by the path to the video file.

    ```txt
    file 'video1.mp4'
    file 'video2.mp4'
    ```

3. **Concatenate using `ffmpeg`:**

    - **Concat Demuxer Method:**

        ```sh
        ffmpeg -f concat -safe 0 -i filelist.txt -c copy output.mp4
        ```

        - `-f concat` tells `ffmpeg` to use the concat demuxer.
        - `-safe 0` allows the use of unsafe file paths.
        - `-i filelist.txt` specifies the input file list.
        - `-c copy` copies the streams without re-encoding.
        - `output.mp4` is the output file.

    - **Concat Protocol Method:**

        ```sh
        ffmpeg -i "concat:video1.mp4|video2.mp4" -c copy output.mp4
        ```

        - `concat:` is the concat protocol followed by the list of input files separated by `|`.
        - `-c copy` copies the streams without re-encoding.
        - `output.mp4` is the output file.

Choose the method that best suits your situation. The concat demuxer is more flexible but requires an additional text file, while the concat protocol is simpler but less flexible.

### 视频帧类型和时间戳

在视频处理过程中，PTS（Presentation Timestamp，显示时间戳）不总是单调递增的，尤其是在涉及 B-帧的情况下。为了更好地理解这个问题，我们需要深入了解视频帧类型和它们的时间戳（DTS 和 PTS）。

视频编码中有三种基本类型的帧：

1. **I-帧（关键帧）**:
   - 是完整的帧，不依赖于其他帧进行解码。
   - 解码和显示时间戳（DTS 和 PTS）通常是相同的，并且是单调递增的。

2. **P-帧（预测帧）**:
   - 依赖于之前的 I-帧或 P-帧进行解码。
   - P-帧的解码时间戳（DTS）和显示时间戳（PTS）通常也是单调递增的。

3. **B-帧（双向预测帧）**:
   - 依赖于之前的 I-帧/P-帧和之后的 I-帧/P-帧进行解码。
   - B-帧的显示时间戳（PTS）可能在它解码后才显示，因此它的 PTS 可能早于之前的 P-帧或 I-帧，导致 PTS 不单调递增。

### DTS 和 PTS

- **DTS（Decoding Timestamp，解码时间戳）**:
  指示帧应该被解码的时间顺序。DTS 总是单调递增的，以确保正确的解码顺序。

- **PTS（Presentation Timestamp，显示时间戳）**:
  指示帧应该被显示的时间顺序。PTS 不一定是单调递增的，尤其是在存在 B-帧时。

### B-帧的影响

在编码过程中，B-帧可以参考之前和之后的帧来进行压缩，这使得它们的 PTS 可以早于之前的帧。举个例子：

- 序列：I0, B2, B1, P3
  - I0：DTS=0，PTS=0
  - B1：DTS=2，PTS=1
  - B2：DTS=1，PTS=2
  - P3：DTS=3，PTS=3

在这个序列中，虽然 DTS 是单调递增的，但 PTS 并不是，因为 B1 的 PTS（1）早于 B2 的 PTS（2）。

### 处理 PTS 不单调递增的情况

当处理或复用视频流时，非单调递增的 PTS 可能会引起问题，如不同步或“Non monotonically increasing PTS”错误。我们可以通过以下几种方法处理这种情况：

1. **重新排序帧**:
   在处理帧时，按照 DTS 而不是 PTS 对它们进行排序，然后写入输出文件。这样可以确保解码顺序是正确的，即使 PTS 不单调递增。

2. **使用帧重排序缓冲**:
   使用一个缓冲区来暂时存储帧并重新排序，以确保它们按照 DTS 递增的顺序被处理。

3. **调整 PTS**:
   通过算法调整 PTS 使其符合单调递增的要求，这通常在特殊情况下使用，可能会影响视频的显示顺序。

### 示例：使用 FFmpeg 重新排序帧

以下是一个使用 FFmpeg 库的 C 语言示例，它演示了如何处理非单调递增的 PTS。

```c
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavutil/avutil.h>

// 帧重排序缓冲结构
typedef struct Frame {
    AVPacket pkt;
    struct Frame *next;
} Frame;

typedef struct {
    Frame *head;
    Frame *tail;
} FrameQueue;

void init_queue(FrameQueue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
}

void enqueue(FrameQueue *queue, AVPacket *pkt) {
    Frame *frame = (Frame *)av_malloc(sizeof(Frame));
    av_packet_ref(&frame->pkt, pkt);
    frame->next = NULL;

    if (!queue->tail) {
        queue->head = frame;
        queue->tail = frame;
    } else {
        queue->tail->next = frame;
        queue->tail = frame;
    }
}

AVPacket* dequeue(FrameQueue *queue) {
    if (!queue->head)
        return NULL;

    Frame *frame = queue->head;
    AVPacket *pkt = &frame->pkt;
    queue->head = frame->next;

    if (!queue->head)
        queue->tail = NULL;

    av_free(frame);
    return pkt;
}

void free_queue(FrameQueue *queue) {
    Frame *frame = queue->head;
    while (frame) {
        Frame *next = frame->next;
        av_packet_unref(&frame->pkt);
        av_free(frame);
        frame = next;
    }
    queue->head = NULL;
    queue->tail = NULL;
}

// 比较帧的 PTS
int compare_pts(const void *a, const void *b) {
    AVPacket *pkt1 = *(AVPacket **)a;
    AVPacket *pkt2 = *(AVPacket **)b;
    return pkt1->pts - pkt2->pts;
}

void process_frames(FrameQueue *queue, AVFormatContext *output_ctx) {
    int count = 0;
    Frame *frame = queue->head;

    while (frame) {
        count++;
        frame = frame->next;
    }

    if (count == 0)
        return;

    AVPacket **packets = (AVPacket **)av_malloc(sizeof(AVPacket *) * count);
    frame = queue->head;
    for (int i = 0; i < count; i++) {
        packets[i] = &frame->pkt;
        frame = frame->next;
    }

    qsort(packets, count, sizeof(AVPacket *), compare_pts);

    for (int i = 0; i < count; i++) {
        if (av_interleaved_write_frame(output_ctx, packets[i]) < 0) {
            fprintf(stderr, "Error writing packet to output file\n");
        }
        av_packet_unref(packets[i]);
    }

    av_free(packets);
    free_queue(queue);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <output file> <input file>\n", argv[0]);
        return 1;
    }

    const char *output_file = argv[1];
    const char *input_file = argv[2];

    AVFormatContext *input_ctx = NULL;
    AVFormatContext *output_ctx = NULL;
    AVPacket pkt;
    FrameQueue frame_queue;
    init_queue(&frame_queue);

    av_register_all();
    avformat_open_input(&input_ctx, input_file, NULL, NULL);
    avformat_find_stream_info(input_ctx, NULL);
    av_dump_format(input_ctx, 0, input_file, 0);

    avformat_alloc_output_context2(&output_ctx, NULL, NULL, output_file);
    if (!output_ctx) {
        fprintf(stderr, "Could not create output context\n");
        return 1;
    }

    for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
        AVStream *in_stream = input_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(output_ctx, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            return 1;
        }

        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            return 1;
        }
        out_stream->time_base = in_stream->time_base;
    }

    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_ctx->pb, output_file, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Could not open output file %s\n", output_file);
            return 1;
        }
    }

    avformat_write_header(output_ctx, NULL);

    while (av_read_frame(input_ctx, &pkt) >= 0) {
        AVStream *in_stream = input_ctx->streams[pkt.stream_index];
        AVStream *out_stream = output_ctx->streams[pkt.stream_index];

        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        enqueue(&frame_queue, &pkt);
        av_packet_unref(&pkt);
    }

    process_frames(&frame_queue, output_ctx);



    av_write_trailer(output_ctx);

    if (!(output_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_ctx->pb);

    avformat_free_context(output_ctx);
    avformat_close_input(&input_ctx);

    return 0;
}
```

### 代码解释

1. **帧队列和处理**:
   - 我们创建了一个帧队列来存储从输入读取的帧。
   - 在队列中，我们按 PTS 对帧进行排序，确保它们按显示时间的顺序输出。

2. **主函数**:
   - 初始化输入和输出上下文。
   - 从输入文件读取帧，将它们放入队列中。
   - 使用 `process_frames` 函数处理队列中的帧，确保输出文件中的帧按 PTS 顺序排列。

3. **处理帧的时间戳**:
   - 使用 `av_rescale_q_rnd` 函数将输入帧的时间戳转换为输出帧的时间戳。
   - 确保时间戳在帧排序和写入过程中正确调整。

### 编译和运行

```bash
gcc -o reorder_frames reorder_frames.c -lavformat -lavcodec -lavutil -lswresample -lswscale -lz
./reorder_frames output.mp4 input.mp4
```

这个示例程序读取一个输入视频文件，将其中的帧重新排序并按正确的顺序写入到一个输出文件中。这个过程确保了即使输入文件中有 B-帧，输出文件也能按正确的顺序显示这些帧。

## Benchmark

5到15秒共450个关键帧。

写文件：98秒
不写文件：102秒
不转图片：5秒
