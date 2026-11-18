// Clock and battery for the status corner. Values refresh every few seconds.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool has_time;
    int hour;    // 0-23, local time.
    int minute;
    bool has_battery;
    int battery_percent;
    bool charging;
} SysInfo;

void sysinfoInit(void);
void sysinfoExit(void);

// Fills info, querying the system at most once per refresh interval.
void sysinfoGet(SysInfo *info, uint64_t now_ns);
