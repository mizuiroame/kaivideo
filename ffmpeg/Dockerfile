# FFmpeg for Switch 2 under the LGPL 2.1, for KaiVideo: the MP4 and
# MKV/WebM demuxers, the bitstream filters that hand video to the console's
# hardware decoder, and the audio decoders, plus the resampler. Video isn't
# decoded by FFmpeg: libounce's VideoDecoder does that. No GPL or non-free
# parts, no network code.
#   docker build -t kaivideo-devkit ffmpeg
# gives the devkit image with FFmpeg in portlibs/switch2.
ARG DEVKIT_IMAGE=ounce-devkit:latest
FROM ${DEVKIT_IMAGE}

ARG FFMPEG_VERSION=7.1
ARG FFMPEG_SHA256=40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6

WORKDIR /tmp/ffmpeg
COPY horizon.patch .
RUN curl -fsSL -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" \
 && echo "${FFMPEG_SHA256}  ffmpeg.tar.xz" | sha256sum -c - \
 && tar xf ffmpeg.tar.xz --strip-components=1 \
 && patch -Np1 -i horizon.patch

RUN . /opt/devkitpro/switch2vars.sh \
 && ./configure --prefix="$PORTLIBS_PREFIX" \
      --enable-cross-compile --cross-prefix=aarch64-none-elf- \
      --arch=aarch64 --target-os=horizon --enable-pic \
      --extra-cflags="$CPPFLAGS $CFLAGS -D_GNU_SOURCE" \
      --extra-ldflags="$LDFLAGS" \
      --disable-shared --enable-static \
      --disable-runtime-cpudetect --disable-programs --disable-doc --disable-debug \
      --disable-autodetect --disable-network --disable-everything \
      --disable-avdevice --disable-avfilter --disable-swscale \
      --enable-avcodec --enable-avformat --enable-swresample \
      --enable-neon \
      --enable-decoder=mp3float,mp3,aac,vorbis,opus,flac,ac3 \
      --enable-parser=h264,vp9,vp8,av1,mpegaudio,aac,vorbis,opus,flac,ac3 \
      --enable-demuxer=mov,matroska \
      --enable-bsf=h264_mp4toannexb,vp9_superframe_split,extract_extradata \
 && make -j"$(nproc)" \
 && make install \
 && grep -q 'define CONFIG_GPL 0' config.h \
 && grep -q 'define CONFIG_NONFREE 0' config.h \
 && rm -rf /tmp/ffmpeg

WORKDIR /src
