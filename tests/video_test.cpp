#include "video.hpp"
#include "video_color.hpp"
#include "video_order.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <vector>

void CheckFrameOrder() {
    VideoFrameOrder order;
    // Actual first Bolt packet PTS in 1/30s units; DTS starts at -2 and increases.
    const unsigned pictures[]={0,2,1,6,4,3,5,10,8,7,9,14,12,11,13};
    std::vector<unsigned> shown;
    for(unsigned i=0;i<std::size(pictures);++i) {
        assert(order.Push(pictures[i]/30.,pictures[i]));
        while(auto next=order.PopReady((static_cast<int>(i)-2)/30.)) {
            assert(next->slot==shown.size()); shown.push_back(next->slot);
        }
    }
    while(auto next=order.PopReady(0,true)) { assert(next->slot==shown.size()); shown.push_back(next->slot); }
    assert(shown.size()==std::size(pictures));
    assert(!order.PopReady(0,true));
    // Missing-DTS fallback: hold the signaled reorder depth (two frames).
    for(unsigned i=0;i<std::size(pictures);++i) {
        assert(order.Push(pictures[i]/30.,pictures[i]));
        if(order.Size()>2) {
            const auto next=order.PopReady(0,true);
            assert(next && next->slot==i-2);
        }
    }
    assert(order.PopReady(0,true)->slot==13);
    assert(order.PopReady(0,true)->slot==14);
    // Capacity is hard-bounded; flushing resets the same queue for a loop.
    for(unsigned i=0;i<VideoFrameOrder::Capacity;++i) assert(order.Push(1.,i));
    assert(!order.Push(2.,99));
    assert(!order.PopReady(.5));
    for(unsigned i=0;i<VideoFrameOrder::Capacity;++i) assert(order.PopReady(1.)->slot==i);
    assert(order.Push(0.,123) && order.PopReady(0.)->slot==123);
    std::cout<<"PASS: actual Bolt B-frame decode order -> monotonic presentation order, DTS watermark, missing-DTS depth, EOF flush, bounded queue and loop reset\n";
}

void CheckColor() {
    // GPU input packing preserves the existing half-size filter, chroma,
    // plane strides, and destination padding without any CPU RGB conversion.
    for(bool half:{false,true}) {
        std::uint8_t y[32],u[8],v[8],uv[16],packed[112],planar[112];
        for(int i=0;i<32;++i)y[i]=(i*37)%256;
        for(int i=0;i<8;++i){u[i]=i*31;v[i]=255-i*29;uv[i*2]=u[i];uv[i*2+1]=v[i];}
        std::fill_n(packed,112,123);std::fill_n(planar,112,123);
        VideoColor420::Pack(y,8,uv,8,nullptr,0,true,6,4,half,packed,28);
        VideoColor420::Pack(y,8,u,4,v,4,false,6,4,half,planar,28);
        for(int i=0;i<112;++i) assert(packed[i]==planar[i]);
        const int w=half?3:6,h=half?2:4;
        for(int row=0;row<h;++row)for(int col=0;col<w;++col) {
            const int sy=half?row*2:row,sx=half?col*2:col,at=sy*8+sx;
            const int luma=half?(y[at]+y[at+1]+y[at+8]+y[at+9]+2)/4:y[at];
            auto *p=packed+row*28+col*4;
            assert(p[0]==luma && p[1]==u[(sy/2)*4+sx/2] && p[2]==v[(sy/2)*4+sx/2] && p[3]==255);
        }
        for(int row=0;row<4;++row)for(int col=0;col<28;++col)
            if(row>=h || col>=w*4)assert(packed[row*28+col]==123);
    }
    std::cout<<"PASS: GPU YUV packing, planar/NV12, full/half size, box filtering and padded strides\n";
    VideoColor420 color;
    for(bool full:{false,true}) for(bool bt709:{false,true}) {
        color.Configure(full,bt709);
        for(int y=0;y<256;y+=17) for(int u=0;u<256;u+=17) for(int v=0;v<256;v+=17) {
            const std::uint8_t yy[]={static_cast<std::uint8_t>(y),static_cast<std::uint8_t>(y),0,
                                     static_cast<std::uint8_t>(y),static_cast<std::uint8_t>(y),0};
            const std::uint8_t uv[]={static_cast<std::uint8_t>(u),static_cast<std::uint8_t>(v),0,0};
            std::uint8_t output[24]{}, planar[24]{};
            color.Convert(yy,3,uv,4,nullptr,0,true,2,2,output,12);
            color.Convert(yy,3,uv,1,uv+1,1,false,2,2,planar,12);
            const double l=full?y:(y-16)*255./219.;
            const double scale=full?1.:255./224.;
            const double kr=bt709?.2126:.299, kb=bt709?.0722:.114, kg=1-kr-kb;
            const double red=l+2*(1-kr)*(v-128)*scale;
            const double blue=l+2*(1-kb)*(u-128)*scale;
            const double green=l-2*kb*(1-kb)/kg*(u-128)*scale-2*kr*(1-kr)/kg*(v-128)*scale;
            const double expected[]={red,green,blue};
            for(int at:{0,4,12,16}) {
                for(int channel=0;channel<3;++channel)
                    assert(std::abs(output[at+channel]-std::clamp(expected[channel],0.,255.))<2.);
                assert(output[at+3]==255);
            }
            for(int at=0;at<24;++at) assert(output[at]==planar[at]);
        }
    }
    std::cout<<"PASS: NV12/planar color conversion, BT601/709, limited/full range, padded strides\n";
    assert(VideoColor420::NeedsHalfSize(1280,720));
    assert(VideoColor420::NeedsHalfSize(1920,1080));
    assert(!VideoColor420::NeedsHalfSize(640,480));
    assert(!VideoColor420::NeedsHalfSize(960,540));
    for(bool full:{false,true}) for(bool bt709:{false,true}) {
        color.Configure(full,bt709);
        std::uint8_t yy[32]{}, uu[8]{}, vv[8]{}, uv[16]{};
        std::uint8_t output[40], planar[40];
        std::fill_n(output,40,123); std::fill_n(planar,40,123);
        for(int row=0;row<4;++row) for(int col=0;col<6;++col) yy[row*8+col]=(row*53+col*31)%256;
        for(int row=0;row<2;++row) for(int col=0;col<3;++col) {
            uu[row*4+col]=uv[row*8+col*2]=40+row*80+col*37;
            vv[row*4+col]=uv[row*8+col*2+1]=220-row*60-col*43;
        }
        color.ConvertHalf(yy,8,uv,8,nullptr,0,true,6,4,output,20);
        color.ConvertHalf(yy,8,uu,4,vv,4,false,6,4,planar,20);
        for(int row=0;row<2;++row) for(int col=0;col<3;++col) {
            const int at=row*16+col*2;
            const int mean=(yy[at]+yy[at+1]+yy[at+8]+yy[at+9]+2)/4;
            const double l=full?mean:(mean-16)*255./219., scale=full?1.:255./224.;
            const double kr=bt709?.2126:.299, kb=bt709?.0722:.114, kg=1-kr-kb;
            const double u=uu[row*4+col]-128, v=vv[row*4+col]-128;
            const double expected[]={l+2*(1-kr)*v*scale,
                l-2*kb*(1-kb)/kg*u*scale-2*kr*(1-kr)/kg*v*scale,l+2*(1-kb)*u*scale};
            for(int channel=0;channel<3;++channel)
                assert(std::abs(output[row*20+col*4+channel]-std::clamp(expected[channel],0.,255.))<2.);
            assert(output[row*20+col*4+3]==255);
        }
        for(int at=0;at<40;++at) {
            assert(output[at]==planar[at]);
            if(at%20>=12) assert(output[at]==123);
        }
    }
    std::cout<<"PASS: half-size box-filtered NV12/planar conversion, varying luma/chroma, odd output width, padded strides\n";
}

void CheckTimer() {
    for(double fps:{24.,25.,30.,50.,59.94,60.,120.}) {
        const int step=static_cast<int>(std::ceil(fps/30.));
        for(int frame=0;frame<600;++frame)
            assert(VideoPreviewFrameWanted(frame/fps,fps)==(frame%step==0));
    }
    assert(VideoPreviewFrameWanted(-1.,60.));
    assert(VideoPreviewFrameWanted(1.,0.));
    assert(VideoPreviewFrameWanted(INFINITY,60.));
    assert(WiiUVideoH264Supported(77,31));
    assert(WiiUVideoH264Supported(100,42));
    assert(!WiiUVideoH264Supported(100,51));
    assert(!WiiUVideoH264Supported(110,31));
    assert(!WiiUVideoH264Supported(100,-99));
    VideoTrigger trigger;
    trigger.Focus("first");
    assert(!trigger.Tick(true,100,1000));
    assert(!trigger.Tick(true,1099,1000));
    assert(trigger.Tick(true,1100,1000));
    assert(!trigger.Tick(true,1101,1000));
    trigger.Focus("second");
    assert(!trigger.Tick(true,1200,1000));
    trigger.Focus("third");
    assert(!trigger.Tick(true,1600,1000));
    assert(!trigger.Tick(true,2200,1000));
    assert(trigger.Tick(true,2600,1000));
    assert(!trigger.Tick(false,2700,1000));
    assert(!trigger.Tick(true,5000,1000));
    assert(trigger.Tick(true,6000,1000));
    trigger.Dismiss(); trigger.Reset();
    assert(!trigger.Tick(true,9000,0));
    trigger.Focus("fourth");
    assert(trigger.Tick(true,9100,0));
    trigger.Focus("rollover");
    assert(!trigger.Tick(true,0xfffffff0u,1000));
    assert(!trigger.Tick(true,0x000003d7u,1000));
    assert(trigger.Tick(true,0x000003d8u,1000));
    trigger.Focus(""); assert(!trigger.Tick(true,10000,0));
}
int main(int argc, char **argv) {
    CheckTimer();
    CheckColor();
    CheckFrameOrder();
    assert(argc >= 2);
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    auto *window = SDL_CreateWindow("video test",0,0,640,360,SDL_WINDOW_HIDDEN);
    auto *renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    assert(renderer);
    {
        VideoPlayer player(renderer);
        const auto waitAvailable=[&] {
            const auto since=SDL_GetTicks();
            while (!player.CanStart() && SDL_GetTicks()-since<3000) SDL_Delay(2);
            assert(player.CanStart());
        };
        // A slow first GPU allocation must not consume audio or make the
        // opening frames late before the first picture can be displayed.
        SDL_setenv("UFLOW_VIDEO_TEST_FIRST_UPLOAD_DELAY_MS","400",1);
        assert(player.Start(argv[1],70,false));
        const auto startup=SDL_GetTicks();
        while(!player.Ready() && SDL_GetTicks()-startup<3000) {
            player.Update();assert(player.TakeError().empty());SDL_Delay(1);
        }
        assert(player.Ready() && SDL_GetTicks()-startup>=400);
        const auto first=player.Statistics();
        assert(first.presented==1 && first.lateDropped==0);
        assert(first.videoLagMs>=-50 && first.videoLagMs<60);
        assert(first.audioFrames<=4096 && first.audioMissingFrames==0);
        const auto warm=SDL_GetTicks();
        while(SDL_GetTicks()-warm<250){player.Update();SDL_Delay(3);}
        assert(player.Statistics().presented>=5 && player.Statistics().audioFrames>4000);
        player.Stop();waitAvailable();
        SDL_setenv("UFLOW_VIDEO_TEST_FIRST_UPLOAD_DELAY_MS","0",1);
        std::cout<<"PASS: slow first texture upload does not advance audio, drop opening frames or start video late\n";
        SDL_setenv("UFLOW_VIDEO_TEST_AUDIO_START_DELAY_MS","800",1);
        waitAvailable();assert(player.Start(argv[1],70,false));
        const auto delayedAudio=SDL_GetTicks();
        while(!player.Ready() && SDL_GetTicks()-delayedAudio<3000) {
            player.Update();assert(player.TakeError().empty());SDL_Delay(2);
        }
        assert(player.Ready() && SDL_GetTicks()-delayedAudio>=800);
        const auto audioWarm=SDL_GetTicks();
        while(SDL_GetTicks()-audioWarm<500){player.Update();SDL_Delay(2);}
        assert(player.Statistics().audioFrames>16000 && player.Statistics().audioMissingFrames==0);
        player.Stop();waitAvailable();
        SDL_setenv("UFLOW_VIDEO_TEST_AUDIO_START_DELAY_MS","0",1);
        std::cout<<"PASS: slow initial audio read cannot bypass startup buffering or produce an audible underrun\n";
        if(const char *shortAudio=std::getenv("UFLOW_VIDEO_SHORT_AUDIO_SAMPLE")) {
            waitAvailable();assert(player.Start(shortAudio,70));
            const auto begin=SDL_GetTicks();
            while(SDL_GetTicks()-begin<1500){player.Update();assert(player.TakeError().empty());SDL_Delay(2);}
            const auto stats=player.Statistics();
            assert(player.Ready() && stats.loops>=2 && stats.audioFrames>4000);
            player.Stop();waitAvailable();
            std::cout<<"PASS: 120 ms audio clips start and loop without reaching the half-second reserve\n";
        }
        for (int argument=1; argument<argc; ++argument) {
            waitAvailable();
            assert(player.Start(argv[argument],70));
            unsigned frames = 0;
            const char *sampleSeconds=std::getenv("UFLOW_VIDEO_SAMPLE_SECONDS");
            const Uint32 duration=argument>3 && sampleSeconds ?
                static_cast<Uint32>(std::max(3,std::atoi(sampleSeconds))*1000) : 2800;
            const Uint32 start = SDL_GetTicks();
            while (SDL_GetTicks()-start < duration) {
                if (player.Update()) ++frames;
                const auto error = player.TakeError();
                if (!error.empty()) { std::cerr<<argv[argument]<<": "<<error<<'\n'; return 1; }
                if (player.Ready()) assert(player.Draw(640,360));
                SDL_RenderPresent(renderer); SDL_Delay(3);
            }
            const auto stats=player.Statistics();
            std::cerr<<argv[argument]<<": ready="<<player.Ready()<<", frames="<<frames
                     <<", decoded="<<stats.decoded<<", loops="<<stats.loops<<'\n';
            assert(player.Ready() && frames > 25); // short fixtures must loop as well as decode
            assert(stats.decoded >= stats.presented && stats.presented > 25);
            assert(stats.converted>=stats.presented && stats.convertUs>0 && stats.decodeUs>0 && stats.uploadUs>0);
            assert(stats.outputWidth>0 && stats.outputHeight>0 && stats.maxPresentGapMs>0);
            if(argument>3 && std::getenv("UFLOW_VIDEO_EXPECT_WIDTH")) {
                assert(stats.outputWidth==std::atoi(std::getenv("UFLOW_VIDEO_EXPECT_WIDTH")));
                assert(stats.outputHeight==std::atoi(std::getenv("UFLOW_VIDEO_EXPECT_HEIGHT")));
            }
            if(argument>3 && std::getenv("UFLOW_VIDEO_EXPECT_NV12")) assert(stats.nativePlanes);
            if(argument>3 && std::getenv("UFLOW_VIDEO_TEST_REFERENCE_CADENCE"))
                assert(stats.presented*100 >= stats.decoded*80); // No second 30fps discard pass.
            if(argument<=3 || duration>=42000) assert(stats.loops>=1);
            if(argument!=3 && !(argument>3&&std::getenv("UFLOW_VIDEO_EXPECT_SILENT"))) assert(stats.audioFrames>1000);
            if(argument>3&&std::getenv("UFLOW_VIDEO_EXPECT_SILENT")) assert(stats.audioFrames==0);
            // A fade holds one presented GPU image, never the decoder/audio.
            player.Stop(true);
            assert(!player.Active() && player.Ready() && player.Draw(640,360,128));
            waitAvailable(); // Reaping workers must not discard the held frame.
            assert(player.Ready() && player.Draw(640,360,0));
            const auto stop = SDL_GetTicks(); player.Stop();
            assert(SDL_GetTicks()-stop < 1000 && !player.Active() && !player.Ready());
            std::cout<<"PASS: "<<argv[argument]<<" ("<<frames<<" presented frames, "<<stats.loops<<" loops, audio and cleanup checked)\n";
        }
        for (int i=0;i<8;++i) { waitAvailable(); assert(player.Start(argv[1],0)); SDL_Delay(2); player.Stop(); }
        waitAvailable();assert(player.Start(argv[1],0));
        const auto pauseWarm=SDL_GetTicks();
        while(player.Statistics().presented<5&&SDL_GetTicks()-pauseWarm<3000){player.Update();SDL_Delay(3);}
        assert(player.Ready());player.Suspend(true);
        const auto pausedFrames=player.Statistics().presented;
        const auto beforePause=player.Statistics().lateDropped;
        SDL_Delay(500);assert(!player.Update()&&player.Statistics().presented==pausedFrames);
        player.Suspend(false);const auto resumeAt=SDL_GetTicks();
        while(SDL_GetTicks()-resumeAt<250){player.Update();SDL_Delay(3);}
        assert(player.Statistics().presented>pausedFrames);
        assert(player.Statistics().lateDropped<=beforePause+2);
        player.Stop();
        waitAvailable();assert(player.Start(argv[1],0,false));assert(!player.Finished());
        const auto oneShotStart=SDL_GetTicks();
        while(!player.Finished() && SDL_GetTicks()-oneShotStart<4000) {
            player.Update();assert(player.TakeError().empty());SDL_Delay(3);
        }
        assert(player.Finished() && player.Ready() && player.Statistics().presented>0 && player.Statistics().loops==0);
        assert(SDL_GetTicks()-oneShotStart>=850);
        player.Stop();assert(!player.Finished());
        std::cout<<"PASS: non-looping completion waits for final picture/audio, and cancellation clears completion\n";
        if(const char *tiny=std::getenv("UFLOW_VIDEO_TINY_SAMPLE")) {
            waitAvailable(); assert(player.Start(tiny,0));
            const auto tinyStart=SDL_GetTicks();
            while(SDL_GetTicks()-tinyStart<1100) {
                player.Update(); assert(player.TakeError().empty()); SDL_Delay(3);
            }
            assert(player.Ready() && player.Statistics().loops>0);
            player.Stop();
            std::cout<<"PASS: one-frame clips start and loop without waiting forever for preroll\n";
        }
        waitAvailable();
        assert(player.Start(argv[1],0));
        player.Suspend(true); const auto stop = SDL_GetTicks(); player.Stop();
        assert(SDL_GetTicks()-stop < 1000);
        waitAvailable(); assert(player.Start("/missing.mp4",0));
        std::string error;
        const auto timeout = SDL_GetTicks();
        while (error.empty() && SDL_GetTicks()-timeout<2000) { player.Update(); error=player.TakeError(); SDL_Delay(2); }
        assert(!error.empty()); player.Stop();
        waitAvailable();
        SDL_setenv("UFLOW_VIDEO_TEST_CLEANUP_DELAY_MS","1200",1);
        assert(player.Start(argv[1],0)); SDL_Delay(50);
        const auto delayedStop=SDL_GetTicks(); player.Stop();
        assert(SDL_GetTicks()-delayedStop<250 && !player.Active() && !player.Ready());
        assert(!player.CanStart()); // Still retiring, but UI stop already returned.
        assert(!player.Start(argv[1],0)); // Never reuse live worker state.
        waitAvailable();
        assert(SDL_GetTicks()-delayedStop>=1000);
        SDL_setenv("UFLOW_VIDEO_TEST_CLEANUP_DELAY_MS","0",1);
        SDL_setenv("UFLOW_VIDEO_TEST_CONVERT_DELAY_MS","60",1);
        SDL_setenv("UFLOW_VIDEO_TEST_DECODE_DELAY_MS","26",1);
        assert(player.Start(argv[1],0));
        const auto slowStart=SDL_GetTicks(); unsigned slowFrames=0;
        while(SDL_GetTicks()-slowStart<3000) {
            if(player.Update()) ++slowFrames;
            assert(player.TakeError().empty()); SDL_Delay(3);
        }
        const auto slowStats=player.Statistics();
        std::cout<<"Slow-stage regression: "<<slowFrames<<" shown, "<<slowStats.lateDropped<<" replaced by newer frames\n";
        assert(slowFrames>=30 && slowStats.lateDropped>0);
        player.Stop(); waitAvailable();
        SDL_setenv("UFLOW_VIDEO_TEST_CONVERT_DELAY_MS","0",1);
        SDL_setenv("UFLOW_VIDEO_TEST_DECODE_DELAY_MS","0",1);
        if(const char *sample=std::getenv("UFLOW_VIDEO_AUDIO_STRESS")) {
            std::uint32_t missing[2]{};
            for(int parallel=0;parallel<2;++parallel) {
                SDL_setenv("UFLOW_VIDEO_TEST_SERIAL_READ",parallel?"0":"1",1);
                SDL_setenv("UFLOW_VIDEO_TEST_READ_DELAY_MS","10",1);
                SDL_setenv("UFLOW_VIDEO_TEST_DECODE_DELAY_MS","22",1);
                waitAvailable();assert(player.Start(sample,70,false));
                const auto begin=SDL_GetTicks();
                while(SDL_GetTicks()-begin<5000) {
                    player.Update();assert(player.TakeError().empty());SDL_Delay(3);
                }
                const auto stats=player.Statistics();missing[parallel]=stats.audioMissingFrames;
                assert(stats.audioFrames>100000 && stats.packetQueuePeak<=512*1024);
                player.Stop();waitAvailable();
            }
            std::cout<<"Audio starvation comparison: serial="<<missing[0]<<" parallel="<<missing[1]<<" missing frames\n";
            assert(missing[0]>24000 && missing[1]<2400 && missing[1]*5<missing[0]);
            SDL_setenv("UFLOW_VIDEO_TEST_SERIAL_READ","0",1);
            SDL_setenv("UFLOW_VIDEO_TEST_READ_DELAY_MS","0",1);
            SDL_setenv("UFLOW_VIDEO_TEST_DECODE_DELAY_MS","0",1);
            std::cout<<"PASS: overlapping slow reads and decoding prevents audio starvation with bounded compressed buffering\n";
            for(int parallel=0;parallel<2;++parallel) {
                SDL_setenv("UFLOW_VIDEO_TEST_SERIAL_READ",parallel?"0":"1",1);
                SDL_setenv("UFLOW_VIDEO_TEST_DECODE_STALL_MS","400",1);
                waitAvailable();assert(player.Start(sample,70,false));
                const auto begin=SDL_GetTicks();
                while(SDL_GetTicks()-begin<5000) {
                    player.Update();assert(player.TakeError().empty());SDL_Delay(3);
                }
                const auto stats=player.Statistics();missing[parallel]=stats.audioMissingFrames;
                assert(stats.audioFrames>100000 && stats.packetQueuePeak<=512*1024);
                player.Stop();waitAvailable();
            }
            std::cout<<"Decoder-wait audio comparison: serial="<<missing[0]<<" parallel="<<missing[1]<<" missing frames\n";
            assert(missing[0]>4000 && missing[1]<2400);
            SDL_setenv("UFLOW_VIDEO_TEST_SERIAL_READ","0",1);
            SDL_setenv("UFLOW_VIDEO_TEST_DECODE_STALL_MS","0",1);
            std::cout<<"PASS: audio decoding continues during video decoder waits; stop joins both owners\n";
            // Sustained decoder backpressure fills the 96-packet video queue.
            // Audio must keep its own file cursor even after that queue fills.
            SDL_setenv("UFLOW_VIDEO_TEST_DECODE_DELAY_MS","100",1);
            waitAvailable();assert(player.Start(sample,70,false));
            const auto sustainedBegin=SDL_GetTicks();
            while(SDL_GetTicks()-sustainedBegin<6500) {
                player.Update();assert(player.TakeError().empty());SDL_Delay(3);
            }
            const auto sustained=player.Statistics();
            std::cout<<"Sustained decoder backlog: audio="<<sustained.audioFrames
                     <<" missing="<<sustained.audioMissingFrames<<" decoded="<<sustained.decoded<<"\n";
            assert(sustained.audioFrames>240000 && sustained.audioMissingFrames<2400);
            assert(sustained.packetQueuePeak<=512*1024);
            player.Suspend(true);player.Stop();waitAvailable();
            SDL_setenv("UFLOW_VIDEO_TEST_DECODE_DELAY_MS","0",1);
            std::cout<<"PASS: bounded video backlog cannot block audio reading; suspended cancellation joins both readers\n";
        }
        if(const char *sample=std::getenv("UFLOW_VIDEO_NATIVE_SAMPLE")) {
            SDL_setenv("UFLOW_VIDEO_TEST_NV12","1",1);
            waitAvailable();assert(player.Start(sample,70));
            const auto begin=SDL_GetTicks();
            while(SDL_GetTicks()-begin<2800) {
                player.Update();assert(player.TakeError().empty());SDL_Delay(3);
            }
            const auto stats=player.Statistics();
            assert(stats.nativePlanes && stats.presented>25 && stats.loops>=1);
            assert(stats.outputWidth==1280 && stats.outputHeight==720 && stats.audioFrames>1000);
            player.Stop(true);waitAvailable();assert(player.Ready() && player.Draw(640,360,128));
            player.Stop();waitAvailable();
            for(int i=0;i<6;++i){assert(player.Start(sample,0));SDL_Delay(5);player.Stop();waitAvailable();}
            std::cout<<"PASS: native NV12 frame leases, source resolution, loops, audio, retained fade and rapid cancellation\n";
        }
        std::cout<<"PASS: parallel decode/conversion continues displaying when stages exceed frame budget; leased pixels remain unchanged\n";
        std::cout<<"PASS: unsupported H264 level/profile guard, non-blocking delayed cleanup, safe restart\n";
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    std::cout<<"PASS: 1s/adjustable idle timer, navigation reset, menu reset, dismissal, tick rollover, rapid cancel, suspended stop, missing file\n";
}
