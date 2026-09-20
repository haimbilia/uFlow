/* uFlow's bounded h264.rpl adapter for the pinned FFmpeg-wiiu decoder interface.
 * Based on the interface introduced by GaryOderNichts, 2020 (FFmpeg LGPL 2.1+).
 * Unlike the upstream adapter, allocate for the padded pitch/height, unwind
 * partial initialization, and emit only images actually returned by h264.rpl.
 */
#include <h264/decode.h>
#include <malloc.h>
#include <string.h>
#include "avcodec.h"
#include "internal.h"
#include "libavutil/imgutils.h"

typedef struct {
    void *decoder, *framebuffer;
    uint8_t *bitstream;
    unsigned bitstream_size;
    int opened, begun, pitch, height, got_frame, error;
    AVCodecContext *avctx;
    AVFrame *output_frame;
} UFlowH264;

static void output_frame(H264DecodeOutput *output)
{
    UFlowH264 *ctx = output->userMemory;
    int i;
    if (!ctx || !ctx->output_frame) return;
    for (i = 0; i < output->frameCount; ++i) {
        H264DecodeResult *r = output->decodeResults[i];
        const uint8_t *planes[4] = {0};
        int stride[4] = {0};
        if (!r || !r->framebuffer || r->width <= 0 || r->height <= 0 ||
            r->width > ctx->pitch || r->height > ctx->height ||
            r->width < ctx->avctx->width || r->height < ctx->avctx->height ||
            r->nextLine < r->width || r->nextLine > ctx->pitch) {
            ctx->error = AVERROR_INVALIDDATA;
            return;
        }
        if (ctx->got_frame) av_frame_unref(ctx->output_frame);
        if (ff_get_buffer(ctx->avctx, ctx->output_frame, 0) < 0) {
            ctx->error = AVERROR(ENOMEM);
            return;
        }
        planes[0] = r->framebuffer;
        planes[1] = planes[0] + r->nextLine * r->height;
        stride[0] = stride[1] = r->nextLine;
        av_image_copy(ctx->output_frame->data, ctx->output_frame->linesize, planes, stride,
                      AV_PIX_FMT_NV12, ctx->avctx->width, ctx->avctx->height);
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
    free(ctx->framebuffer); free(ctx->bitstream); free(ctx->decoder);
    ctx->framebuffer = ctx->decoder = NULL; ctx->bitstream = NULL;
    ctx->begun = ctx->opened = 0;
    return 0;
}

static int init_decoder(AVCodecContext *avctx)
{
    UFlowH264 *ctx = avctx->priv_data;
    /* h264.rpl supports at most 1920x1088, 8-bit 4:2:0. */
    const unsigned memory_size = 0x2200000 + 0x400 + 0x480000;
    void *user = ctx;
    if (avctx->width <= 0 || avctx->height <= 0 || avctx->width > 1920 || avctx->height > 1088)
        return AVERROR(EINVAL);
    ctx->avctx = avctx;
    ctx->pitch = FFALIGN(avctx->width, 256);
    ctx->height = FFALIGN(avctx->height, 16);
    ctx->decoder = memalign(1024, memory_size);
    ctx->framebuffer = memalign(1024, ctx->pitch * ctx->height * 3 / 2);
    if (!ctx->decoder || !ctx->framebuffer) goto fail;
    if (H264DECInitParam(memory_size, ctx->decoder) != 0 ||
        H264DECSetParam_FPTR_OUTPUT(ctx->decoder, output_frame) != 0 ||
        H264DECSetParam_OUTPUT_PER_FRAME(ctx->decoder, 1) != 0 ||
        H264DECSetParam_USER_MEMORY(ctx->decoder, &user) != 0 ||
        H264DECOpen(ctx->decoder) != 0) goto fail;
    ctx->opened = 1;
    if (H264DECBegin(ctx->decoder) != 0) goto fail;
    ctx->begun = 1;
    avctx->pix_fmt = AV_PIX_FMT_NV12;
    return 0;
fail:
    close_decoder(avctx);
    return AVERROR_EXTERNAL;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *packet)
{
    UFlowH264 *ctx = avctx->priv_data;
    unsigned needed;
    int result;
    *got_frame = 0;
    if (packet->size <= 0) return 0;
    if (packet->size > 8 * 1024 * 1024) return AVERROR_INVALIDDATA;
    needed = FFALIGN(packet->size + AV_INPUT_BUFFER_PADDING_SIZE, 1024);
    if (needed > ctx->bitstream_size) {
        uint8_t *next = memalign(1024, needed);
        if (!next) return AVERROR(ENOMEM);
        free(ctx->bitstream); ctx->bitstream = next; ctx->bitstream_size = needed;
    }
    memcpy(ctx->bitstream, packet->data, packet->size);
    memset(ctx->bitstream + packet->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    ctx->output_frame = data; ctx->got_frame = ctx->error = 0;
    result = H264DECSetBitstream(ctx->decoder, ctx->bitstream, packet->size,
                               packet->pts == AV_NOPTS_VALUE ? 0 : (double)packet->pts);
    if (!result) result = H264DECExecute(ctx->decoder, ctx->framebuffer);
    ctx->output_frame = NULL;
    if (ctx->error) return ctx->error;
    if (result != 0 && result != 0xE4) return AVERROR_INVALIDDATA;
    *got_frame = ctx->got_frame;
    return packet->size;
}

static void flush_decoder(AVCodecContext *avctx)
{
    UFlowH264 *ctx = avctx->priv_data;
    ctx->output_frame = NULL;
    if (ctx->begun) H264DECFlush(ctx->decoder);
}

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
