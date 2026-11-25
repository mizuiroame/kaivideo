# Building

[Back to the README](../README.md)

KaiVideo is built in Docker, on top of the
[ounce-devkit](https://github.com/ouncebrew/ounce-devkit) image:

```sh
# Once: the devkit plus FFmpeg (LGPL), then libsmb2 (LGPL) over it.
docker build -t kaivideo-devkit:before-smb ffmpeg
docker build -t kaivideo-devkit smb

docker run --rm -v "$PWD:/build/kaivideo" -w /build/kaivideo kaivideo-devkit make   # KaiVideo.nro

# Once: what the host tests need. Then the tests, and the screenshots.
docker build -t kaivideo-host tests
docker run --rm --security-opt seccomp=unconfined -v "$PWD:/w" -w /w/tests kaivideo-host make run
docker run --rm -v "$PWD:/build/kaivideo" -w /build/kaivideo kaivideo-host make -C tests/preview
```

- **FFmpeg**: `ffmpeg/Dockerfile` builds FFmpeg 7.1 from its release tarball
  (checked by SHA-256). It includes only the demuxers, parsers, bitstream
  filters, decoders, the `atempo` audio filter and the MKV muxer KaiVideo
  needs, and the build fails if it would include GPL or non-free parts.
  `ffmpeg/horizon.patch` lets GCC vectorize FFmpeg's C code, as FFmpeg 8
  does (HEVC's 10-bit motion compensation has no NEON version in 7.1).
- **Video decoding**: most video is decoded by the console, through
  [libounce](https://github.com/ouncebrew/libounce)'s `VideoDecoder`. FFmpeg
  decodes HEVC, 10-bit H.264 and VP9, and H.264 and VP9 over 1080p.
- **libsmb2**: `smb/Dockerfile` builds libsmb2 6.0.0 from its release tarball
  (checked by SHA-256) with the Switch 2 environment, for network folders
  over SMB.
- **Previews**: they're drawn in English and in every language in `data/`
  (`tests/preview/build/es`, `build/fr` and so on), to see what no longer
  fits. For Japanese, Chinese and Korean, `KAIVIDEO_CJK_FONT` names a font
  with them on the PC; without one, their text draws as boxes. They also
  check that every control answers a tap where it's drawn.
- **Live tests**: `tests/host_live/` has tests against real servers (Jellyfin,
  Samba, WebDAV), run on a PC.
- **Tests in CI**: on every push and pull request, GitHub Actions builds
  `tests/Dockerfile` and runs `make run` in it (`.github/workflows/tests.yml`).
  The live tests and the previews stay on the PC.
- **Picture subtitles to try**: `tools/picsubtest/make_picsubs.c` makes a
  VobSub pair (`.idx` + `.sub`) and an MKV with VobSub inside, from a video of
  your own and made-up lines (how to run it is at its top).
- **Icon**: `tools/make_icon.py` draws `icon.jpg`.
- **Languages**: `tools/lang_check.py` checks a language file against the
  texts in the code. See [Languages](languages.md).
