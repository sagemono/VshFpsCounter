/*
 * fps_sensor.c
 *
 * Minimal PRX injected into the game process via ps3mapi
 * 
 * Detects RSX flips by polling cellGcmGetLastFlipTime() and
 * measures frame timing from hardware flip timestamps
 *
 * The VSH overlay injects this automatically on game launch
 *
 * Runs inside the game process. Keep it minimal: pure C, no STL.
 *
 * APPROACH: Poll cellGcmGetLastFlipTime() from a background thread.
 * This returns the exact microsecond timestamp of the last flip
 * from the RSX hardware. No handler hooking, no state modification.
 * 
 * Completely non-invasive, so the game's rendering pipeline is never
 * touched.
 *
 * Additionally collects:
 *   * Buffer count (double/triple (even quad? seen on GT6) buffering detection) via 
       cellGcmGetCurrentDisplayBufferId()
 *   * RSX clock speeds via cellGcmGetConfiguration()
 *   * Missed vsync detection via cellGcmGetVBlankCount()
 */

#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>
#include <sys/sys_time.h>
#include <cell/gcm.h>
#include "shared_metrics.h"


SYS_MODULE_INFO(FpsSensor, 0, 1, 0);
SYS_MODULE_START(fps_sensor_start);
SYS_MODULE_STOP(fps_sensor_stop);

__attribute__((noinline)) static void poke_lv2(uint64_t addr, uint64_t val)
{
    system_call_2(7, addr, val);
}

__attribute__((noinline)) static uint64_t peek_lv2(uint64_t addr)
{
    system_call_1(6, addr);
    return_to_user_prog(uint64_t);
}

/* sys_ppu_thread_exit via raw syscall, ps3mapi threads lack TLS so the
 * liblv2 wrapper crashes. module_start/stop must use this, not the wrapper. */
__attribute__((noinline, noreturn)) static void raw_exit_thread(void)
{
    system_call_1(41, 0ULL);
    while(1);
}

/* memset without libc, safe in TLS-less ps3mapi thread context */
static void zero_mem(volatile void* ptr, size_t bytes)
{
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    size_t i;
    for (i = 0; i < bytes; i++)
        p[i] = 0;
}

/* syscall 496 = _sys_prx_get_module_id_by_name */
__attribute__((noinline)) static int32_t get_module_id_by_name(const char* name)
{
    system_call_3(496, (uint64_t)(uint32_t)name, 0ULL, 0ULL);
    return_to_user_prog(int32_t);
}

#define RING_SIZE       2048
#define HIST_MAX_MS     200
#define HIST_BINS       (HIST_MAX_MS + 2)

static uint64_t  s_ring_time_us[RING_SIZE];
static uint16_t  s_ring_ft_ms[RING_SIZE];
static volatile int s_ring_head  = 0;
static volatile int s_ring_count = 0;
static volatile uint32_t s_frame_count = 0;

static volatile float s_fps_smooth  = 0.0f;
static volatile float s_ft_ms_cur  = 0.0f;

static float    s_fps_avg      = 0.0f;
static float    s_fps_min      = 0.0f;
static float    s_fps_max      = 0.0f;
static float    s_fps_1_low    = 0.0f;
static float    s_fps_01_low   = 0.0f;
static float    s_ft_min_ms    = 0.0f;
static float    s_ft_max_ms    = 0.0f;
static uint32_t s_hitch_count  = 0;
static uint32_t s_sample_count = 0;

static float    s_session_fps_min = 999999.0f;
static float    s_session_fps_max = 0.0f;
static float    s_session_ft_min_ms = 999999.0f;
static float    s_session_ft_max_ms = 0.0f;

static volatile uint64_t s_last_flip_us = 0;

static uint32_t s_window_ms      = 3000;
static float    s_hitch_thresh   = 50.0f;

static volatile int s_running = 0;

static sys_ppu_thread_t s_publisher_tid = SYS_PPU_THREAD_ID_INVALID;

static uint32_t s_hist[HIST_BINS];         /* windowed histogram, rebuilt each stats pass */
static uint32_t s_session_hist[HIST_BINS]; /* session-wide histogram for 1%/0.1% lows. this never resets */
static uint32_t s_session_hist_count = 0;

static uint32_t s_rsx_core_mhz = 0;
static uint32_t s_rsx_mem_mhz  = 0;
static uint32_t s_vram_mb      = 0;

static uint8_t  s_buf_seen_mask = 0; /* bitmask of display buffer IDs seen */
static uint32_t s_buf_count     = 0;

static uint32_t s_missed_vsyncs = 0;

/* GPU busy %: sample CellGcmControl get/put gap each tick.
 * get != put = RSX processing commands; get == put = idle/starved. */
static volatile uint32_t s_gpu_busy_samples = 0;
static volatile uint32_t s_gpu_total_samples = 0;
static float s_gpu_busy_pct = 0.0f;

static float s_ft_stdev_ms = 0.0f;
static float s_pacing_pct  = 0.0f;
static float s_dropped_pct = 0.0f;

static void compute_windowed_stats(uint64_t now_us)
{
    uint64_t window_us = (uint64_t)s_window_ms * 1000ULL;
    uint64_t window_start = (now_us > window_us) ? (now_us - window_us) : 0;

    int count = s_ring_count;
    int head = s_ring_head;
    int tail = head - count;
    if (tail < 0) tail += RING_SIZE;

    int idx = tail;
    int remaining = count;

    while (remaining > 0 && s_ring_time_us[idx] < window_start)
    {
        idx++;
        if (idx >= RING_SIZE) idx = 0;
        remaining--;
    }

    if (remaining < 3)
        return;

    zero_mem(s_hist, sizeof(s_hist));

    /* Single pass over the window: sum, min, max, hitches, histogram, sum-of-squares */
    double sum_ms = 0.0;
    double sum_sq = 0.0;
    uint16_t min_ms = 65535;
    uint16_t max_ms = 0;
    uint32_t hitches = 0;
    int used = 0;
    int it = idx;

    for (int k = 0; k < remaining; k++)
    {
        uint16_t ms = s_ring_ft_ms[it];
        double dms = (double)ms;
        sum_ms += dms;
        sum_sq += dms * dms;
        if (ms < min_ms) min_ms = ms;
        if (ms > max_ms) max_ms = ms;
        if ((float)ms > s_hitch_thresh) hitches++;

        int bin = (ms > HIST_MAX_MS) ? (HIST_BINS - 1) : (int)ms;
        s_hist[bin]++;
        used++;

        it++;
        if (it >= RING_SIZE) it = 0;
    }

    s_sample_count = (uint32_t)used;
    s_hitch_count = hitches;
    s_ft_min_ms = (float)min_ms;
    s_ft_max_ms = (float)max_ms;

    double mean_ms = sum_ms / (double)used;

    if (sum_ms > 0.001)
        s_fps_avg = (float)(1000.0 * (double)used / sum_ms);
    else
        s_fps_avg = 0.0f;

    s_fps_max = (min_ms > 0) ? (1000.0f / (float)min_ms) : 0.0f;
    s_fps_min = (max_ms > 0) ? (1000.0f / (float)max_ms) : 0.0f;

    if (s_fps_min > 0.0f && s_fps_min < s_session_fps_min)
        s_session_fps_min = s_fps_min;
    if (s_fps_max > s_session_fps_max)
        s_session_fps_max = s_fps_max;
    if (s_ft_min_ms < s_session_ft_min_ms)
        s_session_ft_min_ms = s_ft_min_ms;
    if (s_ft_max_ms > s_session_ft_max_ms)
        s_session_ft_max_ms = s_ft_max_ms;

    /* 1%/0.1% lows from session-wide histogram (not windowed, matches RTSS behavior) */
    if (s_session_hist_count >= 10)
    {
        uint32_t total = s_session_hist_count;
        uint32_t target_99  = (uint32_t)((99  * total + 99)  / 100);
        uint32_t target_999 = (uint32_t)((999 * total + 999) / 1000);

        uint16_t p99_ms  = 0;
        uint16_t p999_ms = 0;
        uint32_t cum = 0;
        int got_99 = 0, got_999 = 0;

        for (int b = 0; b < HIST_BINS; b++)
        {
            cum += s_session_hist[b];
            if (!got_99 && cum >= target_99)
            {
                p99_ms = (b >= HIST_BINS - 1) ? (uint16_t)(HIST_MAX_MS + 1) : (uint16_t)b;
                got_99 = 1;
            }
            if (!got_999 && cum >= target_999)
            {
                p999_ms = (b >= HIST_BINS - 1) ? (uint16_t)(HIST_MAX_MS + 1) : (uint16_t)b;
                got_999 = 1;
            }
            if (got_99 && got_999) break;
        }

        s_fps_1_low  = (p99_ms  > 0) ? (1000.0f / (float)p99_ms)  : 0.0f;
        s_fps_01_low = (p999_ms > 0) ? (1000.0f / (float)p999_ms) : 0.0f;
    }

    /* stdev: Var(X) = E[X^2] - E[X]^2, sqrt via newton (no libm) */
    {
        double variance = (sum_sq / (double)used) - (mean_ms * mean_ms);
        if (variance < 0.0) variance = 0.0;
        if (variance > 0.0)
        {
            double guess = mean_ms > 0.0 ? mean_ms * 0.5 : 1.0;
            int i;
            for (i = 0; i < 8; i++)
                guess = 0.5 * (guess + variance / guess);
            s_ft_stdev_ms = (float)guess;
        }
        else
        {
            s_ft_stdev_ms = 0.0f;
        }
    }

    /* pacing: % of frames within +/- 2ms of the nearest common refresh interval */
    {
        float target_ms;
        if (mean_ms < 12.5)       target_ms = 8.33f;   /* 120fps */ 
        else if (mean_ms < 18.3)  target_ms = 16.67f;  /* 60fps */
        else if (mean_ms < 25.0)  target_ms = 20.0f;   /* 50fps */
        else                      target_ms = 33.33f;  /* 30fps */

        int lo = (int)(target_ms - 2.0f);
        int hi = (int)(target_ms + 2.0f);
        if (lo < 0) lo = 0;
        if (hi >= HIST_BINS) hi = HIST_BINS - 1;

        uint32_t in_range = 0;
        int b;
        for (b = lo; b <= hi; b++)
            in_range += s_hist[b];

        s_pacing_pct = (float)in_range / (float)used * 100.0f;
    }

    {
        uint32_t total = s_frame_count + s_missed_vsyncs;
        if (total > 0)
            s_dropped_pct = (float)s_missed_vsyncs / (float)total * 100.0f;
        else
            s_dropped_pct = 0.0f;
    }
}

static void publish_metrics(void)
{
    poke_lv2(METRICS_SLOT_HEADER,    pack_u32_u32(METRICS_MAGIC, s_frame_count));
    poke_lv2(METRICS_SLOT_FPS_FT,    pack_f32_f32(s_fps_smooth, s_ft_ms_cur));
    poke_lv2(METRICS_SLOT_AVG_1LOW,  pack_f32_f32(s_fps_avg, s_fps_1_low));
    poke_lv2(METRICS_SLOT_01LOW_MIN, pack_f32_f32(s_fps_01_low, s_session_fps_min));
    poke_lv2(METRICS_SLOT_MAX_FTMIN, pack_f32_f32(s_session_fps_max, s_session_ft_min_ms));

    /* ft_max packed as raw u32 bits (f32 can't be packed with unpack_f32_lo safely) */
    {
        union { float f; uint32_t u; } ft_max;
        ft_max.f = s_session_ft_max_ms;
        poke_lv2(METRICS_SLOT_FTMAX_HIT, pack_u32_u32(ft_max.u, s_hitch_count));
    }

    poke_lv2(METRICS_SLOT_CNT_FLAGS, pack_u32_u32(s_sample_count, METRICS_FLAG_ACTIVE));
    poke_lv2(METRICS_SLOT_DEBUG,     pack_u32_u32(SENSOR_STATUS_MEASURING, s_buf_count));
    poke_lv2(METRICS_SLOT_RSX_CLK,  pack_u32_u32(s_rsx_core_mhz, s_rsx_mem_mhz));
    poke_lv2(METRICS_SLOT_RSX_MEM,  pack_u32_u32(s_vram_mb, s_missed_vsyncs));
    poke_lv2(METRICS_SLOT_GPU_BUSY, pack_f32_f32(s_gpu_busy_pct, s_dropped_pct));
    poke_lv2(METRICS_SLOT_DERIVED,  pack_f32_f32(s_ft_stdev_ms, s_pacing_pct));
}

static void clear_shared_metrics(void)
{
    int i;
    for (i = 0; i < METRICS_TOTAL_SLOTS; i++)
        poke_lv2(METRICS_LV2_BASE + (uint64_t)(i * 8), 0ULL);
}

static void read_rsx_info(void)
{
    CellGcmConfig config;
    cellGcmGetConfiguration(&config);
    s_rsx_core_mhz = (uint32_t)(config.coreFrequency / 1000000);
    s_rsx_mem_mhz  = (uint32_t)(config.memoryFrequency / 1000000);
    s_vram_mb      = (uint32_t)(config.localSize / (1024 * 1024));
}

static void update_buffer_tracking(void)
{
    uint8_t cur_id = 0;
    int32_t ret = cellGcmGetCurrentDisplayBufferId(&cur_id);
    if (ret == CELL_OK && cur_id < 8)
    {
        s_buf_seen_mask |= (uint8_t)(1 << cur_id);
        uint32_t count = 0;
        uint8_t mask = s_buf_seen_mask;
        while (mask)
        {
            count += (mask & 1);
            mask >>= 1;
        }
        s_buf_count = count;
    }
}

static void record_flip(uint64_t now_us)
{
    if (s_last_flip_us != 0)
    {
        uint64_t delta_us = now_us - s_last_flip_us;
        float delta_ms = (float)delta_us * 0.001f;
        s_ft_ms_cur = delta_ms;

        float fps_instant = (delta_us > 0) ? (1000000.0f / (float)delta_us) : 0.0f;
        const float alpha = 0.1f;
        s_fps_smooth = (s_fps_smooth <= 0.0f) ? fps_instant
                     : s_fps_smooth + alpha * (fps_instant - s_fps_smooth);

        /* ignore stalls > 500ms (pause/load screens) */
        if (delta_ms < 500.0f)
        {
            uint16_t ft_clamped = (delta_ms > 1000.0f)
                ? (uint16_t)1000 : (uint16_t)(delta_ms + 0.5f);

            s_ring_time_us[s_ring_head] = now_us;
            s_ring_ft_ms[s_ring_head]   = ft_clamped;

            s_ring_head++;
            if (s_ring_head >= RING_SIZE) s_ring_head = 0;
            if (s_ring_count < RING_SIZE) s_ring_count++;

            int bin = (ft_clamped > HIST_MAX_MS) ? (HIST_BINS - 1) : (int)ft_clamped;
            s_session_hist[bin]++;
            s_session_hist_count++;
        }

        s_frame_count++;
    }

    s_last_flip_us = now_us;
}

static void publisher_thread_func(uint64_t arg)
{
    (void)arg;

    system_time_t last_flip_time = 0;
    uint64_t last_vblank = 0;
    uint32_t poll_count = 0;
    int hw_info_read = 0;
    volatile CellGcmControl *gcm_ctrl = 0;

    poke_lv2(METRICS_SLOT_DEBUG, pack_u32_u32(SENSOR_STATUS_WAITING, 0));

    while (s_running)
    {
        sys_timer_usleep(1000);

        if (!s_running)
            break;

        {
            system_time_t cur_flip_time = cellGcmGetLastFlipTime();

            if (last_flip_time == 0)
            {
                /* wait for first flip as cellGcmGetLastFlipTime returns 0 until GCM initializes */
                if (cur_flip_time != 0)
                {
                    last_flip_time = cur_flip_time;
                    if (!hw_info_read)
                    {
                        read_rsx_info();
                        last_vblank = cellGcmGetVBlankCount();
                        gcm_ctrl = cellGcmGetControlRegister();
                        hw_info_read = 1;
                    }
                    poke_lv2(METRICS_SLOT_DEBUG, pack_u32_u32(SENSOR_STATUS_MEASURING, 0));
                }
                continue;
            }

            if (cur_flip_time != last_flip_time)
            {
                record_flip(cur_flip_time);
                last_flip_time = cur_flip_time;

                /* vblank_delta > 1 means a vsync was missed */
                {
                    uint64_t cur_vblank = cellGcmGetVBlankCount();
                    if (last_vblank > 0)
                    {
                        uint64_t vblank_delta = cur_vblank - last_vblank;
                        if (vblank_delta > 1)
                            s_missed_vsyncs += (uint32_t)(vblank_delta - 1);
                    }
                    last_vblank = cur_vblank;
                }

                update_buffer_tracking();
            }
        }

        if (gcm_ctrl)
        {
            if (gcm_ctrl->get != gcm_ctrl->put)
                s_gpu_busy_samples++;
            s_gpu_total_samples++;
        }

        poll_count++;
        if (poll_count >= 100)
        {
            poll_count = 0;
            if (s_frame_count > 0)
            {
                uint64_t now_us = sys_time_get_system_time();
                compute_windowed_stats(now_us);

                if (s_gpu_total_samples > 0)
                {
                    s_gpu_busy_pct = (float)s_gpu_busy_samples
                                   / (float)s_gpu_total_samples * 100.0f;
                    s_gpu_busy_samples = 0;
                    s_gpu_total_samples = 0;
                }

                publish_metrics();
            }
        }
    }

    sys_ppu_thread_exit(0);
}

CDECL_BEGIN

int fps_sensor_start(unsigned int args, void* argp)
{
    (void)args;
    (void)argp;

    /* check that lv2 peek/poke syscalls are available or cfw syscalls are disabled, bail out immediately */
    {
        uint64_t test = peek_lv2(METRICS_LV2_BASE);
        if (test == 0xFFFFFFFF80010003ULL)
            raw_exit_thread();
    }

    {
        int32_t gcm_id = get_module_id_by_name("cellGcm_Library");
        if (gcm_id < 0)
            raw_exit_thread();
    }

    /* no gcm probing needed, the publisher thread naturally handles this by waiting for cellGcmGetLastFlipTime() to return non-zero

     * this is ok because:
     * cellGcm_Library is confirmed loaded (checked above)
     * cellGcmGetLastFlipTime() is multithread safe (per SDK docs)
     * before cellGcmInit(), the function reads zeroed BSS -> returns 0
     * the publisher thread just keeps polling until it sees a real flip */

    clear_shared_metrics();

    zero_mem(s_ring_time_us, sizeof(s_ring_time_us));
    zero_mem(s_ring_ft_ms, sizeof(s_ring_ft_ms));
    s_ring_head      = 0;
    s_ring_count     = 0;
    s_frame_count    = 0;
    s_fps_smooth     = 0.0f;
    s_last_flip_us   = 0;

    s_session_fps_min   = 999999.0f;
    s_session_fps_max   = 0.0f;
    s_session_ft_min_ms = 999999.0f;
    s_session_ft_max_ms = 0.0f;

    zero_mem(s_session_hist, sizeof(s_session_hist));
    s_session_hist_count = 0;

    s_buf_seen_mask  = 0;
    s_buf_count      = 0;
    s_missed_vsyncs  = 0;
    s_rsx_core_mhz   = 0;
    s_rsx_mem_mhz    = 0;
    s_vram_mb        = 0;

    s_running = 1;

    /* start publisher thread which handles polling and LV2 writes
     * uses liblv2's sys_ppu_thread_create (not raw syscall) because the
     * new thread needs proper tls setup to call liblv2/libgcm functions
     * like sys_timer_usleep, cellGcmGetLastFlipTime
     *
     * on some games (e.g. skyrim), this call may trigger a non-fatal
     * tls exception because module_start runs in a ps3mapi thread without
     * tls. the game continues but the sensor won't start for that game */
    sys_ppu_thread_create(
        &s_publisher_tid,
        publisher_thread_func,
        0,
        1500,       /* priority (lower than game threads) */
        0x4000,     /* 16 KB stack */
        SYS_PPU_THREAD_CREATE_JOINABLE,
        "FpsSensorPub"
    );

    /* ps3mapi calls module_start as a thread entry point with a poison
     * return address (0xbadadd00...). we must exit the thread explicitly
     * instead of returning, or the CPU will jump to the poison addy */
    raw_exit_thread();
}

int fps_sensor_stop(unsigned int args, void* argp)
{
    (void)args;
    (void)argp;

    s_running = 0;

    /* no handler to restore, we never touched the game's flip handler so just wait for the publisher thread to notice s_running == 0 and exit */

    if (s_publisher_tid != SYS_PPU_THREAD_ID_INVALID)
    {
        uint64_t exit_code;
        sys_ppu_thread_join(s_publisher_tid, &exit_code);
        s_publisher_tid = SYS_PPU_THREAD_ID_INVALID;
    }

    /* clear shared memory so VSH knows we stopped */
    clear_shared_metrics();

    raw_exit_thread();
}

CDECL_END