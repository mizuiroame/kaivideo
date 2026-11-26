#!/bin/sh
# Builds host_plexhome against fake_plextv.py and runs it (in the kaivideo-host image):
#   docker run --rm -v "$PWD:/w" -w /w kaivideo-host sh tests/host_plexhome/run.sh
set -e
PORT=8097
SRC="source/plexsync.c source/plexhome.c source/plex.c source/jellyfin.c source/net.c source/lang.c source/library.c
     source/naming.c source/genres.c source/util.c source/certtrust.c source/resume.c source/watched.c source/browser.c source/dav.c source/md4.c source/scroll.c source/kinetic.c source/folderlist.c"
gcc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -DKAIVIDEO_VERSION='"test"' \
    -DPLEX_TV="\"http://127.0.0.1:$PORT\"" -Isource -Ithird_party tests/host_plexhome/host_plexhome.c $SRC \
    -Wno-deprecated-declarations -o /tmp/host_plexhome -lcurl -ljansson -lpthread -lm
python3 tests/host_plexhome/fake_plextv.py $PORT &
SERVER=$!
sleep 1
status=0
/tmp/host_plexhome || status=$?
kill $SERVER
exit $status
