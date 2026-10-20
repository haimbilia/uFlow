/* Production adapter with real FFmpeg buffers and fault-injected Wii U calls. */
#define UFLOW_H264_ADAPTER_TEST
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/time.h>
#define FFALIGN(x,a) (((x)+(a)-1)&~((a)-1))
typedef int BOOL;
#define FALSE 0
typedef struct {void *framebuffer;int width,height,nextLine;double timestamp;} H264DecodeResult;
typedef struct {void *userMemory;int frameCount;H264DecodeResult **decodeResults;} H264DecodeOutput;
static int fail_step,step,allocation,fail_allocation,live,opened,begun,closed,ended;
static int executes,skip_result,skippable,status,no_output,bad_output,segmentation_error;
static unsigned reject_segments,segment_checks,last_alignment;
static uint32_t requirement=8192,requested;
static int width=640,height=480;
static double timestamp;
static void *user_memory;
static void (*callback)(H264DecodeOutput *);
static int call(void) {return ++step==fail_step?123:0;}
static void *allocate(size_t alignment,size_t bytes) {
    last_alignment=alignment;
    ++allocation;if(allocation==fail_allocation)return NULL;
    void *p=NULL;assert(!posix_memalign(&p,alignment,bytes));++live;return p;
}
static void release(void *p) {if(p){--live;free(p);}}
static int H264DECMemoryRequirement(int profile,int level,int w,int h,uint32_t *out) {
    assert(profile==66||profile==77||profile==100);assert(level>0);
    assert(w==width&&h==height);*out=requirement;return call();
}
static int H264DECInitParam(int bytes,void *p) {assert(p);requested=bytes;return call();}
static int H264DECSetParam_FPTR_OUTPUT(void *p,void(*cb)(H264DecodeOutput*)) {assert(p&&cb);callback=cb;return call();}
static int H264DECSetParam_OUTPUT_PER_FRAME(void *p,int n) {assert(p&&n==1);return call();}
static int H264DECSetParam_USER_MEMORY(void *p,void **user) {assert(p&&*user);user_memory=*user;return call();}
static int H264DECOpen(void *p) {assert(p);int e=call();if(!e)++opened;return e;}
static int H264DECBegin(void *p) {assert(p);int e=call();if(!e)++begun;return e;}
static int H264DECEnd(void *p) {assert(p);++ended;return 0;}
static int H264DECClose(void *p) {assert(p);++closed;return 0;}
static int H264DECFlush(void *p) {assert(p);return 0;}
static int H264DECCheckMemSegmentation(void *p,uint32_t size) {
    assert(p&&size);++segment_checks;
    if(reject_segments){--reject_segments;return -1;}
    return segmentation_error;
}
static void DCFlushRange(void *p,uint32_t size) {assert(p&&size);}
static unsigned invalidations;
static void DCInvalidateRange(void *p,uint32_t size) {assert(p&&size);++invalidations;}
static int H264DECCheckSkipableFrame(const uint8_t *b,uint32_t size,BOOL *skip) {assert(b&&size);*skip=skippable;return skip_result;}
static int H264DECSetBitstream(void *p,uint8_t *b,int size,double pts) {
    assert(p&&b&&size>0);assert(!((uintptr_t)b&1023));timestamp=pts;return 0;
}
static int H264DECExecute(void *p,void *b) {
    assert(p&&b&&!((uintptr_t)b&1023));++executes;
    const int pitch=FFALIGN(width,256), padded=FFALIGN(height,16);
    memset(b,executes,pitch*padded*3/2);
    if(!no_output) {
        H264DecodeResult r={b,width,padded,pitch,timestamp};
        if(bad_output==1)r.framebuffer=(uint8_t*)b+1;
        if(bad_output==2)r.height=padded+16;
        H264DecodeResult *results[]={&r,&r};
        H264DecodeOutput o={user_memory,bad_output==3?2:1,results};callback(&o);
    }
    return status;
}
/* Internal FFmpeg helper: public frame ownership functions below stay real. */
static int ff_decode_frame_props(AVCodecContext *c,AVFrame *f) {
    f->color_range=c->color_range;f->colorspace=c->colorspace;return 0;
}
#define memalign allocate
#define free release
#include "../tools/ffmpeg/h264_wiiu.c"
#undef free
static void reset(void) {
    assert(live==0);step=allocation=opened=begun=closed=ended=0;requested=0;
    fail_step=fail_allocation=executes=skip_result=skippable=status=no_output=bad_output=segmentation_error=0;
    requirement=8192;width=640;height=480;
    reject_segments=segment_checks=last_alignment=0;
}
static AVCodecContext context(UFlowH264 *state,int profile) {
    AVCodecContext c={0};c.priv_data=state;c.width=width;c.height=height;
    c.profile=profile;c.level=31;c.color_range=AVCOL_RANGE_MPEG;c.colorspace=AVCOL_SPC_BT709;return c;
}
static int decode(AVCodecContext *c,AVFrame *f,int pts,int *got) {
    uint8_t data[9]={0,0,0,1,1};AVPacket packet={0};packet.data=data;packet.size=sizeof(data);packet.pts=pts;
    return decode_frame(c,f,got,&packet);
}
int main(void) {
    for(int profile=0;profile<3;++profile)for(int failure=0;failure<=7;++failure) {
        int profiles[]={66|FF_PROFILE_H264_CONSTRAINED,77,100};
        reset();fail_step=failure;UFlowH264 state={0};AVCodecContext c=context(&state,profiles[profile]);
        int result=init_decoder(&c);
        if(!failure){assert(result==0&&requested==requirement&&live==1);close_decoder(&c);}
        else assert(result==AVERROR_EXTERNAL&&live==0);
        assert(opened==closed&&begun==ended);close_decoder(&c);assert(!live);
    }
    reset();fail_allocation=1;UFlowH264 failed={0};AVCodecContext fc=context(&failed,66);
    assert(init_decoder(&fc)==AVERROR(ENOMEM)&&!live);
    reset();segmentation_error=1;UFlowH264 invalid_memory={0};AVCodecContext mc=context(&invalid_memory,66);
    assert(init_decoder(&mc)==AVERROR_EXTERNAL && requested==0 && !opened && !begun && !live);
    close_decoder(&mc);assert(!live); // Rejected memory never reaches hardware initialization.
    /* The allocator releases an unsuitable buffer and retries once, using the
     * hardware check on both allocations. Sizes ending exactly on a power-of-two
     * boundary need a larger alignment because UVD checks the one-past-end. */
    unsigned retry_sizes[]={1024,8192,40u*1024*1024};
    for(unsigned index=0;index<sizeof(retry_sizes)/sizeof(retry_sizes[0]);++index) {
        unsigned size=retry_sizes[index];
        reset();int e=0;reject_segments=1;void *p=allocate_hardware(size,&e);
        assert(p && !e && segment_checks==2 && allocation==2 && live==1 && last_alignment>size);
        release(p);assert(!live);
    }
    reset();int se=0;reject_segments=2;
    assert(!allocate_hardware(8192,&se) && se==-1 && segment_checks==2 && allocation==2 && !live);
    reset();reject_segments=1;fail_allocation=2;
    assert(!allocate_hardware(8192,&se) && !se && allocation==2 && !live);
    reset();reject_segments=1;UFlowH264 recovered={0};AVCodecContext rc=context(&recovered,100);
    assert(!init_decoder(&rc) && requested==requirement && segment_checks==2 && live==1);
    close_decoder(&rc);assert(!live && opened==closed && begun==ended);
    reset();UFlowH264 packet_recovery={0};AVCodecContext pc=context(&packet_recovery,66);
    assert(!init_decoder(&pc));reject_segments=1;
    AVFrame *packet_frame=av_frame_alloc();int packet_got=0;
    assert(decode(&pc,packet_frame,0,&packet_got)>0 && packet_got && allocation==4);
    reject_segments=1;AVFrame *second_frame=av_frame_alloc();
    assert(decode(&pc,second_frame,1,&packet_got)>0 && packet_got && allocation==6);
    assert(packet_frame->data[0][0]==1 && second_frame->data[0][0]==2);
    close_decoder(&pc);av_frame_free(&packet_frame);av_frame_free(&second_frame);assert(!live);
    for(int invalid=0;invalid<2;++invalid) {
        reset();requirement=invalid?UINT32_MAX:0;UFlowH264 state={0};AVCodecContext c=context(&state,66);
        assert(init_decoder(&c)==AVERROR(EINVAL)&&allocation==0&&!live);
    }
    /* Retain output through later decodes and decoder/pool teardown. */
    for(int resolution=0;resolution<2;++resolution) {
        reset();width=resolution?1920:642;height=resolution?1080:358;
        UFlowH264 state={0};AVCodecContext c=context(&state,100);assert(!init_decoder(&c));
        AVFrame *held=av_frame_alloc(),*next=av_frame_alloc();int got;
        assert(decode(&c,held,91,&got)>0&&got);
        uint8_t *first=held->data[0];assert(first==held->buf[0]->data&&first[0]==1&&held->pts==91);
        assert(held->data[1]==first+FFALIGN(width,256)*FFALIGN(height,16));
        assert(held->width==width&&held->height==height&&held->colorspace==AVCOL_SPC_BT709);
        for(int i=0;i<200;++i) {
            assert(decode(&c,next,i,&got)>0&&got&&next->data[0]!=first);
            assert(first[0]==1&&first[held->buf[0]->size-1]==1);av_frame_unref(next);
        }
        assert(allocation==4); /* workspace, packet and only two output images */
        flush_decoder(&c);assert(first[0]==1);close_decoder(&c);
        assert(first[0]==1&&live==1);av_frame_free(&held);av_frame_free(&next);assert(!live);
    }
    reset();UFlowH264 state={0};AVCodecContext c=context(&state,77);assert(!init_decoder(&c));
    AVFrame *f=av_frame_alloc();int got;
    c.skip_frame=AVDISCARD_NONREF;skippable=1;
    assert(decode(&c,f,0,&got)>0&&!got&&!executes&&state.skipped==1);
    skippable=0;assert(decode(&c,f,1,&got)>0&&got&&executes==1);av_frame_unref(f);
    skippable=1;skip_result=1;assert(decode(&c,f,2,&got)>0&&got&&executes==2);av_frame_unref(f);
    c.skip_frame=AVDISCARD_DEFAULT;skip_result=0;
    assert(decode(&c,f,3,&got)>0&&got&&executes==3);av_frame_unref(f);
    for(int fault=1;fault<=3;++fault) {
        bad_output=fault;assert(decode(&c,f,4,&got)==AVERROR_INVALIDDATA&&!f->buf[0]);
    }
    bad_output=0;no_output=1;assert(decode(&c,f,5,&got)>0&&!got&&!f->buf[0]);no_output=0;
    status=0xE4;assert(decode(&c,f,6,&got)>0&&got);av_frame_unref(f);
    status=0x100;assert(decode(&c,f,7,&got)==AVERROR_INVALIDDATA&&!f->buf[0]);
    av_frame_free(&f);close_decoder(&c);assert(!live);
    for(int failure=2;failure<=3;++failure) {
        reset();UFlowH264 s={0};AVCodecContext cc=context(&s,66);assert(!init_decoder(&cc));
        fail_allocation=failure;f=av_frame_alloc();assert(decode(&cc,f,0,&got)==AVERROR(ENOMEM));
        av_frame_free(&f);close_decoder(&cc);assert(!live);
    }
    reset();UFlowH264 s={0};AVCodecContext cc=context(&s,66);assert(!init_decoder(&cc));
    segmentation_error=1;f=av_frame_alloc();assert(decode(&cc,f,0,&got)==AVERROR(ENOMEM));
    av_frame_free(&f);close_decoder(&cc);assert(!live);
    reset();UFlowH264 gpu={0};AVCodecContext gc=context(&gpu,100);assert(!init_decoder(&gc));
    f=av_frame_alloc();invalidations=0;
    assert(decode(&gc,f,0,&got)>0&&got&&invalidations==1);av_frame_unref(f);
    uflow_h264_gpu_output(&gc,1);
    assert(decode(&gc,f,1,&got)>0&&got&&invalidations==1);av_frame_unref(f);
    uflow_h264_gpu_output(&gc,0);
    assert(decode(&gc,f,2,&got)>0&&got&&invalidations==2);av_frame_free(&f);
    close_decoder(&gc);assert(!live);
    puts("PASS: native init fault cleanup, bounded segment-safe workspace/packet/frame allocation retries, zero-copy padded NV12, retained frame ownership through decode/flush/close, pool reuse, safe non-reference skipping, decode/allocation failures, explicit GPU-only cache handling with CPU-safe default");
}
