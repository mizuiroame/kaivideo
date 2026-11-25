#!/bin/sh
# Faster 4K's numbers on a PC, from the repository's top folder:
#   sh tools/hdrtest/deblock.sh testvideos/hdrtest
# Makes made-up 4K HEVC clips (8 and 10 bits, 40 Mb/s; 10 bits at 10 Mb/s),
# times decoding them with and without deblocking on frames nothing refers to
# (kaivideo-hevc-host: FFmpeg 7.1 without x86 assembly), and compares the
# pictures (PSNR, SSIM; FFmpeg's -skip_loop_filter noref) against the normal decode.
set -e
CLIPS=${1:-testvideos/hdrtest}
FFMPEG_IMAGE=${FFMPEG_IMAGE:-linuxserver/ffmpeg}
export MSYS_NO_PATHCONV=1
mkdir -p "$CLIPS" tools/hdrtest/build
SRC="testsrc2=s=3840x2160:r=24:d=6,noise=alls=10:allf=t+u"
encode() {
    [ -f "$CLIPS/$1.mkv" ] || docker run --rm -v "$PWD:/w" -w /w --entrypoint ffmpeg "$FFMPEG_IMAGE" -hide_banner \
        -loglevel error -y -f lavfi -i "$SRC" -c:v libx265 -preset fast -b:v "$2" -pix_fmt "$3" \
        -x265-params log-level=error "$CLIPS/$1.mkv"
}
encode hevc4k_8bit 40M yuv420p
encode hevc4k_10bit 40M yuv420p10le
encode hevc4k_10bit_10M 10M yuv420p10le
docker run --rm -v "$PWD:/w" -w /w kaivideo-hevc-host sh -c "
    gcc -O2 -Itools/hevctest/source -Isource -I/opt/ff/include tools/hdrtest/deblock_bench.c \
        tools/hevctest/source/bench.c source/swframe.c -L/opt/ff/lib -lavformat -lavcodec -lavutil -lm -lpthread \
        -o tools/hdrtest/build/deblock_bench &&
    for t in 1 4; do tools/hdrtest/build/deblock_bench \$t $CLIPS/hevc4k_8bit.mkv $CLIPS/hevc4k_10bit.mkv; done"
for f in hevc4k_8bit hevc4k_10bit hevc4k_10bit_10M; do
    docker run --rm -v "$PWD:/w" -w /w --entrypoint ffmpeg "$FFMPEG_IMAGE" -hide_banner -loglevel error -y \
        -i "$CLIPS/$f.mkv" -skip_loop_filter noref -i "$CLIPS/$f.mkv" \
        -lavfi "[1:v][0:v]psnr=stats_file=tools/hdrtest/build/$f.psnr.log;[1:v][0:v]ssim=stats_file=tools/hdrtest/build/$f.ssim.log" \
        -f null -
    echo "$f:"
    # Frames with deblocking skipped differ (inf: frames left as they were).
    awk '{for(i=1;i<=NF;i++) if($i ~ /^psnr_avg:/){split($i,a,":"); if(a[2]=="inf") same++; else {s+=a[2]; n++; if(min==""||a[2]<min) min=a[2]}}}
         END{printf "  %d frames changed (%d not), PSNR %.1f dB on average, %.1f the lowest\n", n, same, s/n, min}' \
        "tools/hdrtest/build/$f.psnr.log"
    awk '{for(i=1;i<=NF;i++) if($i ~ /^All:/){split($i,a,":"); s+=a[2]; n++; if(min==""||a[2]<min) min=a[2]}}
         END{printf "  SSIM %.4f on average, %.4f the lowest\n", s/n, min}' "tools/hdrtest/build/$f.ssim.log"
done
