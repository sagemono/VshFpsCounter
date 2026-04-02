#ifndef SHARED_METRICS_H
#define SHARED_METRICS_H

#include <stdint.h>

#define METRICS_LV2_BASE        0x8000000000700100ULL
#define METRICS_MAGIC           0x46505331  /* "FPS1" */

/*
 * Slot layout (12 slots = 96 bytes total):
 *
 * Slot  0 (0x00): [magic:u32][frame_count:u32]
 * Slot  1 (0x08): [fps_instant:f32][frame_time_ms:f32]
 * Slot  2 (0x10): [fps_avg:f32][fps_1_low:f32]
 * Slot  3 (0x18): [fps_01_low:f32][fps_min:f32]
 * Slot  4 (0x20): [fps_max:f32][ft_min_ms:f32]
 * Slot  5 (0x28): [ft_max_ms:f32][hitch_count:u32]
 * Slot  6 (0x30): [sample_count:u32][flags:u32]
 * Slot  7 (0x38): [sensor_status:u32][buffer_count:u32]
 * Slot  8 (0x40): [rsx_core_mhz:u32][rsx_mem_mhz:u32]
 * Slot  9 (0x48): [vram_mb:u32][missed_vsyncs:u32]
 * Slot 10 (0x50): [gpu_busy_pct:f32][dropped_pct:f32]
 * Slot 11 (0x58): [ft_stdev_ms:f32][pacing_pct:f32]
 */
#define METRICS_SLOT_HEADER     (METRICS_LV2_BASE + 0x00)
#define METRICS_SLOT_FPS_FT     (METRICS_LV2_BASE + 0x08)
#define METRICS_SLOT_AVG_1LOW   (METRICS_LV2_BASE + 0x10)
#define METRICS_SLOT_01LOW_MIN  (METRICS_LV2_BASE + 0x18)
#define METRICS_SLOT_MAX_FTMIN  (METRICS_LV2_BASE + 0x20)
#define METRICS_SLOT_FTMAX_HIT  (METRICS_LV2_BASE + 0x28)
#define METRICS_SLOT_CNT_FLAGS  (METRICS_LV2_BASE + 0x30)
#define METRICS_SLOT_DEBUG      (METRICS_LV2_BASE + 0x38)
#define METRICS_SLOT_RSX_CLK    (METRICS_LV2_BASE + 0x40)
#define METRICS_SLOT_RSX_MEM    (METRICS_LV2_BASE + 0x48)
#define METRICS_SLOT_GPU_BUSY   (METRICS_LV2_BASE + 0x50)
#define METRICS_SLOT_DERIVED    (METRICS_LV2_BASE + 0x58)
#define METRICS_TOTAL_SLOTS     12
#define METRICS_TOTAL_BYTES     (METRICS_TOTAL_SLOTS * 8)

#define METRICS_FLAG_ACTIVE     (1 << 0)

#define SENSOR_STATUS_WAITING   1  /* waiting for first flip */
#define SENSOR_STATUS_MEASURING 2  /* actively measuring */

static inline uint64_t pack_f32_f32(float a, float b)
{
    union { float f; uint32_t u; } ua, ub;
    ua.f = a;
    ub.f = b;
    return ((uint64_t)ua.u << 32) | (uint64_t)ub.u;
}

static inline uint64_t pack_u32_u32(uint32_t hi, uint32_t lo)
{
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline float unpack_f32_hi(uint64_t v)
{
    union { uint32_t u; float f; } conv;
    conv.u = (uint32_t)(v >> 32);
    return conv.f;
}

static inline float unpack_f32_lo(uint64_t v)
{
    union { uint32_t u; float f; } conv;
    conv.u = (uint32_t)(v & 0xFFFFFFFFULL);
    return conv.f;
}

static inline uint32_t unpack_u32_hi(uint64_t v)
{
    return (uint32_t)(v >> 32);
}

static inline uint32_t unpack_u32_lo(uint64_t v)
{
    return (uint32_t)(v & 0xFFFFFFFFULL);
}

#endif