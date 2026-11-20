// Rewraps files on a PC as the download thread does after a conversion:
//   host_remux IN OUT
// Says how far it came and whether it worked; check OUT with ffprobe.
#include <stdio.h>

#include "remux.h"

static void say(void *user, double fraction)
{
    (void)user;
    printf("\r  %3d%%", (int)(fraction * 100));
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "%s IN OUT\n", argv[0]);
        return 2;
    }
    char error[160];
    bool ok = remuxToMkv(argv[1], argv[2], NULL, say, NULL, error, sizeof(error));
    printf("\n%s %s\n", ok ? "ok" : "FAIL", ok ? argv[2] : error);
    return ok ? 0 : 1;
}
