/* uFlow's bounded h264.rpl adapter for the pinned FFmpeg-wiiu decoder interface.
 * Based on the interface introduced by GaryOderNichts, 2020 (FFmpeg LGPL 2.1+).
 * Unlike the upstream adapter, allocate for the padded pitch/height, unwind
 * partial initialization, and emit only images actually returned by h264.rpl.
 */
#ifndef UFLOW_H264_ADAPTER_TEST
#include <h264/decode.h>
#include <malloc.h>
#include <string.h>
#include <coreinit/cache.h>
#include "avcodec.h"
#include "internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#endif

void uflow_h264_metrics(const AVCodecContext *avctx, uint64_t *execute_us, uint64_t *skipped);
void uflow_h264_gpu_output(AVCodecContext *avctx, int enabled);

typedef struct {
    void *decoder;
    AVBufferPool *frames;
    uint8_t *bitstream;
    unsigned bitstream_size;
    int opened, begun, pitch, height, got_frame, error, gpu_output;
    AVCodecContext *avctx;
    AVFrame *output_frame;
    uint64_t execute_us, skipped;
} UFlowH264;

/* h264.rpl rejects a buffer whose physical start and one-past-end are in
 * different 256 MiB segments (InitParam returns 0x01060000). A successful
 * memalign(1024) does not guarantee that. Retry once with the next power of two
 * strictly larger than the buffer; validate again rather than assuming that
 * virtual alignment implies physical alignment. Free the rejected allocation
 * before retrying, and retain only the requested-size allocation on success. */
static void *allocate_hardware(unsigned size, int *segment_error)
{
    unsigned alignment = 1024, attempt;
    void *buffer;
    *segment_error = 0;
    if (!size || size > 64u * 1024 * 1024) return NULL;
    for (attempt = 0; attempt < 2; ++attempt) {
        *segment_error = 0;
        buffer = memalign(alignment, size);
        if (!buffer) return NULL;
        *segment_error = H264DECCheckMemSegmentation(buffer, size);
        if (!*segment_error) return buffer;
        free(buffer);
        while (alignment <= size) alignment <<= 1;
    }
    return NULL;
}

static void release_frame(void *opaque, uint8_t *data)
{
    (void)opaque;
    free(data);
}

#if LIBAVUTIL_VERSION_MAJOR >= 57
static AVBufferRef *allocate_frame(size_t size)
#else
static AVBufferRef *allocate_frame(int size)
#endif
{
    int segment_error;
    uint8_t *data = allocate_hardware(size, &segment_error);
    AVBufferRef *buffer;
    if (!data) return NULL;
    /* The decoder writes outside the CPU cache. Discard allocation-era dirty
     * lines before its first write, and invalidate before reading its output. */
    DCFlushRange(data, size);
    buffer = av_buffer_create(data, size, release_frame, NULL, 0);
    if (!buffer) free(data);
    return buffer;
}

static void output_frame(H264DecodeOutput *output)
{
    UFlowH264 *ctx = output ? output->userMemory : NULL;
    int i;
    if (!ctx || !ctx->output_frame) return;
    for (i = 0; i < output->frameCount; ++i) {
        H264DecodeResult *r = output->decodeResults[i];
        if (!r || !r->framebuffer || r->width <= 0 || r->height <= 0 ||
            r->width > ctx->pitch || r->height > ctx->height ||
            r->width < ctx->avctx->width || r->height < ctx->avctx->height ||
            r->nextLine < r->width || r->nextLine > ctx->pitch ||
            r->framebuffer != ctx->output_frame->data[0] || ctx->got_frame) {
            ctx->error = AVERROR_INVALIDDATA;
            return;
        }
        ctx->output_frame->data[1] = ctx->output_frame->data[0] + r->nextLine * r->height;
        ctx->output_frame->linesize[0] = ctx->output_frame->linesize[1] = r->nextLine;
        ctx->output_frame->pts = (int64_t)r->timestamp;
        ctx->got_frame = 1;
    }
}

static int close_decoder(AVCodecContext *avctx)
{
    UFlowH264 *ctx = avctx->priv_data;
    ctx->output_frame = NULL;
    if (ctx->begun) H264DECEnd(ctx->decoder);
    if (ctx->opened) H264DECClose(ctx->decoder);
    av_buffer_pool_uninit(&ctx->frames);
    free(ctx->bitstream); free(ctx->decoder);
    ctx->decoder = NULL; ctx->bitstream = NULL;
    ctx->begun = ctx->opened = 0;
    return 0;
}

static int init_decoder(AVCodecContext *avctx)
{
    UFlowH264 *ctx = avctx->priv_data;
    /* h264.rpl supports at most 1920x1088, 8-bit 4:2:0. */
    uint32_t memory_size = 0;
    int result, segment_error = 0, error = AVERROR_EXTERNAL;
    const char *stage = "memory requirement";
    void *user = ctx;
    if (avctx->width <= 0 || avctx->height <= 0 || avctx->width > 1920 || avctx->height > 1088)
        return AVERROR(EINVAL);
    ctx->avctx = avctx;
    /* Ask the platform for this stream's workspace, rather than allocating
     * 38.5 MiB for every small preview. Constraint flags aren't profile IDs. */
    result = H264DECMemoryRequirement(avctx->profile & ~FF_PROFILE_H264_CONSTRAINED,
                                     avctx->level, avctx->width, avctx->height, &memory_size);
    if (result != 0) goto fail;
    if (!memory_size || memory_size > 0x2200000 + 0x400 + 0x480000) {
        error = AVERROR(EINVAL); result = error; goto fail;
    }
    ctx->pitch = FFALIGN(avctx->width, 256);
    ctx->height = FFALIGN(avctx->height, 16);
    ctx->decoder = allocate_hardware(memory_size, &segment_error);
    ctx->frames = av_buffer_pool_init(ctx->pitch * ctx->height * 3 / 2, allocate_frame);
    if (!ctx->decoder || !ctx->frames) {
        if (!ctx->decoder && segment_error) {
            stage = "workspace segmentation"; result = segment_error; goto fail;
        }
        stage = "allocation"; error = AVERROR(ENOMEM); result = error; goto fail;
    }
#define INIT_STEP(name, call) do { stage = name; result = (call); if (result != 0) goto fail; } while (0)
    INIT_STEP("initialization", H264DECInitParam(memory_size, ctx->decoder));
    INIT_STEP("output callback", H264DECSetParam_FPTR_OUTPUT(ctx->decoder, output_frame));
    INIT_STEP("frame mode", H264DECSetParam_OUTPUT_PER_FRAME(ctx->decoder, 1));
    INIT_STEP("user memory", H264DECSetParam_USER_MEMORY(ctx->decoder, &user));
    INIT_STEP("open", H264DECOpen(ctx->decoder));
    ctx->opened = 1;
    INIT_STEP("begin", H264DECBegin(ctx->decoder));
#undef INIT_STEP
    ctx->begun = 1;
    avctx->pix_fmt = AV_PIX_FMT_NV12;
    return 0;
fail:
    av_log(avctx, AV_LOG_ERROR, "H264 %s failed: code=%d workspace=%u address=%p profile=%d level=%d size=%dx%d\n",
           stage, result, memory_size, ctx->decoder, avctx->profile, avctx->level, avctx->width, avctx->height);
    close_decoder(avctx);
    return error;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *packet)
{
    UFlowH264 *ctx = avctx->priv_data;
    unsigned needed;
    int result;
    AVFrame *frame = data;
    int64_t started;
    *got_frame = 0;
    if (packet->size <= 0) return 0;
    if (packet->size > 8 * 1024 * 1024) return AVERROR_INVALIDDATA;
    /* Only the platform's positively identified non-reference pictures may be
     * skipped. Reference pictures still reach the decoder to preserve its DPB. */
    if (avctx->skip_frame >= AVDISCARD_NONREF) {
        BOOL skippable = FALSE;
        if (!H264DECCheckSkipableFrame(packet->data, packet->size, &skippable) && skippable) {
            ++ctx->skipped;
            return packet->size;
        }
    }
    needed = FFALIGN(packet->size + AV_INPUT_BUFFER_PADDING_SIZE, 1024);
    if (needed > ctx->bitstream_size) {
        int segment_error;
        uint8_t *next = allocate_hardware(needed, &segment_error);
        if (!next) return AVERROR(ENOMEM);
        free(ctx->bitstream); ctx->bitstream = next; ctx->bitstream_size = needed;
    }
    memcpy(ctx->bitstream, packet->data, packet->size);
    memset(ctx->bitstream + packet->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    result = ff_decode_frame_props(avctx, frame);
    if (result < 0) return result;
    frame->buf[0] = av_buffer_pool_get(ctx->frames);
    if (!frame->buf[0]) return AVERROR(ENOMEM);
    frame->data[0] = frame->buf[0]->data;
    frame->data[1] = frame->data[0] + ctx->pitch * ctx->height;
    frame->linesize[0] = frame->linesize[1] = ctx->pitch;
    frame->width = avctx->width; frame->height = avctx->height;
    frame->format = AV_PIX_FMT_NV12;
    ctx->output_frame = data; ctx->got_frame = ctx->error = 0;
    result = H264DECSetBitstream(ctx->decoder, ctx->bitstream, packet->size,
                               packet->pts == AV_NOPTS_VALUE ? 0 : (double)packet->pts);
    started = av_gettime_relative();
    if (!result) result = H264DECExecute(ctx->decoder, frame->data[0]);
    ctx->execute_us += av_gettime_relative() - started;
    ctx->output_frame = NULL;
    if (ctx->error || (result & ~0xff)) {
        av_frame_unref(frame);
        return ctx->error ? ctx->error : AVERROR_INVALIDDATA;
    }
    if (ctx->got_frame) {
        // GPU-only consumers invalidate the texture cache, not the CPU cache.
        // An opted-in caller must invalidate before any CPU conversion/copy.
        if (!ctx->gpu_output) DCInvalidateRange(frame->buf[0]->data, frame->buf[0]->size);
    } else av_frame_unref(frame);
    *got_frame = ctx->got_frame;
    return packet->size;
}

/* Called only on the producer thread for this explicitly selected codec. */
void uflow_h264_metrics(const AVCodecContext *avctx, uint64_t *execute_us, uint64_t *skipped)
{
    const UFlowH264 *ctx = avctx->priv_data;
    *execute_us = ctx->execute_us; *skipped = ctx->skipped;
}

void uflow_h264_gpu_output(AVCodecContext *avctx, int enabled)
{
    UFlowH264 *ctx = avctx->priv_data;
    ctx->gpu_output = enabled != 0;
}

static void flush_decoder(AVCodecContext *avctx)
{
    UFlowH264 *ctx = avctx->priv_data;
    ctx->output_frame = NULL;
    if (ctx->begun) H264DECFlush(ctx->decoder);
}

#ifndef UFLOW_H264_ADAPTER_TEST
AVCodec ff_h264_wiiu_decoder = {
    .name = "h264_wiiu",
    .long_name = NULL_IF_CONFIG_SMALL("Wii U H.264 preview decoder"),
    .type = AVMEDIA_TYPE_VIDEO,
    .id = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(UFlowH264),
    .init = init_decoder,
    .close = close_decoder,
    .decode = decode_frame,
    .flush = flush_decoder,
    .bsfs = "h264_mp4toannexb",
};
#endif
