// Decode speed with and without deblocking on frames nothing refers to, on a
// PC with the FFmpeg the console has (no x86 assembly): tools/hdrtest/deblock.sh.
//   deblock_bench THREADS VIDEO...
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "bench.h"
static uint64_t nowNs(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
int main(int argc,char**argv){
  int threads=atoi(argv[1]);
  for(int i=2;i<argc;i++){
    for(int rep=0;rep<5;rep++) for(int skip=0;skip<2;skip++){
      BenchResult r; char e[96]="";
      if(!benchRun(argv[i],threads,skip,60.0,96,nowNs,NULL,NULL,&r,e,sizeof e)){printf("%s fail %s\n",argv[i],e);continue;}
      printf("%s threads=%d skip=%d frames=%d fps=%.2f slowest=%.2f convert=%.2fms\n",argv[i],threads,skip,r.frames,r.average_fps,r.slowest_fps,r.convert_ms);
      fflush(stdout);
    }
  }
}
