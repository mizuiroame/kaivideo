# libsmb2 for Switch 2 under the LGPL 2.1, for KaiVideo's network folder over
# SMB (a Windows shared folder, Samba on a NAS): the client library only, its
# own NTLM, signing and encryption; no Kerberos, no examples. Built with the
# Switch 2 environment over the FFmpeg image:
#   docker build -t kaivideo-devkit smb
# (tag the image it starts from first, e.g. kaivideo-devkit:before-smb).
ARG DEVKIT_IMAGE=kaivideo-devkit:before-smb
FROM ${DEVKIT_IMAGE}

ARG LIBSMB2_VERSION=6.0.0
ARG LIBSMB2_SHA256=c65a7a2b7a6e967a4fd5b6168fca4f1f7d0362a3510767700609c770e3ead1e8

WORKDIR /tmp/libsmb2
RUN curl -fsSL -o libsmb2.tar.gz "https://github.com/sahlberg/libsmb2/archive/refs/tags/v${LIBSMB2_VERSION}.tar.gz" \
 && echo "${LIBSMB2_SHA256}  libsmb2.tar.gz" | sha256sum -c - \
 && tar xzf libsmb2.tar.gz --strip-components=1

RUN . /opt/devkitpro/switch2vars.sh \
 && cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
      -DCMAKE_INSTALL_PREFIX="$PORTLIBS_PREFIX" -DBUILD_SHARED_LIBS=OFF -DENABLE_EXAMPLES=OFF \
      -DCMAKE_C_FLAGS="$CPPFLAGS $CFLAGS" \
 && cmake --build build -j"$(nproc)" \
 && cmake --install build \
 && cp COPYING LICENCE-LGPL-2.1.txt "$PORTLIBS_PREFIX/include/smb2/" \
 && rm -rf /tmp/libsmb2

WORKDIR /src
