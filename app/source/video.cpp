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
#include <functional>
#include <new>
#include <limits>
#include <vector>
#ifndef UFLOW_HOST_PREVIEW
#include <coreinit/core.h>
#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/thread.h>
// Exported by coreinit and libwut, but absent from the pinned WUT header.
// Returns accumulated system ticks across the requested CPU-core mask.
extern "C" OSTime OSGetThreadCoreTime(OSThread *, uint32_t);
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifndef UFLOW_HOST_PREVIEW
void uflow_h264_metrics(const AVCodecContext *, uint64_t *, uint64_t *);
void uflow_h264_gpu_output(AVCodecContext *, int);
int UFlow_YuvShadersReady(void);
int UFlow_SetYuvTexture(SDL_Texture *, unsigned);
int UFlow_BindNV12(SDL_Texture *,const uint8_t *,int,size_t,const uint8_t *,int,size_t,void (*)(void *),void *);
#endif
}

namespace {
std::atomic<bool> decoderWakePriority{false};
#ifndef UFLOW_HOST_PREVIEW
void ReleaseTextureFrame(void *opaque) {
    auto *frame=static_cast<AVFrame *>(opaque);
    av_frame_free(&frame);
}
#endif
void VideoStage(const char *stage) {
#ifndef UFLOW_HOST_PREVIEW
    // Never route video diagnostics through the potentially stalled SD path.
    // Include the actual native thread so a recorded heap owner can be mapped
    // even when it was created after the startup thread snapshot.
    OSReport("uFlow: video-stage ticks=%u thread=%p %s\n",SDL_GetTicks(),OSGetCurrentThread(),stage);
#else
    (void)stage;
#endif
}
#ifndef UFLOW_HOST_PREVIEW
void VideoLog(void *context, int level, const char *format, va_list arguments) {
    va_list copy;
    va_copy(copy,arguments);
    av_log_default_callback(context,level,format,copy);
    va_end(copy);
    if(level>AV_LOG_ERROR) return;
    char message[768];
    std::vsnprintf(message,sizeof(message),format,arguments);
    OSReport("uFlow: ffmpeg-error ticks=%u thread=%p %s\n",SDL_GetTicks(),OSGetCurrentThread(),message);
}
#endif
constexpr int kRate = 48000, kAudioFrameBytes = 4;
constexpr std::size_t kAudioBytes = kRate * kAudioFrameBytes * 2;
constexpr int kMaxWidth = 1920, kMaxHeight = 1088;
struct FrameDeleter { void operator()(AVFrame *p) const { av_frame_free(&p); } };
struct Frame {
    std::vector<std::uint8_t> bytes;
    int width = 0, height = 0;
    unsigned yuvMode=0;
    bool nv12=false;
    std::unique_ptr<AVFrame,FrameDeleter> native;
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
    AVFrame *frame = av_frame_alloc(), *audioFrame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    SwsContext *scale = nullptr;
    int scaleWidth=0, scaleHeight=0, outputWidth=0, outputHeight=0;
    int scaleFormat=-1, scaleRange=-1, scaleMatrix=-1;
    SwrContext *resample = nullptr;
    ~Decoder() {
        sws_freeContext(scale); swr_free(&resample);
        av_packet_free(&packet); av_frame_free(&frame); av_frame_free(&audioFrame);
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

void VideoDecoderWakePriority(bool enabled) { decoderWakePriority.store(enabled); }

struct VideoPlayer::Impl {
    explicit Impl(SDL_Renderer *r) : renderer(r) {
        frameMutex = SDL_CreateMutex(); audioMutex = SDL_CreateMutex();
    }
    ~Impl() { SDL_DestroyMutex(frameMutex); SDL_DestroyMutex(audioMutex); }
    SDL_Renderer *renderer;
    SDL_Texture *texture = nullptr;
    bool retainedFrame=false;
    std::array<SDL_Texture *,3> texturePool{};
    unsigned textureIndex=0;
    unsigned textureYuvMode=0;
    bool textureNV12=false, gpuNV12=false;
    bool gpuColour=false;
    SDL_Thread *thread = nullptr;
    SDL_Thread *converter = nullptr;
    std::atomic<bool> producerDone{false}, converting{false};
    SDL_mutex *frameMutex = nullptr, *audioMutex = nullptr;
    std::atomic<bool> cancel{false}, suspended{false}, started{false}, done{false};
    std::atomic<bool> completed{false};
    std::atomic<bool> readStop{false}, hasAudio{false};
    std::atomic<bool> independentAudioReader{false};
    std::atomic<bool> audioInputDone{false};
    std::atomic<std::uint32_t> packetQueuePeak{0};
    std::atomic<Uint32> clockStart{0};
    std::atomic<Uint32> suspendedAt{0};
    std::atomic<std::uint32_t> decoded{0}, audioFrames{0}, loops{0};
    std::atomic<std::uint32_t> audioMissingFrames{0};
    std::atomic<std::uint32_t> handoffUs{0}, decoderCores{0}, converterCores{0};
    std::atomic<std::uint32_t> reorderedFrames{0}, timestampCorrections{0};
    std::atomic<std::uint32_t> convertedFrames{0}, lateDropped{0}, readUs{0}, decodeUs{0}, convertUs{0};
    std::atomic<std::uint32_t> nativeDecodeUs{0}, skippedNonreference{0};
    std::atomic<bool> hardwareDecoder{false};
    std::uint32_t uploadUs=0;
    std::uint32_t presented = 0;
    Uint32 firstFrameAt=0, lastPresentedAt=0, maxPresentGap=0, longPresentGaps=0;
    bool buffering=false,loop=true;
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

    static int Interrupt(void *user) {
        const auto &s=*static_cast<Impl *>(user);
        return s.cancel.load() || s.readStop.load();
    }
    // Each demux cursor has one owner. Audio reads independently so a full
    // compressed-video queue cannot starve the existing PCM ring. Seek/close
    // only run after joining both readers. No extra decoded images are queued.
    // Large packets may occupy an empty queue, but cannot accumulate together.
    struct PacketReader {
        static constexpr std::size_t Budget=512*1024, Capacity=96;
        Impl &owner;
        AVFormatContext *format;
        int audioIndex;
        std::function<bool(AVPacket *)> consumeAudio;
        SDL_mutex *mutex=SDL_CreateMutex();
        SDL_Thread *thread=nullptr;
        SDL_Thread *audioThread=nullptr;
        AVFormatContext *audioFormat=nullptr;
        std::array<AVPacket *,Capacity> packets{};
        std::size_t head=0,count=0,bytes=0;
        std::atomic<int> terminal{0};
#ifdef UFLOW_HOST_PREVIEW
        bool serial=std::getenv("UFLOW_VIDEO_TEST_SERIAL_READ") && std::atoi(std::getenv("UFLOW_VIDEO_TEST_SERIAL_READ"));
#endif
        PacketReader(Impl &s,AVFormatContext *f,int ai,std::function<bool(AVPacket *)> audio)
            :owner(s),format(f),audioIndex(ai),consumeAudio(std::move(audio)) {}
        ~PacketReader() {
            Finish();
            for(auto *&packet:packets)av_packet_free(&packet);
            if(mutex)SDL_DestroyMutex(mutex);
        }
        void Finish(bool drainAudio=false) {
            // EOF must not truncate the last PCM packets. Cancellation still
            // interrupts both file handles and a full PCM ring immediately.
            if(drainAudio && audioThread) { SDL_WaitThread(audioThread,nullptr);audioThread=nullptr; }
            owner.readStop.store(true);
            if(thread){SDL_WaitThread(thread,nullptr);thread=nullptr;}
            if(audioThread){SDL_WaitThread(audioThread,nullptr);audioThread=nullptr;}
            avformat_close_input(&audioFormat);
            owner.independentAudioReader.store(false);
            owner.readStop.store(false);
        }
        bool Start() {
#ifdef UFLOW_HOST_PREVIEW
            if(serial)return true;
#endif
            if(!mutex)return false;
            for(auto *&packet:packets) {
                if(!packet)packet=av_packet_alloc();
                if(!packet)return false;
            }
            terminal.store(0);
            owner.audioInputDone.store(false);
            if(audioIndex>=0 && consumeAudio) {
                // A second local demux cursor keeps audio independent of the
                // bounded compressed-video queue. In indexed MP4s discarded
                // video samples are skipped, not decoded or retained.
                audioFormat=avformat_alloc_context();
                if(!audioFormat)return false;
                audioFormat->interrupt_callback={Interrupt,&owner};
                AVDictionary *options=nullptr;
                av_dict_set(&options,"protocol_whitelist","file",0);
                const std::string input="file:"+owner.path;
                const int opened=avformat_open_input(&audioFormat,input.c_str(),nullptr,&options);
                av_dict_free(&options);
                if(opened<0 || static_cast<unsigned>(audioIndex)>=audioFormat->nb_streams ||
                    audioFormat->streams[audioIndex]->codecpar->codec_id!=format->streams[audioIndex]->codecpar->codec_id)
                    return false;
                for(unsigned i=0;i<audioFormat->nb_streams;++i)
                    audioFormat->streams[i]->discard=i==static_cast<unsigned>(audioIndex)?AVDISCARD_DEFAULT:AVDISCARD_ALL;
                owner.independentAudioReader.store(true);
                audioThread=SDL_CreateThreadWithStackSize(AudioWorker,"uflow-video-audio",128*1024,this);
                if(!audioThread)return false;
            }
            thread=SDL_CreateThreadWithStackSize(Worker,"uflow-video-read",128*1024,this);
            return thread!=nullptr;
        }
        static int AudioWorker(void *user) {
            auto &s=*static_cast<PacketReader *>(user);
            VideoStage("audio-reader-enter");
            AVPacket *packet=av_packet_alloc();
            int result=packet?0:AVERROR(ENOMEM);
            try {
#ifdef UFLOW_HOST_PREVIEW
                if(const char *delay=std::getenv("UFLOW_VIDEO_TEST_AUDIO_START_DELAY_MS")) {
                    const auto until=SDL_GetTicks()+static_cast<Uint32>(std::clamp(std::atoi(delay),0,2000));
                    while(!s.owner.cancel.load() && !s.owner.readStop.load() &&
                          static_cast<Sint32>(SDL_GetTicks()-until)<0)SDL_Delay(2);
                }
#endif
                while(packet && !s.owner.cancel.load() && !s.owner.readStop.load()) {
                    if(s.owner.suspended.load()){SDL_Delay(5);continue;}
                    result=av_read_frame(s.audioFormat,packet);
                    if(result<0)break;
                    if(packet->size<0 || packet->size>8*1024*1024){result=AVERROR_INVALIDDATA;break;}
                    if(packet->stream_index==s.audioIndex && !s.consumeAudio(packet)){result=AVERROR_EXIT;break;}
                    av_packet_unref(packet);
                }
                if(result==AVERROR_EOF && !s.owner.cancel.load() && !s.owner.readStop.load() &&
                    !s.consumeAudio(nullptr))result=AVERROR_EXIT;
            } catch(...) { result=AVERROR(ENOMEM); }
            av_packet_free(&packet);
            s.owner.audioInputDone.store(true);
            if(result<0 && result!=AVERROR_EOF && result!=AVERROR_EXIT)
                s.owner.DecodeError("AUDIO READ",result);
            VideoStage("audio-reader-exit");
            return 0;
        }
        int ReadOne(AVPacket *packet) {
            unsigned consumed=0;
            while(!owner.cancel.load() && !owner.readStop.load()) {
            const auto begin=SDL_GetPerformanceCounter();
            const int result=av_read_frame(format,packet);
#ifdef UFLOW_HOST_PREVIEW
            if(const char *delay=std::getenv("UFLOW_VIDEO_TEST_READ_DELAY_MS"))
                SDL_Delay(static_cast<Uint32>(std::clamp(std::atoi(delay),0,200)));
#endif
            owner.readUs.fetch_add(ElapsedUs(begin));
            if(result>=0 && (packet->size<0 || packet->size>8*1024*1024 || ++consumed>20000))
                return AVERROR_INVALIDDATA;
            if(result<0 || packet->stream_index!=audioIndex || !consumeAudio)return result;
            // Audio has its own AVFrame and runs while H264 waits for the UVD.
            // The existing two-second PCM ring provides bounded backpressure.
            const bool ok=audioFormat || consumeAudio(packet);
            av_packet_unref(packet);
            if(!ok)return AVERROR_EXIT;
            }
            return AVERROR_EXIT;
        }
        static int Worker(void *user) {
            auto &s=*static_cast<PacketReader *>(user);
            VideoStage("reader-enter");
            AVPacket *packet=av_packet_alloc();
            int result=packet?AVERROR_EXIT:AVERROR(ENOMEM);
            while(packet && !s.owner.cancel.load() && !s.owner.readStop.load()) {
                if(s.owner.suspended.load()){SDL_Delay(5);continue;}
                result=s.ReadOne(packet);
                if(result<0)break;
                if(packet->size<0 || packet->size>8*1024*1024){result=AVERROR_INVALIDDATA;break;}
                bool queued=false;
                while(!s.owner.cancel.load() && !s.owner.readStop.load()) {
                    SDL_LockMutex(s.mutex);
                    if(s.count<Capacity && (s.count==0 || s.bytes+packet->size<=Budget)) {
                        s.bytes+=packet->size;
                        av_packet_move_ref(s.packets[(s.head+s.count)%Capacity],packet);
                        ++s.count;queued=true;
                        s.owner.packetQueuePeak.store(std::max(s.owner.packetQueuePeak.load(),static_cast<std::uint32_t>(s.bytes)));
                    }
                    SDL_UnlockMutex(s.mutex);
                    if(queued)break;
                    SDL_Delay(2);
                }
                if(!queued){result=AVERROR_EXIT;break;}
            }
            av_packet_free(&packet);
            s.terminal.store(result<0?result:AVERROR_EXIT);
            VideoStage("reader-exit");
            return 0;
        }
        int Read(AVPacket *packet) {
#ifdef UFLOW_HOST_PREVIEW
            if(serial)return ReadOne(packet);
#endif
            while(owner.Running()) {
                SDL_LockMutex(mutex);
                if(count) {
                    av_packet_unref(packet);
                    bytes-=packets[head]->size;
                    av_packet_move_ref(packet,packets[head]);
                    head=(head+1)%Capacity;--count;
                    SDL_UnlockMutex(mutex);return 0;
                }
                const int result=terminal.load();
                SDL_UnlockMutex(mutex);
                if(result)return result;
                SDL_Delay(2);
            }
            return AVERROR_EXIT;
        }
    };
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
        while (length && !cancel.load() && !readStop.load()) {
            if(suspended.load()){SDL_Delay(5);continue;}
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
                if (!independentAudioReader.load() && !started.load() && !decoded.load() && !packetQueuePeak.load()) {
                    Fail("VIDEO HAS EXCESSIVE AUDIO PREROLL"); return false;
                }
                SDL_Delay(2);
            } else { if (bytes) bytes += part; length -= part; }
        }
        return !cancel.load() && !readStop.load();
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
            converted.nv12=gpuNV12 && pixelFormat==AV_PIX_FMT_NV12 && direct420;
#ifndef UFLOW_HOST_PREVIEW
            // CPU conversion is a fallback from the opted-in GPU-only decoder.
            if(!converted.nv12 && gpuNV12 && hardwareDecoder.load() && raw.buf[0])
                DCInvalidateRange(raw.buf[0]->data,raw.buf[0]->size);
#endif
            if(converted.nv12) {
                converted.width=raw.width;converted.height=raw.height;
            } else if(direct420) {
                converted.width=raw.width/(half420?2:1);
                converted.height=raw.height/(half420?2:1);
            } else {
                const double factor=std::min({1.0,640.0/raw.width,360.0/raw.height});
                converted.width=std::max(2,static_cast<int>(raw.width*factor)&~1);
                converted.height=std::max(2,static_cast<int>(raw.height*factor)&~1);
            }
            if(!converted.nv12) converted.bytes.resize(static_cast<std::size_t>(converted.width)*converted.height*4);
            converted.yuvMode=direct420 && (gpuColour || converted.nv12) ?
                1+(raw.colorspace==AVCOL_SPC_BT709?1:0)+(fullRange?2:0) : 0;
            const std::uint8_t *source[]={raw.data[0],raw.data[1],raw.data[2],raw.data[3]};
            const int *sourcePitch=raw.linesize;
            if(converted.native)av_frame_unref(converted.native.get());
            if(converted.nv12) {
                if(!converted.native)converted.native.reset(av_frame_alloc());
                if(!converted.native || av_frame_ref(converted.native.get(),&raw)<0) {
                    Fail("VIDEO PLANE LEASE FAILED");break;
                }
            } else if(converted.yuvMode) {
                VideoColor420::Pack(source[0],sourcePitch[0],source[1],sourcePitch[1],source[2],sourcePitch[2],
                    pixelFormat==AV_PIX_FMT_NV12,raw.width,raw.height,half420,
                    converted.bytes.data(),converted.width*4);
            } else if(direct420) {
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
        VideoStage("converter-enter");
        try { self.ConvertFrames(); }
        catch(const std::bad_alloc &) { self.Fail("NOT ENOUGH MEMORY FOR VIDEO CONVERSION"); }
        catch(...) { self.Fail("VIDEO CONVERSION FAILED"); }
        self.converting.store(false);
        VideoStage("converter-exit");
        return 0;
    }
    int OpenCodec(AVStream *stream, AVCodecContext **out, bool hardware) {
        const AVCodec *codec = nullptr;
#ifndef UFLOW_HOST_PREVIEW
        if (hardware && stream->codecpar->codec_id == AV_CODEC_ID_H264)
            codec = avcodec_find_decoder_by_name("h264_wiiu");
#else
        (void)hardware;
#endif
        if (!codec) codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (hardware) hardwareDecoder.store(codec && std::strcmp(codec->name,"h264_wiiu")==0);
        if (!codec) return AVERROR_DECODER_NOT_FOUND;
        if (!(*out = avcodec_alloc_context3(codec))) return AVERROR(ENOMEM);
        const int copied = avcodec_parameters_to_context(*out,stream->codecpar);
        if (copied < 0) return copied;
        (*out)->thread_count = 1;
        (*out)->pkt_timebase = stream->time_base;
        return avcodec_open2(*out,codec,nullptr);
    }
    int Run() {
        Decoder d;
        if (!d.frame || !d.audioFrame || !d.packet || !(d.format = avformat_alloc_context())) { Fail("NOT ENOUGH MEMORY FOR VIDEO"); return 0; }
        d.format->interrupt_callback = {Interrupt,this};
        d.format->probesize = 1024*1024;
        d.format->max_analyze_duration = AV_TIME_BASE;
        // Only local files; do not allow playlists/network indirections.
        AVDictionary *options = nullptr;
        av_dict_set(&options,"protocol_whitelist","file",0);
        const std::string localPath = "file:" + path; // Wii U's fs: is a device path, not an FFmpeg protocol.
        VideoStage("input-open-begin");
        const int opened = avformat_open_input(&d.format,localPath.c_str(),nullptr,&options);
        av_dict_free(&options);
        if (opened < 0 || !Running() || avformat_find_stream_info(d.format,nullptr) < 0) {
            Fail("VIDEO FILE UNAVAILABLE OR DAMAGED"); return 0;
        }
        VideoStage("input-open-ready");
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
        VideoStage("decoder-open-begin");
        const int videoOpened = OpenCodec(vs,&d.video,true);
        if (videoOpened < 0) { DecodeError("DECODER OPEN",videoOpened); return 0; }
#ifndef UFLOW_HOST_PREVIEW
        if(hardwareDecoder.load())uflow_h264_gpu_output(d.video,gpuNV12);
#endif
        VideoStage("decoder-open-ready");
        bool reorderDecoderOutput=hardwareDecoder.load();
#ifdef UFLOW_HOST_PREVIEW
        if(const char *test=std::getenv("UFLOW_VIDEO_TEST_REORDER")) reorderDecoderOutput=std::atoi(test)!=0;
#endif
        const double origin = vs->start_time == AV_NOPTS_VALUE ? 0 : vs->start_time*av_q2d(vs->time_base);
        const double fps = av_q2d(av_guess_frame_rate(d.format,vs,nullptr));
        const double frameStep = fps > 0 && fps <= 240 ? 1.0/fps : 1.0/30;
        // 1080p60 can exceed h264.rpl's sustained budget. Preserve all
        // reference pictures but omit disposable ones from the outset. With
        // direct planes, present every surviving picture at its original PTS;
        // applying the 30fps filter again would discard useful references too.
        bool referenceCadence=gpuNV12 && hardwareDecoder.load() &&
            fps>30 && fps<=240 && (parameters->width>1280 || parameters->height>720);
#ifdef UFLOW_HOST_PREVIEW
        if(std::getenv("UFLOW_VIDEO_TEST_REFERENCE_CADENCE") && fps>30 && fps<=240 &&
            (parameters->width>1280 || parameters->height>720))referenceCadence=true;
#endif
        // High-rate previews need not convert/upload more than 30 images/sec.
        const double displayStep = referenceCadence ? frameStep : std::max(frameStep,1.0/30);
        AVStream *as = ai >= 0 ? d.format->streams[ai] : nullptr;
        if (decodeAudio && volume > 0 && as && OpenCodec(as,&d.audio,false) >= 0) {
            const auto layout = d.audio->channel_layout ? d.audio->channel_layout : av_get_default_channel_layout(d.audio->channels);
            if (d.audio->sample_rate > 0 && d.audio->sample_rate <= 192000 && d.audio->channels <= 8) {
                d.resample = swr_alloc_set_opts(nullptr,AV_CH_LAYOUT_STEREO,AV_SAMPLE_FMT_S16,kRate,
                    layout,d.audio->sample_fmt,d.audio->sample_rate,0,nullptr);
                if (!d.resample || swr_init(d.resample) < 0) swr_free(&d.resample);
            }
            if (!d.resample) avcodec_free_context(&d.audio);
        } else avcodec_free_context(&d.audio);
        hasAudio.store(d.audio!=nullptr);
        // Snapshot demux metadata before the read worker owns the format.
        const double videoTimeBase=av_q2d(vs->time_base);
        const double audioTimeBase=as?av_q2d(as->time_base):0;
        const auto defaultAspect=av_guess_sample_aspect_ratio(d.format,vs,nullptr);
        const auto reorderDepth=static_cast<std::size_t>(std::clamp(parameters->video_delay,0,16));
        std::vector<std::uint8_t> pcm;
        ReorderFrames reorder;
        double nextFrame = 0, fallbackPts = 0, lastVideo = 0;
        double lastDecodedPts=-std::numeric_limits<double>::infinity();
        std::int64_t audioWritten = 0;
        const auto emitVideo=[&](AVFrame *frame, double pts) -> bool {
            if((referenceCadence || VideoPreviewFrameWanted(pts,fps)) && pts+.001>=nextFrame) {
                if(!frame->buf[0]) { Fail("VIDEO FRAME HAS NO OWNED BUFFER"); return false; }
                const auto sar=defaultAspect.num>0 && defaultAspect.den>0 ? defaultAspect : frame->sample_aspect_ratio;
                const double aspect=std::clamp(static_cast<double>(frame->width)/frame->height*
                    (sar.num>0 && sar.den>0 ? av_q2d(sar) : 1.0),.25,4.0);
                if(!QueueRaw(frame,std::max(0.0,pts),aspect)) return false;
                nextFrame=pts+displayStep-.001;
            }
            av_frame_unref(frame); return true;
        };
        const auto drainOrder=[&](double watermark, bool flush, bool missingDts=false) -> bool {
            const auto depth=reorderDepth;
            while(auto ready=reorder.order.PopReady(watermark,flush || (missingDts && reorder.order.Size()>depth))) {
                if(!emitVideo(reorder.frames[ready->slot],ready->pts)) return false;
                reorder.used[ready->slot]=false;
            }
            return true;
        };
        auto receive = [&](AVCodecContext *codec, bool video) -> bool {
            AVFrame *frame=video?d.frame:d.audioFrame;
            int result = AVERROR(EAGAIN);
            while (Running() && (result = Receive(codec,frame)) >= 0) {
                if (video) {
                    decoded.fetch_add(1);
#ifndef UFLOW_HOST_PREVIEW
                    decoderCores.fetch_or(1u<<OSGetCoreId());
#endif
#ifdef UFLOW_HOST_PREVIEW
                    if(const char *delay=std::getenv("UFLOW_VIDEO_TEST_DECODE_DELAY_MS"))
                        SDL_Delay(static_cast<Uint32>(std::clamp(std::atoi(delay),0,200)));
                    if(decoded.load()%20==0)
                        if(const char *delay=std::getenv("UFLOW_VIDEO_TEST_DECODE_STALL_MS"))
                            SDL_Delay(static_cast<Uint32>(std::clamp(std::atoi(delay),0,1000)));
#endif
                    if (frame->width <= 0 || frame->height <= 0 || frame->width > kMaxWidth || frame->height > kMaxHeight) {
                        Fail("VIDEO CHANGED TO AN UNSUPPORTED RESOLUTION"); return false;
                    }
                    // Immediate h264.rpl output is decode order. FFmpeg's best-
                    // effort heuristic can replace its non-monotonic PTS with
                    // DTS; that labels future reference pictures as earlier ones.
                    const bool reorderHardware=reorderDecoderOutput && frame->pts!=AV_NOPTS_VALUE;
                    const auto timestamp = reorderHardware ? frame->pts :
                        frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
                    const double pts = timestamp == AV_NOPTS_VALUE ? fallbackPts : timestamp*videoTimeBase-origin;
                    fallbackPts = pts+frameStep;
                    lastVideo = std::max(lastVideo,pts+frameStep);
                    if(reorderHardware) {
                        if(pts<lastDecodedPts) reorderedFrames.fetch_add(1);
                        if(frame->best_effort_timestamp!=AV_NOPTS_VALUE && frame->best_effort_timestamp!=timestamp)
                            timestampCorrections.fetch_add(1);
                        lastDecodedPts=pts;
                        const bool missingDts=frame->pkt_dts==AV_NOPTS_VALUE;
                        const double watermark=missingDts ? -std::numeric_limits<double>::infinity() :
                            frame->pkt_dts*videoTimeBase-origin;
                        if(!reorder.Push(frame,pts)) { Fail("VIDEO REORDER BUFFER LIMIT EXCEEDED"); return false; }
                        if(!drainOrder(watermark,false,missingDts)) return false;
                    } else if(!emitVideo(frame,pts)) return false;
                } else {
                    const int samples = static_cast<int>(av_rescale_rnd(swr_get_delay(d.resample,codec->sample_rate)+frame->nb_samples,
                        kRate,codec->sample_rate,AV_ROUND_UP));
                    if (samples <= 0 || samples > kRate*2) { Fail("INVALID VIDEO AUDIO FRAME"); return false; }
                    pcm.resize(static_cast<std::size_t>(samples)*kAudioFrameBytes);
                    std::uint8_t *output[] = {pcm.data()};
                    const int n = swr_convert(d.resample,output,samples,const_cast<const std::uint8_t **>(frame->extended_data),frame->nb_samples);
                    if (n < 0) { Fail("VIDEO AUDIO DECODE FAILED"); return false; }
                    std::int64_t at = audioWritten;
                    if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                        at = static_cast<std::int64_t>((frame->best_effort_timestamp*audioTimeBase-origin)*kRate);
                    // Honor starting offsets and discontinuities; never allocate for a gap.
                    if (at > audioWritten+kRate*2) { Fail("VIDEO AUDIO TIMESTAMPS ARE INVALID"); return false; }
                    if (at > audioWritten && !QueueAudio(nullptr,static_cast<std::size_t>(at-audioWritten)*kAudioFrameBytes)) return false;
                    const int trim = static_cast<int>(std::clamp<std::int64_t>(audioWritten-at,0,n));
                    if (!QueueAudio(pcm.data()+trim*kAudioFrameBytes,(n-trim)*kAudioFrameBytes)) return false;
                    audioWritten = std::max(audioWritten,at+n);
                }
                av_frame_unref(frame);
            }
            av_frame_unref(frame);
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF && !cancel.load()) {
                DecodeError(video ? "FRAME DECODE" : "AUDIO DECODE",result); return false;
            }
            return !cancel.load();
        };
        // Declare the reader after everything used by its callback: joining it
        // must precede destruction of the audio frame, PCM and receive closure.
        PacketReader reader(*this,d.format,d.audio?ai:-1,[&](AVPacket *packet) {
            int sent=Send(d.audio,packet);
            if(sent==AVERROR(EAGAIN)) {
                if(!receive(d.audio,false))return false;
                sent=Send(d.audio,packet);
            }
            if(sent<0){DecodeError("AUDIO PACKET",sent);return false;}
            return receive(d.audio,false);
        });
        if(!reader.Start()){Fail("VIDEO READER COULD NOT START");return 0;}
        unsigned packetsWithoutVideo = 0;
#ifndef UFLOW_HOST_PREVIEW
        auto *budgetThread=OSGetCurrentThread();
        const auto originalPriority=OSGetThreadPriority(budgetThread);
        auto appliedPriority=originalPriority;
        auto budgetCpu=OSGetThreadCoreTime(budgetThread,7);
        auto budgetAt=SDL_GetTicks();
        auto budgetDecoded=decoded.load();
        auto budgetDecodeUs=decodeUs.load();
#endif
        while (Running()) {
#ifndef UFLOW_HOST_PREVIEW
            // UVD completion must be consumed promptly even while rendering is
            // runnable. WUT user priority 14 is native 78: ahead of the UI's 80,
            // below SDL audio's 77. Keep affinity and system threads untouched.
            const auto desiredPriority=decoderWakePriority.load()?std::min(originalPriority,14):originalPriority;
            if(desiredPriority!=appliedPriority && OSSetThreadPriority(budgetThread,desiredPriority)) {
                appliedPriority=desiredPriority;
                OSReport("uFlow: video-wake-priority user=%d\n",appliedPriority);
            }
#endif
            const int read = reader.Read(d.packet);
            if (read < 0) {
                reader.Finish(read==AVERROR_EOF && decoded.load()>0);
                if (read != AVERROR_EOF) { if(read!=AVERROR_EXIT)Fail("VIDEO READ FAILED"); break; }
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
                if (!loop) { completed.store(true); break; }
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
                if(!reader.Start()){Fail("VIDEO READER COULD NOT RESTART");break;}
                continue;
            }
            AVCodecContext *codec = d.packet->stream_index == vi ? d.video : d.packet->stream_index == ai ? d.audio : nullptr;
            if (++packetsWithoutVideo > 20000 || d.packet->size > 8*1024*1024) { Fail("VIDEO STREAM LIMIT EXCEEDED"); break; }
            if (codec) {
                // When decoding falls behind, skip only pictures that cannot
                // affect later frames. The native adapter asks h264.rpl which
                // packets are safe; references and packet timestamps survive.
                if (codec==d.video && (hardwareDecoder.load() || referenceCadence)) {
                    const auto timestamp=d.packet->dts!=AV_NOPTS_VALUE?d.packet->dts:d.packet->pts;
                    const double packetTime=timestamp==AV_NOPTS_VALUE?Clock():timestamp*videoTimeBase-origin;
                    const bool outsideCadence=d.packet->pts!=AV_NOPTS_VALUE &&
                        !VideoPreviewFrameWanted(d.packet->pts*videoTimeBase-origin,fps);
                    codec->skip_frame=referenceCadence || outsideCadence || (started.load() && packetTime<Clock()-.10)
                        ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
                }
                int sent = Send(codec,d.packet);
                if (sent == AVERROR(EAGAIN)) {
                    if (!receive(codec,codec==d.video)) break;
                    sent = Send(codec,d.packet);
                }
                if (sent < 0) { DecodeError(codec==d.video ? "PACKET DECODE" : "AUDIO PACKET",sent); break; }
                if (!receive(codec,codec==d.video)) break; // Preserve the specific failure.
#ifndef UFLOW_HOST_PREVIEW
                // Query scheduler accounting through coreinit: the legacy
                // OSThread coreTimeConsumedNs field is not populated here.
                const auto budgetNow=SDL_GetTicks();
                if(budgetNow-budgetAt>=5000) {
                    const auto cpuNow=OSGetThreadCoreTime(budgetThread,7);
                    const auto framesNow=decoded.load();
                    const auto decodeNow=decodeUs.load();
                    OSReport("uFlow: video-decode-budget wall_ms=%u cpu_ms=%u decode_ms=%u frames=%u\n",
                        budgetNow-budgetAt,static_cast<unsigned>(OSTicksToMilliseconds(cpuNow-budgetCpu)),
                        (decodeNow-budgetDecodeUs)/1000,framesNow-budgetDecoded);
                    budgetAt=budgetNow;budgetCpu=cpuNow;budgetDecoded=framesNow;budgetDecodeUs=decodeNow;
                }
#endif
                if(codec==d.video && hardwareDecoder.load()) {
#ifndef UFLOW_HOST_PREVIEW
                    uint64_t nativeUs=0, skipped=0;
                    uflow_h264_metrics(codec,&nativeUs,&skipped);
                    nativeDecodeUs.store(static_cast<std::uint32_t>(nativeUs));
                    skippedNonreference.store(static_cast<std::uint32_t>(skipped));
#endif
                    // A safely skipped packet still advances the presentation
                    // watermark, allowing buffered reference pictures to leave.
                    if(d.packet->dts!=AV_NOPTS_VALUE && !drainOrder(d.packet->dts*videoTimeBase-origin,false)) break;
                }
                if (codec == d.video) packetsWithoutVideo = 0;
            }
            av_packet_unref(d.packet);
        }
        return 0;
    }
    static int Worker(void *user) {
        auto &self = *static_cast<Impl *>(user);
        VideoStage("decoder-enter");
        try { self.Run(); }
        catch (const std::bad_alloc &) { self.Fail("NOT ENOUGH MEMORY FOR VIDEO"); }
        catch (...) { self.Fail("VIDEO PLAYBACK FAILED"); }
        VideoStage("decoder-resources-released");
        self.producerDone.store(true);
        if(self.converter) { SDL_WaitThread(self.converter,nullptr); self.converter=nullptr; }
#ifdef UFLOW_HOST_PREVIEW
        if (const char *delay=std::getenv("UFLOW_VIDEO_TEST_CLEANUP_DELAY_MS"))
            SDL_Delay(static_cast<Uint32>(std::clamp(std::atoi(delay),0,3000)));
#endif
        VideoStage("decoder-exit");
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
bool VideoPlayer::Start(const std::string &path, int volume, bool loop) {
    Stop();
    av_log_set_level(AV_LOG_ERROR);
#ifndef UFLOW_HOST_PREVIEW
    av_log_set_callback(VideoLog);
#endif
    auto &s = *impl_;
    if (s.thread) return false; // Previous cancelled decoder is still retiring.
    if (!s.frameMutex || !s.audioMutex) return false;
#ifndef UFLOW_HOST_PREVIEW
    s.gpuColour=UFlow_YuvShadersReady()!=0;
    SDL_RendererInfo rendererInfo{};
    s.gpuNV12=false;
    if(s.gpuColour && SDL_GetRendererInfo(s.renderer,&rendererInfo)==0)
        for(unsigned i=0;i<rendererInfo.num_texture_formats && i<16;++i)
            if(rendererInfo.texture_formats[i]==SDL_PIXELFORMAT_NV12)s.gpuNV12=true;
    VideoStage(s.gpuColour?"gpu-colour-ready":"cpu-colour-fallback");
#else
    s.gpuNV12=std::getenv("UFLOW_VIDEO_TEST_NV12")!=nullptr;
#endif
    for(auto &slot:s.rawFrames) {
        if(!slot.frame) slot.frame=av_frame_alloc();
        if(!slot.frame) return false;
    }
    s.path = path; s.volume = std::clamp(volume,0,100); s.loop=loop;
    s.cancel.store(false); s.suspended.store(false); s.started.store(false); s.done.store(false);
    s.completed.store(false);
    s.readStop.store(false);s.hasAudio.store(false);s.packetQueuePeak.store(0);
    s.producerDone.store(false); s.converting.store(false);
    s.error.clear();
    s.decoded.store(0); s.audioFrames.store(0); s.loops.store(0); s.presented=0;
    s.audioMissingFrames.store(0);
    s.handoffUs.store(0); s.decoderCores.store(0); s.converterCores.store(0);
    s.reorderedFrames.store(0); s.timestampCorrections.store(0);
    s.buffering=false; s.firstFrameAt=s.lastPresentedAt=s.maxPresentGap=s.longPresentGaps=0;
    s.convertedFrames.store(0); s.lateDropped.store(0); s.readUs.store(0); s.decodeUs.store(0);
    s.convertUs.store(0); s.uploadUs=0; s.hardwareDecoder.store(false);
    s.nativeDecodeUs.store(0); s.skippedNonreference.store(0);
    VideoStage("audio-open-begin");
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
    VideoStage("audio-open-ready");
    s.decodeAudio=s.audioOpened;
    s.converter=SDL_CreateThreadWithStackSize(Impl::ConverterWorker,"uflow-video-color",256*1024,&s);
    if(!s.converter) { Stop(); return false; }
    s.thread = SDL_CreateThreadWithStackSize(Impl::Worker,"uflow-video",256*1024,&s);
    if (!s.thread) {
        s.cancel.store(true); s.producerDone.store(true);
        SDL_WaitThread(s.converter,nullptr); s.converter=nullptr; Stop(); return false;
    }
    VideoStage("workers-started");
    return true;
}
void VideoPlayer::Stop(bool retainFrame) {
    auto &s = *impl_;
    s.cancel.store(true);
    if (s.audioOpened) { Mix_SetPostMix(nullptr,nullptr); Mix_CloseAudio(); s.audioOpened = false; }
    if (s.audioInitialized) { SDL_QuitSubSystem(SDL_INIT_AUDIO); s.audioInitialized = false; }
    s.retainedFrame=retainFrame && s.texture;
    for (auto *&texture : s.texturePool) {
        if(s.retainedFrame && texture==s.texture)continue;
        if(texture) SDL_DestroyTexture(texture);
        texture=nullptr;
    }
    if(!s.retainedFrame) {s.texture=nullptr;s.textureWidth=s.textureHeight=0;}
    s.textureIndex=0;
    // The worker owns its queues until it exits. Cancellation unhooks audio and
    // hides video immediately without a join that could freeze the main screen.
    if (s.thread && !s.done.load()) return;
    if (s.thread) { SDL_WaitThread(s.thread,nullptr); s.thread = nullptr; }
    for (auto &frame : s.frames) frame = {};
    // Both workers have exited; pool references may now safely be released here.
    for (auto &slot : s.rawFrames) { if(slot.frame) av_frame_unref(slot.frame); slot.state=RawFrame::Free; }
    s.rawHead=s.rawCount=0;
    const double aspect=s.displayed.aspect;
    s.displayed = {}; if(s.retainedFrame)s.displayed.aspect=aspect;
    s.head = s.count = s.audioHead = s.audioCount = 0;
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
    if (impl_->cancel.load()) Stop(impl_->retainedFrame);
    return impl_->thread == nullptr;
}
bool VideoPlayer::Active() const { return impl_->thread != nullptr && !impl_->cancel.load(); }
bool VideoPlayer::Ready() const { return impl_->texture != nullptr; }
bool VideoPlayer::Finished() const {return Active() && impl_->completed.load();}
bool VideoPlayer::Update() {
    auto &s = *impl_;
    if (!s.thread || s.cancel.load() || s.suspended.load()) return false;
    bool changed = false, beginPlayback = false;
    SDL_LockMutex(s.frameMutex);
    if (s.count && !s.started.load()) {
        if(!s.buffering) { s.buffering=true; s.firstFrameAt=SDL_GetTicks(); }
        // A video timeout must not start an empty audio ring on slow storage.
        // The independent audio cursor can fill while video queues are full;
        // EOF permits short or empty sound tracks without a fixed-size reserve.
        SDL_LockMutex(s.audioMutex);
        const bool audioReady=!s.hasAudio.load() || s.audioCount>=kRate*kAudioFrameBytes/2 || s.audioInputDone.load();
        SDL_UnlockMutex(s.audioMutex);
        const bool videoReady=s.count>=3 || SDL_GetTicks()-s.firstFrameAt>=400 || s.producerDone.load();
        if((videoReady && audioReady) || (!s.independentAudioReader.load() &&
            (SDL_GetTicks()-s.firstFrameAt>=400 || s.producerDone.load()))) {
            beginPlayback=true;
        }
    }
    if(!s.started.load() && !beginPlayback) { SDL_UnlockMutex(s.frameMutex); return false; }
    const double now = s.Clock();
    while (s.count && (beginPlayback || s.frames[s.head].seconds <= now+.01)) {
        std::swap(s.displayed,s.frames[s.head]);
        s.head = (s.head+1)%s.frames.size(); --s.count; changed = true;
        if(beginPlayback)break; // Present the opening picture, never skip it during setup.
    }
    SDL_UnlockMutex(s.frameMutex);
    if (!changed) return false;
    if(!s.presented) VideoStage("first-upload-begin");
#ifdef UFLOW_HOST_PREVIEW
    if(beginPlayback) if(const char *delay=std::getenv("UFLOW_VIDEO_TEST_FIRST_UPLOAD_DELAY_MS"))
        SDL_Delay(static_cast<Uint32>(std::clamp(std::atoi(delay),0,1000)));
#endif
    const auto uploadStart=SDL_GetPerformanceCounter();
    if (s.displayed.width != s.textureWidth || s.displayed.height != s.textureHeight ||
        s.displayed.yuvMode!=s.textureYuvMode || s.displayed.nv12!=s.textureNV12) {
        s.texture=nullptr;
        for (auto *&texture : s.texturePool) { if(texture) SDL_DestroyTexture(texture); texture=nullptr; }
        for (auto *&texture : s.texturePool) {
            texture=SDL_CreateTexture(s.renderer,s.displayed.nv12?SDL_PIXELFORMAT_NV12:SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING,s.displayed.width,s.displayed.height);
            if (!texture) { s.Fail("VIDEO TEXTURE ALLOCATION FAILED"); return false; }
#ifndef UFLOW_HOST_PREVIEW
            if(UFlow_SetYuvTexture(texture,s.displayed.yuvMode)!=0) {
                s.Fail("VIDEO COLOUR SHADER UNAVAILABLE");return false;
            }
#endif
            SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);
        }
        s.textureWidth = s.displayed.width; s.textureHeight = s.displayed.height;
        s.textureYuvMode=s.displayed.yuvMode;
        s.textureNV12=s.displayed.nv12;
    }
    // Do not overwrite the texture GX2 is still reading from the previous frame.
    s.texture=s.texturePool[s.textureIndex]; s.textureIndex=(s.textureIndex+1)%s.texturePool.size();
    const auto *native=s.displayed.native.get();
    int uploaded=-1;
#ifndef UFLOW_HOST_PREVIEW
    if(s.displayed.nv12 && native && native->buf[0]) {
        const auto base=reinterpret_cast<uintptr_t>(native->buf[0]->data);
        const auto chroma=reinterpret_cast<uintptr_t>(native->data[1]);
        if(native->buf[0]->size>0 && native->data[0]==native->buf[0]->data && chroma>=base &&
           chroma-base<=static_cast<std::size_t>(native->buf[0]->size)) {
            if(auto *lease=av_frame_clone(native)) {
                uploaded=UFlow_BindNV12(s.texture,native->data[0],native->linesize[0],chroma-base,
                    native->data[1],native->linesize[1],native->buf[0]->size-(chroma-base),ReleaseTextureFrame,lease);
                if(uploaded!=0)av_frame_free(&lease);
            }
        }
        if(!s.presented)OSReport("uFlow: video-plane-import active=%u\n",uploaded==0);
        // The upload fallback may copy rows on the CPU. Imported planes never
        // pass here, so don't sweep a full 1080p frame through CPU cache upkeep.
        if(uploaded!=0 && s.hardwareDecoder.load())
            DCInvalidateRange(native->buf[0]->data,native->buf[0]->size);
    }
#endif
    if(uploaded!=0)uploaded=s.displayed.nv12 ?
        SDL_UpdateNVTexture(s.texture,nullptr,native->data[0],native->linesize[0],native->data[1],native->linesize[1]) :
        SDL_UpdateTexture(s.texture,nullptr,s.displayed.bytes.data(),s.displayed.width*4);
    // Imported textures own a separate frame lease until their GPU fence.
    // Copied textures own their pixels. Either permits releasing this reference.
    if(native)av_frame_unref(s.displayed.native.get());
    if (uploaded != 0) {
        s.Fail("VIDEO TEXTURE UPDATE FAILED");return false;
    }
    // GPU texture allocation/upload can stall on the first frame. Keep audio
    // and the presentation clock stopped until it is ready, or the converter
    // drops opening frames and audio drains while nothing can be displayed.
    if(beginPlayback) {
        s.clockStart.store(SDL_GetTicks());s.started.store(true);s.buffering=false;
        s.lastPresentedAt=0;
    }
    s.uploadUs+=Impl::ElapsedUs(uploadStart);
    const auto at=SDL_GetTicks();
    if(s.lastPresentedAt) {
        const auto gap=at-s.lastPresentedAt;
        s.maxPresentGap=std::max(s.maxPresentGap,gap);
        if(gap>50) ++s.longPresentGaps;
    }
    s.lastPresentedAt=at;
    if(!s.presented) VideoStage("first-upload-ready");
    ++s.presented;
    return true;
}
bool VideoPlayer::Draw(int width, int height, Uint8 opacity) const {
    const auto &s = *impl_;
    if (!s.texture) return false;
    SDL_SetRenderDrawBlendMode(s.renderer,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(s.renderer,0,0,0,opacity);
    const double aspect = s.displayed.aspect;
    const int w = std::min(width,static_cast<int>(height*aspect));
    const int h = std::min(height,static_cast<int>(width/aspect));
    const SDL_Rect destination{(width-w)/2,(height-h)/2,w,h};
    // Only fade black into the letterbox bars. Filling underneath the picture
    // too would darken the underlying scene twice during the crossfade.
    const SDL_Rect bars[]={{0,0,width,destination.y},{0,destination.y+h,width,height-destination.y-h},
        {0,destination.y,destination.x,h},{destination.x+w,destination.y,width-destination.x-w,h}};
    SDL_RenderFillRects(s.renderer,bars,4);
    SDL_SetTextureBlendMode(s.texture,SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(s.texture,opacity);
    const bool result=SDL_RenderCopy(s.renderer,s.texture,nullptr,&destination)==0;
    SDL_SetTextureAlphaMod(s.texture,255);
    return result;
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
        impl_->reorderedFrames.load(),impl_->timestampCorrections.load(),
        impl_->nativeDecodeUs.load(),impl_->skippedNonreference.load(),impl_->packetQueuePeak.load(),impl_->displayed.yuvMode!=0,impl_->displayed.nv12};
}
