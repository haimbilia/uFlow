#include "video.hpp"
#include "video_color.hpp"
#include "video_order.hpp"
#include <SDL2/SDL_mixer.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <limits>
#include <vector>
#ifndef UFLOW_HOST_PREVIEW
#include <coreinit/core.h>
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {
constexpr int kRate = 48000, kAudioFrameBytes = 4;
constexpr std::size_t kAudioBytes = kRate * kAudioFrameBytes * 2;
constexpr int kMaxWidth = 1920, kMaxHeight = 1088;
struct Frame {
    std::vector<std::uint8_t> bytes;
    int width = 0, height = 0;
    double seconds = 0;
    double aspect = 1;
};
struct RawFrame {
    RawFrame()=default;
    RawFrame(const RawFrame &)=delete;
    RawFrame &operator=(const RawFrame &)=delete;
    AVFrame *frame=nullptr;
    enum State { Free, Queued, Converting } state=Free;
    double seconds=0, aspect=1;
    ~RawFrame() { av_frame_free(&frame); }
};
struct Decoder {
    AVFormatContext *format = nullptr;
    AVCodecContext *video = nullptr, *audio = nullptr;
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    SwsContext *scale = nullptr;
    int scaleWidth=0, scaleHeight=0, outputWidth=0, outputHeight=0;
    int scaleFormat=-1, scaleRange=-1, scaleMatrix=-1;
    SwrContext *resample = nullptr;
    ~Decoder() {
        sws_freeContext(scale); swr_free(&resample);
        av_packet_free(&packet); av_frame_free(&frame);
        avcodec_free_context(&video); avcodec_free_context(&audio);
        avformat_close_input(&format);
    }
};
struct ReorderFrames {
    VideoFrameOrder order;
    std::array<AVFrame *,VideoFrameOrder::Capacity> frames{};
    std::array<bool,VideoFrameOrder::Capacity> used{};
    ~ReorderFrames() { for(auto *&frame:frames) av_frame_free(&frame); }
    bool Push(AVFrame *frame, double pts) {
        for(unsigned i=0;i<frames.size();++i) if(!used[i]) {
            if(!frames[i]) frames[i]=av_frame_alloc();
            if(!frames[i] || !order.Push(pts,i)) return false;
            av_frame_move_ref(frames[i],frame); used[i]=true; return true;
        }
        return false;
    }
};
}

struct VideoPlayer::Impl {
    explicit Impl(SDL_Renderer *r) : renderer(r) {
        frameMutex = SDL_CreateMutex(); audioMutex = SDL_CreateMutex();
    }
    ~Impl() { SDL_DestroyMutex(frameMutex); SDL_DestroyMutex(audioMutex); }
    SDL_Renderer *renderer;
    SDL_Texture *texture = nullptr;
    std::array<SDL_Texture *,3> texturePool{};
    unsigned textureIndex=0;
    SDL_Thread *thread = nullptr;
    SDL_Thread *converter = nullptr;
    std::atomic<bool> producerDone{false}, converting{false};
    SDL_mutex *frameMutex = nullptr, *audioMutex = nullptr;
    std::atomic<bool> cancel{false}, suspended{false}, started{false}, done{false};
    std::atomic<Uint32> clockStart{0};
    std::atomic<Uint32> suspendedAt{0};
    std::atomic<std::uint32_t> decoded{0}, audioFrames{0}, loops{0};
    std::atomic<std::uint32_t> audioMissingFrames{0};
    std::atomic<std::uint32_t> handoffUs{0}, decoderCores{0}, converterCores{0};
    std::atomic<std::uint32_t> reorderedFrames{0}, timestampCorrections{0};
    std::atomic<std::uint32_t> convertedFrames{0}, lateDropped{0}, readUs{0}, decodeUs{0}, convertUs{0};
    std::atomic<bool> hardwareDecoder{false};
    std::uint32_t uploadUs=0;
    std::uint32_t presented = 0;
    Uint32 firstFrameAt=0, lastPresentedAt=0, maxPresentGap=0, longPresentGaps=0;
    bool buffering=false;
    std::array<Frame,4> frames;
    // Three waiting frames plus one leased to the converter. Only the decoder
    // changes AVFrame references; the converter only reads a leased image.
    std::array<RawFrame,4> rawFrames;
    std::array<unsigned,3> rawQueue{};
    std::size_t rawHead=0, rawCount=0;
    Frame displayed;
    std::size_t head = 0, count = 0;
    std::array<std::uint8_t,kAudioBytes> audioBytes{};
    std::size_t audioHead = 0, audioCount = 0;
    std::string path, error;
    int volume = 0, textureWidth = 0, textureHeight = 0;
    bool audioInitialized = false, audioOpened = false, decodeAudio = false;

    static std::uint32_t ElapsedUs(Uint64 begin) {
        return static_cast<std::uint32_t>((SDL_GetPerformanceCounter()-begin)*1000000/SDL_GetPerformanceFrequency());
    }
    int Send(AVCodecContext *codec, const AVPacket *packet) {
        const auto begin=SDL_GetPerformanceCounter();
        const int result=avcodec_send_packet(codec,packet);
        decodeUs.fetch_add(ElapsedUs(begin)); return result;
    }
    int Receive(AVCodecContext *codec, AVFrame *frame) {
        const auto begin=SDL_GetPerformanceCounter();
        const int result=avcodec_receive_frame(codec,frame);
        decodeUs.fetch_add(ElapsedUs(begin)); return result;
    }

    static int Interrupt(void *user) { return static_cast<Impl *>(user)->cancel.load() ? 1 : 0; }
    bool Running() {
        while (suspended.load() && !cancel.load()) SDL_Delay(5);
        return !cancel.load();
    }
    double Clock() const {
        return started.load() ? static_cast<Uint32>(SDL_GetTicks()-clockStart.load()) / 1000.0 : 0;
    }
    void Fail(const char *message) {
        if (cancel.load()) return;
        SDL_LockMutex(frameMutex); error = message; SDL_UnlockMutex(frameMutex);
    }
    void DecodeError(const char *stage, int code) {
        char detail[96], message[192];
        av_strerror(code,detail,sizeof(detail));
        std::snprintf(message,sizeof(message),"VIDEO %s: %s (%d)",stage,detail,code);
        Fail(message);
    }
    static void Audio(void *user, Uint8 *stream, int length) {
        auto &s = *static_cast<Impl *>(user);
        if (s.cancel.load() || s.suspended.load() || !s.started.load()) return;
        SDL_LockMutex(s.audioMutex);
        std::size_t remaining = std::min(static_cast<std::size_t>(length),s.audioCount);
        s.audioMissingFrames.fetch_add(static_cast<std::uint32_t>((length-remaining)/kAudioFrameBytes));
        s.audioFrames.fetch_add(static_cast<std::uint32_t>(remaining/kAudioFrameBytes));
        while (remaining) {
            const std::size_t part = std::min(remaining,kAudioBytes-s.audioHead);
            SDL_MixAudioFormat(stream,s.audioBytes.data()+s.audioHead,AUDIO_S16SYS,
                               static_cast<Uint32>(part),s.volume*MIX_MAX_VOLUME/100);
            stream += part; remaining -= part; s.audioCount -= part;
            s.audioHead = (s.audioHead+part)%kAudioBytes;
        }
        SDL_UnlockMutex(s.audioMutex);
    }
    bool QueueAudio(const std::uint8_t *bytes, std::size_t length) {
        while (length && Running()) {
            SDL_LockMutex(audioMutex);
            const std::size_t tail = (audioHead+audioCount)%kAudioBytes;
            const std::size_t part = std::min({length,kAudioBytes-audioCount,kAudioBytes-tail});
            if (part) {
                if (bytes) std::memcpy(audioBytes.data()+tail,bytes,part);
                else std::memset(audioBytes.data()+tail,0,part);
                audioCount += part;
            }
            SDL_UnlockMutex(audioMutex);
            if (!part) {
                // Never deadlock on malformed streams with audio but no video.
                if (!started.load()) { Fail("VIDEO HAS EXCESSIVE AUDIO PREROLL"); return false; }
                SDL_Delay(2);
            } else { if (bytes) bytes += part; length -= part; }
        }
        return !cancel.load();
    }
    bool QueueFrame(Frame &frame) {
        while (Running()) {
            SDL_LockMutex(frameMutex);
            if (count < frames.size()) {
                std::swap(frames[(head+count)%frames.size()],frame); ++count;
                SDL_UnlockMutex(frameMutex); return true;
            }
            SDL_UnlockMutex(frameMutex);
            SDL_Delay(2);
        }
        return false;
    }
    bool QueueRaw(AVFrame *frame, double seconds, double aspect) {
        while (Running()) {
            SDL_LockMutex(frameMutex);
            if (rawCount == rawQueue.size() && started.load() && rawFrames[rawQueue[rawHead]].seconds < Clock()-.04) {
                rawFrames[rawQueue[rawHead]].state=RawFrame::Free;
                rawHead=(rawHead+1)%rawQueue.size(); --rawCount; lateDropped.fetch_add(1);
            }
            if (rawCount < rawQueue.size()) {
                for(unsigned i=0;i<rawFrames.size();++i) if(rawFrames[i].state==RawFrame::Free) {
                    const auto handoffStart=SDL_GetPerformanceCounter();
                    auto &slot=rawFrames[i];
                    // Pool operations stay on this decoder thread, including
                    // returning a previously converted frame to FFmpeg's pool.
                    av_frame_unref(slot.frame);
                    av_frame_move_ref(slot.frame,frame);
                    slot.seconds=seconds; slot.aspect=aspect; slot.state=RawFrame::Queued;
                    rawQueue[(rawHead+rawCount)%rawQueue.size()]=i; ++rawCount;
                    handoffUs.fetch_add(ElapsedUs(handoffStart));
                    SDL_UnlockMutex(frameMutex); return true;
                }
            }
            SDL_UnlockMutex(frameMutex); SDL_Delay(2);
        }
        return false;
    }
    void ConvertFrames() {
        Decoder d; // Own scaler context; decoder images are read-only leases.
        Frame converted;
        VideoColor420 color420;
        while (Running()) {
            SDL_LockMutex(frameMutex);
            // Drop only when a newer, already-decoded frame can replace this one.
            while (rawCount>1 && started.load() && rawFrames[rawQueue[(rawHead+1)%rawQueue.size()]].seconds<=Clock()+.01) {
                rawFrames[rawQueue[rawHead]].state=RawFrame::Free;
                rawHead=(rawHead+1)%rawQueue.size(); --rawCount; lateDropped.fetch_add(1);
            }
            const bool available=rawCount>0;
            unsigned slotIndex=0;
            if (available) {
                converting.store(true);
                slotIndex=rawQueue[rawHead]; rawFrames[slotIndex].state=RawFrame::Converting;
                rawHead=(rawHead+1)%rawQueue.size(); --rawCount;
            }
            SDL_UnlockMutex(frameMutex);
            if (!available) {
                if (producerDone.load()) break;
                SDL_Delay(2); continue;
            }
            const auto conversionStart=SDL_GetPerformanceCounter();
#ifndef UFLOW_HOST_PREVIEW
            converterCores.fetch_or(1u<<OSGetCoreId());
#endif
            const auto &slot=rawFrames[slotIndex];
            const auto &raw=*slot.frame;
#ifdef UFLOW_HOST_PREVIEW
            if (const char *delay=std::getenv("UFLOW_VIDEO_TEST_CONVERT_DELAY_MS"); delay && std::atoi(delay)>0) {
                // Exercise buffer ownership while the decoder runs ahead. A
                // leased image must remain unchanged until conversion releases it.
                const auto format=static_cast<AVPixelFormat>(raw.format);
                const int size=av_image_get_buffer_size(format,raw.width,raw.height,1);
                if(size<=0) { Fail("TEST FRAME LAYOUT INVALID"); break; }
                std::vector<std::uint8_t> before(size), after(size);
                av_image_copy_to_buffer(before.data(),size,raw.data,raw.linesize,format,raw.width,raw.height,1);
                SDL_Delay(static_cast<Uint32>(std::clamp(std::atoi(delay),0,200)));
                av_image_copy_to_buffer(after.data(),size,raw.data,raw.linesize,format,raw.width,raw.height,1);
                if(before!=after) { Fail("LEASED VIDEO FRAME CHANGED DURING CONVERSION"); break; }
            }
#endif
            auto pixelFormat=static_cast<AVPixelFormat>(raw.format);
            int fullRange=raw.color_range==AVCOL_RANGE_JPEG;
            if(pixelFormat==AV_PIX_FMT_YUVJ420P) { pixelFormat=AV_PIX_FMT_YUV420P; fullRange=1; }
            if(pixelFormat==AV_PIX_FMT_YUVJ422P) { pixelFormat=AV_PIX_FMT_YUV422P; fullRange=1; }
            if(pixelFormat==AV_PIX_FMT_YUVJ444P) { pixelFormat=AV_PIX_FMT_YUV444P; fullRange=1; }
            const bool direct420=(pixelFormat==AV_PIX_FMT_NV12 || pixelFormat==AV_PIX_FMT_YUV420P) &&
                !(raw.width&1) && !(raw.height&1);
            const bool half420=direct420 && VideoColor420::NeedsHalfSize(raw.width,raw.height);
            if(direct420) {
                converted.width=raw.width/(half420?2:1);
                converted.height=raw.height/(half420?2:1);
            } else {
                const double factor=std::min({1.0,640.0/raw.width,360.0/raw.height});
                converted.width=std::max(2,static_cast<int>(raw.width*factor)&~1);
                converted.height=std::max(2,static_cast<int>(raw.height*factor)&~1);
            }
            converted.bytes.resize(static_cast<std::size_t>(converted.width)*converted.height*4);
            const std::uint8_t *source[]={raw.data[0],raw.data[1],raw.data[2],raw.data[3]};
            const int *sourcePitch=raw.linesize;
            if(direct420) {
                color420.Configure(fullRange,raw.colorspace==AVCOL_SPC_BT709);
                if(half420)
                    color420.ConvertHalf(source[0],sourcePitch[0],source[1],sourcePitch[1],source[2],sourcePitch[2],
                        pixelFormat==AV_PIX_FMT_NV12,raw.width,raw.height,converted.bytes.data(),converted.width*4);
                else
                    color420.Convert(source[0],sourcePitch[0],source[1],sourcePitch[1],source[2],sourcePitch[2],
                        pixelFormat==AV_PIX_FMT_NV12,raw.width,raw.height,converted.bytes.data(),converted.width*4);
            } else {
            const bool newScale=!d.scale || d.scaleWidth!=raw.width || d.scaleHeight!=raw.height ||
                d.outputWidth!=converted.width || d.outputHeight!=converted.height || d.scaleFormat!=pixelFormat;
            if(newScale) {
                d.scale=sws_getCachedContext(d.scale,raw.width,raw.height,pixelFormat,
                    converted.width,converted.height,AV_PIX_FMT_RGBA,SWS_FAST_BILINEAR,nullptr,nullptr,nullptr);
                d.scaleWidth=raw.width; d.scaleHeight=raw.height; d.outputWidth=converted.width;
                d.outputHeight=converted.height; d.scaleFormat=pixelFormat;
            }
            if(!d.scale) { Fail("VIDEO COLOUR CONVERSION FAILED"); break; }
            const int matrixId=raw.colorspace==AVCOL_SPC_BT709?SWS_CS_ITU709:SWS_CS_ITU601;
            if(newScale || d.scaleRange!=fullRange || d.scaleMatrix!=matrixId) {
                const int *matrix=sws_getCoefficients(matrixId);
                sws_setColorspaceDetails(d.scale,matrix,fullRange,matrix,1,0,1<<16,1<<16);
                d.scaleRange=fullRange; d.scaleMatrix=matrixId;
            }
            std::uint8_t *destination[]={converted.bytes.data(),nullptr,nullptr,nullptr};
            const int destinationPitch[]={converted.width*4,0,0,0};
            if(sws_scale(d.scale,source,sourcePitch,0,raw.height,destination,destinationPitch)<=0) {
                Fail("VIDEO COLOUR CONVERSION FAILED"); break;
            }
            }
            converted.seconds=slot.seconds; converted.aspect=slot.aspect;
            convertUs.fetch_add(ElapsedUs(conversionStart)); convertedFrames.fetch_add(1);
            SDL_LockMutex(frameMutex);
            rawFrames[slotIndex].state=RawFrame::Free;
            SDL_UnlockMutex(frameMutex);
            if(!QueueFrame(converted)) break;
            converting.store(false);
        }
        converting.store(false);
    }
    static int ConverterWorker(void *user) {
        auto &self=*static_cast<Impl *>(user);
        try { self.ConvertFrames(); }
        catch(const std::bad_alloc &) { self.Fail("NOT ENOUGH MEMORY FOR VIDEO CONVERSION"); }
        catch(...) { self.Fail("VIDEO CONVERSION FAILED"); }
        self.converting.store(false);
        return 0;
    }
    bool OpenCodec(AVStream *stream, AVCodecContext **out, bool hardware) {
        const AVCodec *codec = nullptr;
#ifndef UFLOW_HOST_PREVIEW
        if (hardware && stream->codecpar->codec_id == AV_CODEC_ID_H264)
            codec = avcodec_find_decoder_by_name("h264_wiiu");
#else
        (void)hardware;
#endif
        if (!codec) codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (hardware) hardwareDecoder.store(codec && std::strcmp(codec->name,"h264_wiiu")==0);
        if (!codec || !(*out = avcodec_alloc_context3(codec))) return false;
        if (avcodec_parameters_to_context(*out,stream->codecpar) < 0) return false;
        (*out)->thread_count = 1;
        (*out)->pkt_timebase = stream->time_base;
        return avcodec_open2(*out,codec,nullptr) >= 0;
    }
    int Run() {
        Decoder d;
        if (!d.frame || !d.packet || !(d.format = avformat_alloc_context())) { Fail("NOT ENOUGH MEMORY FOR VIDEO"); return 0; }
        d.format->interrupt_callback = {Interrupt,this};
        d.format->probesize = 1024*1024;
        d.format->max_analyze_duration = AV_TIME_BASE;
        // Only local files; do not allow playlists/network indirections.
        AVDictionary *options = nullptr;
        av_dict_set(&options,"protocol_whitelist","file",0);
        const std::string localPath = "file:" + path; // Wii U's fs: is a device path, not an FFmpeg protocol.
        const int opened = avformat_open_input(&d.format,localPath.c_str(),nullptr,&options);
        av_dict_free(&options);
        if (opened < 0 || !Running() || avformat_find_stream_info(d.format,nullptr) < 0) {
            Fail("VIDEO FILE UNAVAILABLE OR DAMAGED"); return 0;
        }
        int vi = -1, ai = -1;
        for (unsigned i = 0; i < d.format->nb_streams; ++i) {
            AVStream *s = d.format->streams[i];
            if (vi < 0 && s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) vi = i;
            if (ai < 0 && s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ai = i;
        }
        if (vi < 0) { Fail("NO PLAYABLE VIDEO STREAM"); return 0; }
        auto *vs = d.format->streams[vi];
        const auto *parameters = vs->codecpar;
#ifndef UFLOW_HOST_PREVIEW
        // Reject unsupported bitstreams before opening h264.rpl, including during
        // cleanup: the hardware cannot safely be assumed to recover from them.
        if (parameters->codec_id == AV_CODEC_ID_H264 &&
            !WiiUVideoH264Supported(parameters->profile & ~FF_PROFILE_H264_CONSTRAINED,parameters->level)) {
            Fail("H264 NEEDS BASELINE/MAIN/HIGH LEVEL 4.2 OR LOWER"); return 0;
        }
#endif
        if (parameters->width <= 0 || parameters->height <= 0 ||
            parameters->width > kMaxWidth || parameters->height > kMaxHeight) {
            Fail("VIDEO RESOLUTION LIMIT IS 1920 X 1080"); return 0;
        }
        if (!OpenCodec(vs,&d.video,true)) { Fail("UNSUPPORTED VIDEO CODEC"); return 0; }
        bool reorderDecoderOutput=hardwareDecoder.load();
#ifdef UFLOW_HOST_PREVIEW
        if(const char *test=std::getenv("UFLOW_VIDEO_TEST_REORDER")) reorderDecoderOutput=std::atoi(test)!=0;
#endif
        const double origin = vs->start_time == AV_NOPTS_VALUE ? 0 : vs->start_time*av_q2d(vs->time_base);
        const double fps = av_q2d(av_guess_frame_rate(d.format,vs,nullptr));
        const double frameStep = fps > 0 && fps <= 240 ? 1.0/fps : 1.0/30;
        // High-rate previews need not convert/upload more than 30 images/sec.
        const double displayStep = std::max(frameStep,1.0/30);
        AVStream *as = ai >= 0 ? d.format->streams[ai] : nullptr;
        if (decodeAudio && volume > 0 && as && OpenCodec(as,&d.audio,false)) {
            const auto layout = d.audio->channel_layout ? d.audio->channel_layout : av_get_default_channel_layout(d.audio->channels);
            if (d.audio->sample_rate > 0 && d.audio->sample_rate <= 192000 && d.audio->channels <= 8) {
                d.resample = swr_alloc_set_opts(nullptr,AV_CH_LAYOUT_STEREO,AV_SAMPLE_FMT_S16,kRate,
                    layout,d.audio->sample_fmt,d.audio->sample_rate,0,nullptr);
                if (!d.resample || swr_init(d.resample) < 0) swr_free(&d.resample);
            }
            if (!d.resample) avcodec_free_context(&d.audio);
        } else avcodec_free_context(&d.audio);
        std::vector<std::uint8_t> pcm;
        ReorderFrames reorder;
        double nextFrame = 0, fallbackPts = 0, lastVideo = 0;
        double lastDecodedPts=-std::numeric_limits<double>::infinity();
        std::int64_t audioWritten = 0;
        const auto emitVideo=[&](AVFrame *frame, double pts) -> bool {
            if(pts+.001>=nextFrame) {
                if(!frame->buf[0]) { Fail("VIDEO FRAME HAS NO OWNED BUFFER"); return false; }
                const auto sar=av_guess_sample_aspect_ratio(d.format,vs,frame);
                const double aspect=std::clamp(static_cast<double>(frame->width)/frame->height*
                    (sar.num>0 && sar.den>0 ? av_q2d(sar) : 1.0),.25,4.0);
                if(!QueueRaw(frame,std::max(0.0,pts),aspect)) return false;
                nextFrame=pts+displayStep-.001;
            }
            av_frame_unref(frame); return true;
        };
        const auto drainOrder=[&](double watermark, bool flush, bool missingDts=false) -> bool {
            const auto depth=static_cast<std::size_t>(std::clamp(parameters->video_delay,0,16));
            while(auto ready=reorder.order.PopReady(watermark,flush || (missingDts && reorder.order.Size()>depth))) {
                if(!emitVideo(reorder.frames[ready->slot],ready->pts)) return false;
                reorder.used[ready->slot]=false;
            }
            return true;
        };
        auto receive = [&](AVCodecContext *codec, bool video) -> bool {
            int result = AVERROR(EAGAIN);
            while (Running() && (result = Receive(codec,d.frame)) >= 0) {
                if (video) {
                    decoded.fetch_add(1);
#ifndef UFLOW_HOST_PREVIEW
                    decoderCores.fetch_or(1u<<OSGetCoreId());
#endif
#ifdef UFLOW_HOST_PREVIEW
                    if(const char *delay=std::getenv("UFLOW_VIDEO_TEST_DECODE_DELAY_MS"))
                        SDL_Delay(static_cast<Uint32>(std::clamp(std::atoi(delay),0,200)));
#endif
                    if (d.frame->width <= 0 || d.frame->height <= 0 || d.frame->width > kMaxWidth || d.frame->height > kMaxHeight) {
                        Fail("VIDEO CHANGED TO AN UNSUPPORTED RESOLUTION"); return false;
                    }
                    // Immediate h264.rpl output is decode order. FFmpeg's best-
                    // effort heuristic can replace its non-monotonic PTS with
                    // DTS; that labels future reference pictures as earlier ones.
                    const bool reorderHardware=reorderDecoderOutput && d.frame->pts!=AV_NOPTS_VALUE;
                    const auto timestamp = reorderHardware ? d.frame->pts :
                        d.frame->best_effort_timestamp != AV_NOPTS_VALUE ? d.frame->best_effort_timestamp : d.frame->pts;
                    const double pts = timestamp == AV_NOPTS_VALUE ? fallbackPts : timestamp*av_q2d(vs->time_base)-origin;
                    fallbackPts = pts+frameStep;
                    lastVideo = std::max(lastVideo,pts+frameStep);
                    if(reorderHardware) {
                        if(pts<lastDecodedPts) reorderedFrames.fetch_add(1);
                        if(d.frame->best_effort_timestamp!=AV_NOPTS_VALUE && d.frame->best_effort_timestamp!=timestamp)
                            timestampCorrections.fetch_add(1);
                        lastDecodedPts=pts;
                        const bool missingDts=d.frame->pkt_dts==AV_NOPTS_VALUE;
                        const double watermark=missingDts ? -std::numeric_limits<double>::infinity() :
                            d.frame->pkt_dts*av_q2d(vs->time_base)-origin;
                        if(!reorder.Push(d.frame,pts)) { Fail("VIDEO REORDER BUFFER LIMIT EXCEEDED"); return false; }
                        if(!drainOrder(watermark,false,missingDts)) return false;
                    } else if(!emitVideo(d.frame,pts)) return false;
                } else {
                    const int samples = static_cast<int>(av_rescale_rnd(swr_get_delay(d.resample,codec->sample_rate)+d.frame->nb_samples,
                        kRate,codec->sample_rate,AV_ROUND_UP));
                    if (samples <= 0 || samples > kRate*2) { Fail("INVALID VIDEO AUDIO FRAME"); return false; }
                    pcm.resize(static_cast<std::size_t>(samples)*kAudioFrameBytes);
                    std::uint8_t *output[] = {pcm.data()};
                    const int n = swr_convert(d.resample,output,samples,const_cast<const std::uint8_t **>(d.frame->extended_data),d.frame->nb_samples);
                    if (n < 0) { Fail("VIDEO AUDIO DECODE FAILED"); return false; }
                    std::int64_t at = audioWritten;
                    if (d.frame->best_effort_timestamp != AV_NOPTS_VALUE)
                        at = static_cast<std::int64_t>((d.frame->best_effort_timestamp*av_q2d(as->time_base)-origin)*kRate);
                    // Honor starting offsets and discontinuities; never allocate for a gap.
                    if (at > audioWritten+kRate*2) { Fail("VIDEO AUDIO TIMESTAMPS ARE INVALID"); return false; }
                    if (at > audioWritten && !QueueAudio(nullptr,static_cast<std::size_t>(at-audioWritten)*kAudioFrameBytes)) return false;
                    const int trim = static_cast<int>(std::clamp<std::int64_t>(audioWritten-at,0,n));
                    if (!QueueAudio(pcm.data()+trim*kAudioFrameBytes,(n-trim)*kAudioFrameBytes)) return false;
                    audioWritten = std::max(audioWritten,at+n);
                }
                av_frame_unref(d.frame);
            }
            av_frame_unref(d.frame);
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF && !cancel.load()) {
                DecodeError(video ? "FRAME DECODE" : "AUDIO DECODE",result); return false;
            }
            return !cancel.load();
        };
        unsigned packetsWithoutVideo = 0;
        while (Running()) {
            const auto readStart=SDL_GetPerformanceCounter();
            const int read = av_read_frame(d.format,d.packet);
            readUs.fetch_add(ElapsedUs(readStart));
            if (read < 0) {
                if (read != AVERROR_EOF) { Fail("VIDEO READ FAILED"); break; }
                Send(d.video,nullptr); if (!receive(d.video,true)) break;
                if(!drainOrder(0,true)) break;
                if (d.audio) { Send(d.audio,nullptr); if (!receive(d.audio,false)) break; }
                if (lastVideo <= 0) { Fail("VIDEO PRODUCED NO FRAMES"); break; }
                while (Running()) {
                    SDL_LockMutex(frameMutex); const bool empty = count == 0 && rawCount == 0 && !converting.load(); SDL_UnlockMutex(frameMutex);
                    SDL_LockMutex(audioMutex); const bool silent = audioCount == 0; SDL_UnlockMutex(audioMutex);
                    if (started.load() && empty && silent && Clock() >= lastVideo) break;
                    SDL_Delay(3);
                }
                if (!Running()) break;
                if (av_seek_frame(d.format,vi,vs->start_time == AV_NOPTS_VALUE ? 0 : vs->start_time,AVSEEK_FLAG_BACKWARD) < 0) {
                    Fail("VIDEO CANNOT LOOP"); break;
                }
                avcodec_flush_buffers(d.video);
                if (d.audio) {
                    avcodec_flush_buffers(d.audio); swr_close(d.resample);
                    if (swr_init(d.resample)<0) { Fail("VIDEO AUDIO RESTART FAILED"); break; }
                }
                loops.fetch_add(1);
                started.store(false); nextFrame = fallbackPts = lastVideo = 0; audioWritten = 0;
                lastDecodedPts=-std::numeric_limits<double>::infinity();
                packetsWithoutVideo = 0;
                continue;
            }
            AVCodecContext *codec = d.packet->stream_index == vi ? d.video : d.packet->stream_index == ai ? d.audio : nullptr;
            if (++packetsWithoutVideo > 20000 || d.packet->size > 8*1024*1024) { Fail("VIDEO STREAM LIMIT EXCEEDED"); break; }
            if (codec) {
                int sent = Send(codec,d.packet);
                if (sent == AVERROR(EAGAIN)) {
                    if (!receive(codec,codec==d.video)) break;
                    sent = Send(codec,d.packet);
                }
                if (sent < 0) { DecodeError(codec==d.video ? "PACKET DECODE" : "AUDIO PACKET",sent); break; }
                if (!receive(codec,codec==d.video)) break; // Preserve the specific failure.
                if (codec == d.video) packetsWithoutVideo = 0;
            }
            av_packet_unref(d.packet);
        }
        return 0;
    }
    static int Worker(void *user) {
        auto &self = *static_cast<Impl *>(user);
        try { self.Run(); }
        catch (const std::bad_alloc &) { self.Fail("NOT ENOUGH MEMORY FOR VIDEO"); }
        catch (...) { self.Fail("VIDEO PLAYBACK FAILED"); }
        self.producerDone.store(true);
        if(self.converter) { SDL_WaitThread(self.converter,nullptr); self.converter=nullptr; }
#ifdef UFLOW_HOST_PREVIEW
        if (const char *delay=std::getenv("UFLOW_VIDEO_TEST_CLEANUP_DELAY_MS"))
            SDL_Delay(static_cast<Uint32>(std::clamp(std::atoi(delay),0,3000)));
#endif
        self.done.store(true);
        return 0;
    }
};

VideoPlayer::VideoPlayer(SDL_Renderer *renderer) : impl_(new Impl(renderer)) {}
VideoPlayer::~VideoPlayer() {
    Stop();
    // Only final process teardown joins unfinished work; ordinary navigation,
    // errors and menu entry never wait on the hardware decoder.
    if (impl_->thread) { SDL_WaitThread(impl_->thread,nullptr); impl_->thread=nullptr; }
}
bool VideoPlayer::Start(const std::string &path, int volume) {
    Stop();
    av_log_set_level(AV_LOG_ERROR);
    auto &s = *impl_;
    if (s.thread) return false; // Previous cancelled decoder is still retiring.
    if (!s.frameMutex || !s.audioMutex) return false;
    for(auto &slot:s.rawFrames) {
        if(!slot.frame) slot.frame=av_frame_alloc();
        if(!slot.frame) return false;
    }
    s.path = path; s.volume = std::clamp(volume,0,100);
    s.cancel.store(false); s.suspended.store(false); s.started.store(false); s.done.store(false);
    s.producerDone.store(false); s.converting.store(false);
    s.error.clear();
    s.decoded.store(0); s.audioFrames.store(0); s.loops.store(0); s.presented=0;
    s.audioMissingFrames.store(0);
    s.handoffUs.store(0); s.decoderCores.store(0); s.converterCores.store(0);
    s.reorderedFrames.store(0); s.timestampCorrections.store(0);
    s.buffering=false; s.firstFrameAt=s.lastPresentedAt=s.maxPresentGap=s.longPresentGaps=0;
    s.convertedFrames.store(0); s.lateDropped.store(0); s.readUs.store(0); s.decodeUs.store(0);
    s.convertUs.store(0); s.uploadUs=0; s.hardwareDecoder.store(false);
    if (s.volume > 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        s.audioInitialized = true;
        if (Mix_OpenAudio(kRate,AUDIO_S16SYS,2,2048) == 0) {
            int frequency = 0, channels = 0; Uint16 format = 0;
            Mix_QuerySpec(&frequency,&format,&channels);
            if (frequency == kRate && channels == 2 && format == AUDIO_S16SYS) {
                s.audioOpened = true; Mix_SetPostMix(Impl::Audio,&s);
            } else Mix_CloseAudio();
        }
        if (!s.audioOpened) SDL_Log("Video audio unavailable; preview will be silent: %s",Mix_GetError());
    }
    s.decodeAudio=s.audioOpened;
    s.converter=SDL_CreateThreadWithStackSize(Impl::ConverterWorker,"uflow-video-color",256*1024,&s);
    if(!s.converter) { Stop(); return false; }
    s.thread = SDL_CreateThreadWithStackSize(Impl::Worker,"uflow-video",256*1024,&s);
    if (!s.thread) {
        s.cancel.store(true); s.producerDone.store(true);
        SDL_WaitThread(s.converter,nullptr); s.converter=nullptr; Stop(); return false;
    }
    return true;
}
void VideoPlayer::Stop() {
    auto &s = *impl_;
    s.cancel.store(true);
    if (s.audioOpened) { Mix_SetPostMix(nullptr,nullptr); Mix_CloseAudio(); s.audioOpened = false; }
    if (s.audioInitialized) { SDL_QuitSubSystem(SDL_INIT_AUDIO); s.audioInitialized = false; }
    for (auto *&texture : s.texturePool) { if(texture) SDL_DestroyTexture(texture); texture=nullptr; }
    s.texture=nullptr; s.textureIndex=0;
    s.textureWidth = s.textureHeight = 0;
    // The worker owns its queues until it exits. Cancellation unhooks audio and
    // hides video immediately without a join that could freeze the main screen.
    if (s.thread && !s.done.load()) return;
    if (s.thread) { SDL_WaitThread(s.thread,nullptr); s.thread = nullptr; }
    for (auto &frame : s.frames) frame = {};
    // Both workers have exited; pool references may now safely be released here.
    for (auto &slot : s.rawFrames) { if(slot.frame) av_frame_unref(slot.frame); slot.state=RawFrame::Free; }
    s.rawHead=s.rawCount=0;
    s.displayed = {}; s.head = s.count = s.audioHead = s.audioCount = 0;
    s.started.store(false);
}
void VideoPlayer::Suspend(bool suspended) {
    auto &s=*impl_;
    if(suspended&&!s.suspended.load()) { s.suspendedAt.store(SDL_GetTicks());s.suspended.store(true); }
    else if(!suspended&&s.suspended.load()) {
        // Pause the timeline too: resuming a frozen backdrop must not rush
        // through the frames corresponding to time spent in a settings menu.
        if(s.started.load())s.clockStart.fetch_add(SDL_GetTicks()-s.suspendedAt.load());
        s.suspended.store(false);
    }
}
bool VideoPlayer::CanStart() {
    if (impl_->cancel.load()) Stop();
    return impl_->thread == nullptr;
}
bool VideoPlayer::Active() const { return impl_->thread != nullptr && !impl_->cancel.load(); }
bool VideoPlayer::Ready() const { return impl_->texture != nullptr; }
bool VideoPlayer::Update() {
    auto &s = *impl_;
    if (!s.thread || s.cancel.load() || s.suspended.load()) return false;
    bool changed = false;
    SDL_LockMutex(s.frameMutex);
    if (s.count && !s.started.load()) {
        if(!s.buffering) { s.buffering=true; s.firstFrameAt=SDL_GetTicks(); }
        // Hold audio and video together until three frames are ready. The timeout
        // also starts tiny clips and error/EOF tails that cannot fill the queue.
        if(s.count>=3 || SDL_GetTicks()-s.firstFrameAt>=200 || s.producerDone.load()) {
            s.clockStart.store(SDL_GetTicks()); s.started.store(true); s.buffering=false;
            s.lastPresentedAt=0;
        }
    }
    if(!s.started.load()) { SDL_UnlockMutex(s.frameMutex); return false; }
    const double now = s.Clock();
    while (s.count && s.frames[s.head].seconds <= now+.01) {
        std::swap(s.displayed,s.frames[s.head]);
        s.head = (s.head+1)%s.frames.size(); --s.count; changed = true;
    }
    SDL_UnlockMutex(s.frameMutex);
    if (!changed) return false;
    const auto uploadStart=SDL_GetPerformanceCounter();
    if (s.displayed.width != s.textureWidth || s.displayed.height != s.textureHeight) {
        s.texture=nullptr;
        for (auto *&texture : s.texturePool) { if(texture) SDL_DestroyTexture(texture); texture=nullptr; }
        for (auto *&texture : s.texturePool) {
            texture=SDL_CreateTexture(s.renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,s.displayed.width,s.displayed.height);
            if (!texture) { s.Fail("VIDEO TEXTURE ALLOCATION FAILED"); return false; }
            SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);
        }
        s.textureWidth = s.displayed.width; s.textureHeight = s.displayed.height;
    }
    // Do not overwrite the texture GX2 is still reading from the previous frame.
    s.texture=s.texturePool[s.textureIndex]; s.textureIndex=(s.textureIndex+1)%s.texturePool.size();
    if (SDL_UpdateTexture(s.texture,nullptr,s.displayed.bytes.data(),s.displayed.width*4) != 0)
        s.Fail("VIDEO TEXTURE UPDATE FAILED");
    s.uploadUs+=Impl::ElapsedUs(uploadStart);
    const auto at=SDL_GetTicks();
    if(s.lastPresentedAt) {
        const auto gap=at-s.lastPresentedAt;
        s.maxPresentGap=std::max(s.maxPresentGap,gap);
        if(gap>50) ++s.longPresentGaps;
    }
    s.lastPresentedAt=at;
    ++s.presented;
    return true;
}
bool VideoPlayer::Draw(int width, int height) const {
    const auto &s = *impl_;
    if (!s.texture) return false;
    SDL_SetRenderDrawColor(s.renderer,0,0,0,255);
    const SDL_Rect screen{0,0,width,height}; SDL_RenderFillRect(s.renderer,&screen);
    const double aspect = s.displayed.aspect;
    const int w = std::min(width,static_cast<int>(height*aspect));
    const int h = std::min(height,static_cast<int>(width/aspect));
    const SDL_Rect destination{(width-w)/2,(height-h)/2,w,h};
    return SDL_RenderCopy(s.renderer,s.texture,nullptr,&destination) == 0;
}
std::string VideoPlayer::TakeError() {
    auto &s = *impl_;
    SDL_LockMutex(s.frameMutex); std::string result; result.swap(s.error); SDL_UnlockMutex(s.frameMutex);
    return result;
}
VideoPlayer::Stats VideoPlayer::Statistics() const {
    return {impl_->decoded.load(),impl_->presented,impl_->audioFrames.load(),impl_->loops.load(),
        impl_->convertedFrames.load(),impl_->lateDropped.load(),impl_->readUs.load(),impl_->decodeUs.load(),
        impl_->convertUs.load(),impl_->uploadUs,impl_->hardwareDecoder.load(),
        impl_->textureWidth,impl_->textureHeight,impl_->maxPresentGap,impl_->longPresentGaps,
        impl_->audioMissingFrames.load(),
        impl_->started.load()?static_cast<int>((impl_->Clock()-impl_->displayed.seconds)*1000):0,
        impl_->handoffUs.load(),impl_->decoderCores.load(),impl_->converterCores.load(),
        impl_->reorderedFrames.load(),impl_->timestampCorrections.load()};
}
