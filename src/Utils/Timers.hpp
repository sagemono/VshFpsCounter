#pragma once
#include <stdint.h>
#include <unistd.h>
#include <sys/sys_time.h>
#include <sys/timer.h>
#include <sys/time_util.h>

uint64_t GetTimeNow();
uint64_t GetCurrentTick();
void Sleep(uint64_t ms);