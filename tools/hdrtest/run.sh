#!/bin/sh
# HDR checks on a PC, from the repository's top folder:
#   sh tools/hdrtest/run.sh testvideos/hdrtest
# Makes the clips if they aren't there (make_hdr_clip.py, with numpy, and x265
# in an ffmpeg image), takes a frame of each, and runs gl_hdr on it with Mesa (the
# kaiplayer-gltest image: Debian with Mesa's EGL and GLES). Pictures go to
# tools/hdrtest/build/*.png.
set -e
CLIPS=${1:-testvideos/hdrtest}
FFMPEG_IMAGE=${FFMPEG_IMAGE:-linuxserver/ffmpeg}
GL_IMAGE=${GL_IMAGE:-kaiplayer-gltest}
OUT=tools/hdrtest/build
mkdir -p "$OUT"
export MSYS_NO_PATHCONV=1
ffmpeg() { docker run --rm -v "$PWD:/w" -w /w --entrypoint ffmpeg "$FFMPEG_IMAGE" -hide_banner -loglevel error -y "$@"; }

# A clip: python makes the frames, x265 encodes them as tagged HDR10 or HLG.
make_clip() {  # name, pq|hlg, width, height, bitrate
    [ -f "$CLIPS/$1.mkv" ] && return 0
    if [ "$2" = pq ]; then
        trc=smpte2084
        x265="hdr10=1:repeat-headers=1:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc"
        x265="$x265:master-display=G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1):max-cll=1000,400"
    else
        trc=arib-std-b67
        x265="repeat-headers=1:colorprim=bt2020:transfer=arib-std-b67:colormatrix=bt2020nc"
    fi
    python tools/hdrtest/make_hdr_clip.py "$2" "$3" "$4" 6 | docker run -i --rm -v "$PWD:/w" -w /w --entrypoint ffmpeg \
        "$FFMPEG_IMAGE" -hide_banner -loglevel error -y -f rawvideo -pix_fmt yuv420p10le -s "$3x$4" -r 24 -i - \
        -c:v libx265 -preset fast -b:v "$5" -pix_fmt yuv420p10le -x265-params "$x265:log-level=error" \
        -color_primaries bt2020 -color_trc "$trc" -colorspace bt2020nc -color_range tv "$CLIPS/$1.mkv"
}
mkdir -p "$CLIPS"
make_clip hdr10_1080p pq 1920 1080 8M
make_clip hlg_1080p hlg 1920 1080 8M
make_clip hdr10_2160p pq 3840 2160 20M

# name, transfer, size, peak (MaxCLL), second to take
for clip in "hdr10_1080p pq 1920 1080 1000 3" "hlg_1080p hlg 1920 1080 1000 3" "hdr10_2160p pq 3840 2160 1000 3"; do
    set -- $clip
    ffmpeg -ss "$6" -i "$CLIPS/$1.mkv" -frames:v 1 -f rawvideo -pix_fmt yuv420p10le "$OUT/$1.yuv"
done
docker run --rm -v "$PWD:/w" -w /w "$GL_IMAGE" sh -c "
    gcc -O2 -DKAI_GPU -Isource tools/hdrtest/gl_hdr.c source/gfx_gl.c source/hdr.c source/swframe.c \
        -lEGL -lGLESv2 -lm -o $OUT/gl_hdr &&
    $OUT/gl_hdr $OUT/hdr10_1080p.yuv 1920 1080 pq 1000 $OUT/hdr10_1080p &&
    $OUT/gl_hdr $OUT/hlg_1080p.yuv 1920 1080 hlg 1000 $OUT/hlg_1080p &&
    $OUT/gl_hdr $OUT/hdr10_2160p.yuv 3840 2160 pq 1000 $OUT/hdr10_2160p"
for p in "$OUT"/*.ppm; do
    ffmpeg -i "$p" "${p%.ppm}.png"
done
