FROM gcc:14-bookworm
RUN apt-get update && apt-get install -y --no-install-recommends libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev ffmpeg libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev libpng-dev libturbojpeg0-dev libcurl4-openssl-dev xvfb xauth libgl1-mesa-dri libglx-mesa0 && rm -rf /var/lib/apt/lists/*
ENV SDL_VIDEODRIVER=dummy
WORKDIR /project
