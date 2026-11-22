// MobileGlues-RS - gl/perf.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
//
// Implementation notes
// --------------------
//  * Counters are std::atomic<uint64_t> with relaxed ordering: they are plain
//    tally marks, so no ordering against other memory is needed. GL calls can
//    arrive from more than one thread, the swap path certainly runs on the
//    render thread.
//  * Frame times go into fixed histograms (0.5 ms buckets up to 200 ms, plus
//    one overflow bucket). Percentiles come out of the histogram instead of
//    keeping every sample: 400 uint64 per histogram is 3.2 KB, and a session
//    of any length costs the same.
//  * The report is rewritten atomically (tmp + rename) every window so a file
//    the user grabs mid-game is always a complete, parseable snapshot.
//  * Frame time here is the wall time between successive presents on the EGL
//    surface. It includes the game's CPU work and any GPU wait the swap had
//    to absorb; that is the number that decides how the session *feels*, and
//    it is the number the optimization roadmap needs.

#include "perf.h"

#include "../config/config.h"
#include "../config/settings.h"
#include "../version.h"
#include "envvars.h"
#include "glsl/rs_bridge.h"
#include "log.h"
#include "mg.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#define DEBUG 0

int mg_perf_active = 0;

namespace {

// One histogram per frame-time population: the session as a whole and the
// current window. Bucket width 0.5 ms, range 0..200 ms.
constexpr int kBucketCount = 400;
constexpr double kBucketMs = 0.5;
constexpr double kMaxMs = kBucketCount * kBucketMs;

struct FrameHistogram {
    std::atomic<uint64_t> buckets[kBucketCount + 1];
    std::atomic<uint64_t> count;
    std::atomic<uint64_t> sum_ns; // total, for the true average
    std::atomic<uint64_t> max_ns;

    void record(uint64_t ns) {
        const double ms = (double)ns / 1e6;
        int idx = kBucketCount;
        if (ms < kMaxMs) idx = (int)(ms / kBucketMs);
        buckets[idx].fetch_add(1, std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);
        sum_ns.fetch_add(ns, std::memory_order_relaxed);
        // Monotone max: race losers simply lose.
        uint64_t prev = max_ns.load(std::memory_order_relaxed);
        while (ns > prev && !max_ns.compare_exchange_weak(prev, ns, std::memory_order_relaxed)) {
        }
    }
};

struct PerfState {
    // Frame stream.
    std::atomic<uint64_t> last_present_ns{0};
    std::atomic<uint64_t> started_ns{0};
    std::atomic<uint64_t> frames{0};
    FrameHistogram total;
    FrameHistogram window;

    // Window bookkeeping (owned by the swap thread; no atomics needed but the
    // histogram inside must survive being read from the report writer).
    uint64_t window_start_ns = 0;
    uint64_t window_frames_at_start = 0;

    // Counters, cumulative.
    std::atomic<uint64_t> draws{0};
    std::atomic<uint64_t> multidraw_batches{0};
    std::atomic<uint64_t> multidraw_subdraws{0};
    std::atomic<uint64_t> shader_sources{0};
    std::atomic<uint64_t> shader_direct{0};
    std::atomic<uint64_t> shader_cache_hits{0};
    std::atomic<uint64_t> shader_cache_misses{0};
    std::atomic<uint64_t> shader_translate_ns{0};
    std::atomic<uint64_t> buffer_calls{0};
    std::atomic<uint64_t> buffer_bytes{0};
    std::atomic<uint64_t> texture_calls{0};
    std::atomic<uint64_t> texture_bytes{0};
    std::atomic<uint64_t> state_changes{0};
    std::atomic<uint64_t> use_program{0};

    // 2.3.0 pipeline-cache counters. use_program counts every arriving call
    // (as before); forwarded/skipped split it into what really reached the
    // driver and what the dedup absorbed.
    std::atomic<uint64_t> use_program_forwarded{0};
    std::atomic<uint64_t> use_program_skipped{0};
    std::atomic<uint64_t> shader_compile_executed{0};
    std::atomic<uint64_t> shader_compile_ns{0};
    std::atomic<uint64_t> shader_compile_deferred{0};
    std::atomic<uint64_t> shader_status_hits{0};
    std::atomic<uint64_t> program_link_ns{0};
    std::atomic<uint64_t> program_binary_hits{0};
    std::atomic<uint64_t> program_binary_misses{0};
    std::atomic<uint64_t> program_binary_failures{0};
    std::atomic<uint64_t> program_binary_load_ns{0};

    // Snapshot of the counters when the current window opened, so the report
    // can show "last N seconds" rates without stopping anything.
    uint64_t snap_frames = 0, snap_draws = 0, snap_md_batches = 0, snap_md_subdraws = 0;
    uint64_t snap_shader_sources = 0, snap_shader_hits = 0, snap_shader_misses = 0;
    uint64_t snap_shader_direct = 0, snap_shader_translate_ns = 0;
    uint64_t snap_buffer_calls = 0, snap_buffer_bytes = 0;
    uint64_t snap_texture_calls = 0, snap_texture_bytes = 0;
    uint64_t snap_state_changes = 0, snap_use_program = 0;
    uint64_t snap_binary_hits = 0, snap_binary_misses = 0;

    std::mutex write_mutex;
    bool history_header_written = false;
    std::string device_renderer = "<not captured yet>";
    std::string device_gles_version = "<not captured yet>";
    time_t started_wall = 0;
};

PerfState g_perf;

constexpr uint64_t kWindowNs = 10ull * 1000 * 1000 * 1000; // 10 seconds

uint64_t now_ns() {
#ifdef __APPLE__
    return (uint64_t)mach_absolute_time();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

// Percentile in milliseconds out of a histogram. `q` in (0, 1].
double hist_percentile_ms(const FrameHistogram& h, double q) {
    const uint64_t total = h.count.load(std::memory_order_relaxed);
    if (total == 0) return 0.0;
    uint64_t want = (uint64_t)(q * (double)total);
    if (want >= total) want = total - 1;
    uint64_t seen = 0;
    for (int i = 0; i <= kBucketCount; i++) {
        seen += h.buckets[i].load(std::memory_order_relaxed);
        if (seen > want) {
            const double base = (double)i * kBucketMs;
            const double width = (i == kBucketCount) ? 1.0 : kBucketMs;
            // Center of the bucket is the best estimate a histogram can give.
            return base + width * 0.5;
        }
    }
    return kMaxMs;
}

double hist_avg_ms(const FrameHistogram& h) {
    const uint64_t c = h.count.load(std::memory_order_relaxed);
    if (c == 0) return 0.0;
    return (double)h.sum_ns.load(std::memory_order_relaxed) / 1e6 / (double)c;
}

double hist_max_ms(const FrameHistogram& h) {
    return (double)h.max_ns.load(std::memory_order_relaxed) / 1e6;
}

// Small formatting helpers keep the report builder readable.
std::string fmt_f(double v, const char* unit) {
    char buf[64];
    if (v >= 1000.0) {
        snprintf(buf, sizeof(buf), "%.0f%s", v, unit);
    } else {
        snprintf(buf, sizeof(buf), "%.2f%s", v, unit);
    }
    return std::string(buf);
}

std::string fmt_u64(uint64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
    return std::string(buf);
}

double rate_per_frame(uint64_t counter, uint64_t frames) {
    return frames ? (double)counter / (double)frames : 0.0;
}

double rate_per_second(uint64_t counter, double seconds) {
    return seconds > 0.0 ? (double)counter / seconds : 0.0;
}

std::string report_path() {
    return std::string(mg_directory_path ? mg_directory_path : "/sdcard/MG") + "/perf_report.txt";
}

std::string history_path() {
    return std::string(mg_directory_path ? mg_directory_path : "/sdcard/MG") + "/perf_history.log";
}

std::string tmp_path(const std::string& path) {
    return path + ".tmp";
}

// The Rust core status comes from the bridge so the report tells the truth
// about which implementation actually ran during the session.
std::string rust_status() {
    if (rs::enabled()) {
        return std::string("active (") + rs::version() + ")";
    }
    return std::string("disabled (C++ fallback paths)");
}

void write_history_line(uint64_t uptime_s, uint64_t w_frames, double w_seconds, PerfState& s) {
    FILE* f = fopen(history_path().c_str(), "a");
    if (!f) return;
    if (!s.history_header_written) {
        fprintf(f,
                "# MobileGlues perf history (one line per 10s window)\n"
                "# uptime_s,frames,fps_avg,ft_avg_ms,ft_p95_ms,ft_max_ms,draws_per_frame,"
                "md_batches_per_frame,md_subdraws_per_frame,shader_hits,shader_misses,"
                "buffer_MB_per_frame,texture_MB_per_frame,state_changes_per_frame,use_program_per_frame,"
                "pipeline_binary_hits,pipeline_binary_misses\n");
        s.history_header_written = true;
    }
    const double fps = w_seconds > 0 ? (double)w_frames / w_seconds : 0.0;
    const uint64_t w_draws = s.draws.load() - s.snap_draws;
    const uint64_t w_md_b = s.multidraw_batches.load() - s.snap_md_batches;
    const uint64_t w_md_s = s.multidraw_subdraws.load() - s.snap_md_subdraws;
    const uint64_t w_hits = s.shader_cache_hits.load() - s.snap_shader_hits;
    const uint64_t w_miss = s.shader_cache_misses.load() - s.snap_shader_misses;
    const uint64_t w_buf = s.buffer_bytes.load() - s.snap_buffer_bytes;
    const uint64_t w_tex = s.texture_bytes.load() - s.snap_texture_bytes;
    const uint64_t w_state = s.state_changes.load() - s.snap_state_changes;
    const uint64_t w_prog = s.use_program.load() - s.snap_use_program;
    const double w_fps = w_frames ? (double)w_frames : 0.0;
    fprintf(f,
            "%llu,%llu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%llu,%llu,%.3f,%.3f,%.2f,%.2f,%llu,%llu\n",
            (unsigned long long)uptime_s, (unsigned long long)w_frames, fps, hist_avg_ms(s.window),
            hist_percentile_ms(s.window, 0.95), hist_max_ms(s.window),
            rate_per_frame(w_draws, w_frames), rate_per_frame(w_md_b, w_frames),
            rate_per_frame(w_md_s, w_frames), (unsigned long long)w_hits, (unsigned long long)w_miss,
            (double)w_buf / 1048576.0 / (w_fps > 0 ? w_fps : 1.0),
            (double)w_tex / 1048576.0 / (w_fps > 0 ? w_fps : 1.0),
            rate_per_frame(w_state, w_frames), rate_per_frame(w_prog, w_frames),
            (unsigned long long)(s.program_binary_hits.load() - s.snap_binary_hits),
            (unsigned long long)(s.program_binary_misses.load() - s.snap_binary_misses));
    fclose(f);
}

void write_report(const PerfState& s) {
    const std::string path = report_path();
    const std::string tmp = tmp_path(path);

    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return;

    const uint64_t frames = s.frames.load();
    const uint64_t uptime_ns = now_ns() - s.started_ns.load();
    const double uptime_s = (double)uptime_ns / 1e9;

    char started_str[32] = {0};
    {
        struct tm tmv;
        const time_t t = s.started_wall;
        localtime_r(&t, &tmv);
        strftime(started_str, sizeof(started_str), "%Y-%m-%d %H:%M:%S", &tmv);
    }

    fprintf(f, "==================== MobileGlues perf report ====================\n");
    fprintf(f, "renderer_version: %d.%d.%d\n", MAJOR, MINOR, REVISION);
    fprintf(f, "session_started: %s\n", started_str);
    fprintf(f, "uptime_s: %.1f\n", uptime_s);
    fprintf(f, "device_gpu: %s\n", s.device_renderer.c_str());
    fprintf(f, "device_gles: %s\n", s.device_gles_version.c_str());
    fprintf(f, "rust_core: %s\n", rust_status().c_str());
    fprintf(f, "---- settings ----\n");
    fprintf(f, "%s", dump_settings_string("  ").c_str());

    fprintf(f, "---- frame timing (all times wall-clock between presents) ----\n");
    fprintf(f, "frames_total: %llu\n", (unsigned long long)frames);
    if (frames > 0 && uptime_s > 0.5) {
        fprintf(f, "fps_average: %.2f\n", (double)frames / uptime_s);
    }
    fprintf(f, "frame_time_ms_avg: %s\n", fmt_f(hist_avg_ms(s.total), "").c_str());
    fprintf(f, "frame_time_ms_p50: %s\n", fmt_f(hist_percentile_ms(s.total, 0.50), "").c_str());
    fprintf(f, "frame_time_ms_p90: %s\n", fmt_f(hist_percentile_ms(s.total, 0.90), "").c_str());
    fprintf(f, "frame_time_ms_p95: %s\n", fmt_f(hist_percentile_ms(s.total, 0.95), "").c_str());
    fprintf(f, "frame_time_ms_p99: %s\n", fmt_f(hist_percentile_ms(s.total, 0.99), "").c_str());
    fprintf(f, "frame_time_ms_max: %s\n", fmt_f(hist_max_ms(s.total), "").c_str());
    {
        // "1% low" as the community reads it: the average FPS you get in the
        // worst 1% of frames, approximated from the 99th percentile frame time.
        const double p99 = hist_percentile_ms(s.total, 0.99);
        if (p99 > 0.0) fprintf(f, "fps_1pct_low: %.2f\n", 1000.0 / p99);
    }

    fprintf(f, "---- gl calls (total | per frame | per second) ----\n");
    const uint64_t draws = s.draws.load();
    const uint64_t md_b = s.multidraw_batches.load();
    const uint64_t md_s = s.multidraw_subdraws.load();
    const uint64_t buf_c = s.buffer_calls.load();
    const uint64_t buf_b = s.buffer_bytes.load();
    const uint64_t tex_c = s.texture_calls.load();
    const uint64_t tex_b = s.texture_bytes.load();
    const uint64_t st_c = s.state_changes.load();
    const uint64_t pr_c = s.use_program.load();
    fprintf(f, "draw_calls: %s | %.2f | %.1f\n", fmt_u64(draws).c_str(), rate_per_frame(draws, frames),
            rate_per_second(draws, uptime_s));
    fprintf(f, "multidraw_batches: %s | %.2f | %.1f\n", fmt_u64(md_b).c_str(), rate_per_frame(md_b, frames),
            rate_per_second(md_b, uptime_s));
    fprintf(f, "multidraw_subdraws: %s | %.2f | %.1f\n", fmt_u64(md_s).c_str(), rate_per_frame(md_s, frames),
            rate_per_second(md_s, uptime_s));
    // A multidraw batch also crosses prepareForDraw once, so the effective
    // driver-level draw count is single draws plus sub-draws minus the batches
    // themselves. (A fallback re-entry counts a batch twice; fallbacks are
    // WARN_ONCE paths, rare enough that the estimate stays honest.)
    const uint64_t effective = draws + md_s - (md_s >= md_b ? md_b : md_s);
    fprintf(f, "effective_draws_per_frame: %.2f\n", rate_per_frame(effective, frames));
    fprintf(f, "buffer_uploads_calls: %s | %.2f | %.1f\n", fmt_u64(buf_c).c_str(), rate_per_frame(buf_c, frames),
            rate_per_second(buf_c, uptime_s));
    fprintf(f, "buffer_uploads_MB_total: %.2f | per_frame: %.4f\n", (double)buf_b / 1048576.0,
            (double)buf_b / 1048576.0 / (frames ? (double)frames : 1.0));
    fprintf(f, "texture_uploads_calls: %s | %.2f | %.1f\n", fmt_u64(tex_c).c_str(), rate_per_frame(tex_c, frames),
            rate_per_second(tex_c, uptime_s));
    fprintf(f, "texture_uploads_MB_total: %.2f | per_frame: %.4f\n", (double)tex_b / 1048576.0,
            (double)tex_b / 1048576.0 / (frames ? (double)frames : 1.0));
    fprintf(f, "state_changes (glEnable/glDisable): %s | %.2f | %.1f\n", fmt_u64(st_c).c_str(),
            rate_per_frame(st_c, frames), rate_per_second(st_c, uptime_s));
    fprintf(f, "use_program_calls: %s | %.2f | %.1f\n", fmt_u64(pr_c).c_str(), rate_per_frame(pr_c, frames),
            rate_per_second(pr_c, uptime_s));
    {
        // Since 2.3.0 the call counter alone overstates driver work: the
        // dispatcher already absorbed redundant switches. The split shows
        // both sides.
        const uint64_t pr_fwd = s.use_program_forwarded.load();
        const uint64_t pr_skip = s.use_program_skipped.load();
        fprintf(f, "use_program_forwarded: %s | %.2f | %.1f\n", fmt_u64(pr_fwd).c_str(),
                rate_per_frame(pr_fwd, frames), rate_per_second(pr_fwd, uptime_s));
        fprintf(f, "use_program_skipped_redundant: %s | %.2f | %.1f\n", fmt_u64(pr_skip).c_str(),
                rate_per_frame(pr_skip, frames), rate_per_second(pr_skip, uptime_s));
    }

    fprintf(f, "---- shaders ----\n");
    const uint64_t srcs = s.shader_sources.load();
    const uint64_t direct = s.shader_direct.load();
    const uint64_t hits = s.shader_cache_hits.load();
    const uint64_t miss = s.shader_cache_misses.load();
    const uint64_t translate_ms = s.shader_translate_ns.load() / 1000000ull;
    fprintf(f, "shader_sources_total: %s\n", fmt_u64(srcs).c_str());
    fprintf(f, "shader_direct_passthrough: %s\n", fmt_u64(direct).c_str());
    fprintf(f, "shader_cache_hits: %s\n", fmt_u64(hits).c_str());
    fprintf(f, "shader_cache_misses: %s\n", fmt_u64(miss).c_str());
    if (hits + miss > 0) {
        fprintf(f, "shader_cache_hit_rate: %.1f%%\n", 100.0 * (double)hits / (double)(hits + miss));
    }
    fprintf(f, "shader_translate_time_ms_total: %s\n", fmt_u64(translate_ms).c_str());
    if (hits + miss > 0) {
        fprintf(f, "shader_translate_time_ms_avg: %.2f\n", (double)translate_ms / (double)(hits + miss));
    }

    fprintf(f, "---- programs (driver-side compile/link, pipeline cache) ----\n");
    {
        const uint64_t c_exec = s.shader_compile_executed.load();
        const uint64_t c_def = s.shader_compile_deferred.load();
        const uint64_t c_st = s.shader_status_hits.load();
        const uint64_t c_ms = s.shader_compile_ns.load() / 1000000ull;
        fprintf(f, "shader_driver_compiles: %s | deferred_away: %s | status_cache_hits: %s\n",
                fmt_u64(c_exec).c_str(), fmt_u64(c_def).c_str(), fmt_u64(c_st).c_str());
        fprintf(f, "shader_driver_compile_ms_total: %s\n", fmt_u64(c_ms).c_str());
        const uint64_t link_ms = s.program_link_ns.load() / 1000000ull;
        fprintf(f, "program_link_ms_total: %s\n", fmt_u64(link_ms).c_str());
        const uint64_t b_hits = s.program_binary_hits.load();
        const uint64_t b_miss = s.program_binary_misses.load();
        const uint64_t b_fail = s.program_binary_failures.load();
        const uint64_t b_ms = s.program_binary_load_ns.load() / 1000000ull;
        fprintf(f, "program_binary_cache: hits %s | misses %s | load_failures %s\n", fmt_u64(b_hits).c_str(),
                fmt_u64(b_miss).c_str(), fmt_u64(b_fail).c_str());
        fprintf(f, "program_binary_load_ms_total: %s", fmt_u64(b_ms).c_str());
        if (b_hits > 0) fprintf(f, " | avg_per_hit: %.2f", (double)b_ms / (double)b_hits);
        fprintf(f, "\n");
    }

    // The last window is the interesting one for "what is happening now";
    // the history file holds every previous window including world load.
    fprintf(f, "---- last %.0fs window ----\n", (double)(kWindowNs / 1000000000ull));
    const uint64_t w_frames = frames - s.snap_frames;
    const double w_seconds = (double)(now_ns() - s.window_start_ns) / 1e9;
    fprintf(f, "window_frames: %llu | fps: %.2f\n", (unsigned long long)w_frames,
            w_seconds > 0.2 ? (double)w_frames / w_seconds : 0.0);
    fprintf(f, "window_frame_time_ms: avg %s | p95 %s | max %s\n",
            fmt_f(hist_avg_ms(s.window), "").c_str(), fmt_f(hist_percentile_ms(s.window, 0.95), "").c_str(),
            fmt_f(hist_max_ms(s.window), "").c_str());
    fprintf(f, "window_draws_per_frame: %.2f\n",
            rate_per_frame(s.draws.load() - s.snap_draws, w_frames));
    fprintf(f, "window_multidraw_subdraws_per_frame: %.2f\n",
            rate_per_frame(s.multidraw_subdraws.load() - s.snap_md_subdraws, w_frames));
    fprintf(f, "window_buffer_MB_per_frame: %.4f\n",
            (double)(s.buffer_bytes.load() - s.snap_buffer_bytes) / 1048576.0 / (w_frames ? (double)w_frames : 1.0));
    fprintf(f, "window_texture_MB_per_frame: %.4f\n",
            (double)(s.texture_bytes.load() - s.snap_texture_bytes) / 1048576.0 / (w_frames ? (double)w_frames : 1.0));
    fprintf(f, "window_state_changes_per_frame: %.2f\n",
            rate_per_frame(s.state_changes.load() - s.snap_state_changes, w_frames));
    fprintf(f, "window_use_program_per_frame: %.2f\n",
            rate_per_frame(s.use_program.load() - s.snap_use_program, w_frames));

    fprintf(f, "------------------------------------------------------------------\n");
    fprintf(f, "how_to_read: high cache_misses = caching work; high effective_draws_per_frame\n");
    fprintf(f, "  (>500) = batching/multidraw work; high buffer/texture MB per frame = upload\n");
    fprintf(f, "  staging work; high state_changes per frame (>200) = state tracking work.\n");
    fprintf(f, "pipeline cache: the FIRST run with a new shader pack must compile everything\n");
    fprintf(f, "  (binary misses); from the SECOND run on, program_binary_cache hits should\n");
    fprintf(f, "  replace nearly all driver compiles+links, and the world-load stall should\n");
    fprintf(f, "  shrink accordingly. use_program_calls counts arrivals; the forwarded line is\n");
    fprintf(f, "  what the driver actually saw.\n");
    fprintf(f, "Attach this file together with perf_history.log when reporting.\n");

    fclose(f);
    // Atomic replace: a reader never sees a half-written snapshot.
    rename(tmp.c_str(), path.c_str());
}

void close_window(PerfState& s) {
    std::lock_guard<std::mutex> lock(s.write_mutex);
    const uint64_t now = now_ns();
    const uint64_t uptime_s = (now - s.started_ns.load()) / 1000000000ull;
    const uint64_t w_frames = s.frames.load() - s.window_frames_at_start;
    const double w_seconds = (double)(now - s.window_start_ns) / 1e9;

    write_history_line(uptime_s, w_frames, w_seconds, s);
    write_report(s);

    // Open the next window.
    s.window_start_ns = now;
    s.window_frames_at_start = s.frames.load();
    s.snap_frames = s.frames.load();
    s.snap_draws = s.draws.load();
    s.snap_md_batches = s.multidraw_batches.load();
    s.snap_md_subdraws = s.multidraw_subdraws.load();
    s.snap_shader_sources = s.shader_sources.load();
    s.snap_shader_direct = s.shader_direct.load();
    s.snap_shader_hits = s.shader_cache_hits.load();
    s.snap_shader_misses = s.shader_cache_misses.load();
    s.snap_shader_translate_ns = s.shader_translate_ns.load();
    s.snap_buffer_calls = s.buffer_calls.load();
    s.snap_buffer_bytes = s.buffer_bytes.load();
    s.snap_texture_calls = s.texture_calls.load();
    s.snap_texture_bytes = s.texture_bytes.load();
    s.snap_state_changes = s.state_changes.load();
    s.snap_use_program = s.use_program.load();
    s.snap_binary_hits = s.program_binary_hits.load();
    s.snap_binary_misses = s.program_binary_misses.load();

    // Reset the window histogram for the next interval. Relaxed stores are
    // fine: only the swap thread writes, and the report writer runs under the
    // same mutex as this function.
    for (int i = 0; i <= kBucketCount; i++) s.window.buckets[i].store(0, std::memory_order_relaxed);
    s.window.count.store(0, std::memory_order_relaxed);
    s.window.sum_ns.store(0, std::memory_order_relaxed);
    s.window.max_ns.store(0, std::memory_order_relaxed);
}

} // namespace

void mg_perf_init() {
    bool on = global_settings.perf_log;
    int env_on = 0;
    if (GetEnvVarBool("MG_PERF_LOG", &env_on, 0)) on = env_on != 0;
    if (!on) return;

    g_perf.started_ns = now_ns();
    g_perf.last_present_ns = 0;
    g_perf.started_wall = time(nullptr);
    g_perf.window_start_ns = g_perf.started_ns;
    g_perf.window_frames_at_start = 0;

    // Remove the previous session's history so the file is always "this run".
    // The report itself is rewritten atomically anyway.
    remove(history_path().c_str());
    g_perf.history_header_written = false;

    mg_perf_active = 1;
    LOG_I("perf: recording enabled (perfLog); report every %d s in %s", (int)(kWindowNs / 1000000000ull),
          report_path().c_str());
}

void mg_perf_frame() {
    if (!mg_perf_active) return;
    const uint64_t now = now_ns();
    const uint64_t prev = g_perf.last_present_ns.exchange(now);
    if (prev == 0) return; // first present: no delta yet
    const uint64_t dt = now - prev;
    // Present gaps beyond a minute are the game pausing / loading screens /
    // window switches, not render performance; keep the frame count honest
    // but keep them out of the frame-time distribution.
    g_perf.frames.fetch_add(1, std::memory_order_relaxed);
    if (dt < 60ull * 1000000000ull) {
        g_perf.total.record(dt);
        g_perf.window.record(dt);
    }
    if (now - g_perf.window_start_ns >= kWindowNs) close_window(g_perf);
}

void mg_perf_add_draws(unsigned long long n) {
    g_perf.draws.fetch_add(n, std::memory_order_relaxed);
}

void mg_perf_add_multidraws(unsigned long long batches, unsigned long long subdraws) {
    g_perf.multidraw_batches.fetch_add(batches, std::memory_order_relaxed);
    g_perf.multidraw_subdraws.fetch_add(subdraws, std::memory_order_relaxed);
}

void mg_perf_add_shader_sources(unsigned long long n, unsigned long long direct) {
    g_perf.shader_sources.fetch_add(n, std::memory_order_relaxed);
    g_perf.shader_direct.fetch_add(direct, std::memory_order_relaxed);
}

void mg_perf_add_shader_translate(unsigned long long ns, int cache_hit) {
    if (cache_hit) {
        g_perf.shader_cache_hits.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_perf.shader_cache_misses.fetch_add(1, std::memory_order_relaxed);
    }
    g_perf.shader_translate_ns.fetch_add(ns, std::memory_order_relaxed);
}

void mg_perf_add_buffer_upload(unsigned long long calls, unsigned long long bytes) {
    g_perf.buffer_calls.fetch_add(calls, std::memory_order_relaxed);
    g_perf.buffer_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void mg_perf_add_texture_upload(unsigned long long calls, unsigned long long bytes) {
    g_perf.texture_calls.fetch_add(calls, std::memory_order_relaxed);
    g_perf.texture_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void mg_perf_add_state_change(unsigned long long n) {
    g_perf.state_changes.fetch_add(n, std::memory_order_relaxed);
}

void mg_perf_add_use_program(unsigned long long n) {
    g_perf.use_program.fetch_add(n, std::memory_order_relaxed);
}

void mg_perf_add_use_program_forwarded(unsigned long long n) {
    g_perf.use_program_forwarded.fetch_add(n, std::memory_order_relaxed);
}

void mg_perf_add_use_program_skipped(unsigned long long n) {
    g_perf.use_program_skipped.fetch_add(n, std::memory_order_relaxed);
}

void mg_perf_add_shader_compile_executed(unsigned long long ns) {
    g_perf.shader_compile_executed.fetch_add(1, std::memory_order_relaxed);
    g_perf.shader_compile_ns.fetch_add(ns, std::memory_order_relaxed);
}

void mg_perf_add_shader_compile_deferred(unsigned long long n) {
    g_perf.shader_compile_deferred.fetch_add(n, std::memory_order_relaxed);
}

void mg_perf_add_shader_status_hit(unsigned long long n) {
    g_perf.shader_status_hits.fetch_add(n, std::memory_order_relaxed);
}

void mg_perf_add_program_link(unsigned long long ns) {
    g_perf.program_link_ns.fetch_add(ns, std::memory_order_relaxed);
}

void mg_perf_add_program_binary(unsigned long long ns, int hit, int ok) {
    if (hit) {
        g_perf.program_binary_hits.fetch_add(1, std::memory_order_relaxed);
        g_perf.program_binary_load_ns.fetch_add(ns, std::memory_order_relaxed);
        if (!ok) g_perf.program_binary_failures.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_perf.program_binary_misses.fetch_add(1, std::memory_order_relaxed);
    }
}

void mg_perf_set_device_info(const char* renderer, const char* gles_version) {
    if (renderer && renderer[0]) g_perf.device_renderer = renderer;
    if (gles_version && gles_version[0]) g_perf.device_gles_version = gles_version;
}

unsigned long long mg_perf_now_ns() {
    return now_ns();
}
