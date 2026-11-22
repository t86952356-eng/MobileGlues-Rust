// MobileGlues-RS - gl/perf.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
//
// In-game performance log ("perfLog").
//
// When the user turns the option on, this module records what the renderer does
// while the game runs: frame times from the swap path, and call/byte counters
// from the GL entry points (draws, multidraws, shader translations with cache
// hit/miss, buffer and texture uploads, state changes, program switches).
//
// Two files appear under the MG directory:
//   perf_report.txt   - full snapshot (header + cumulative + last window),
//                       rewritten atomically every PERF_WINDOW_SECONDS.
//   perf_history.log  - one CSV line per window, kept across the session,
//                       so the world-loading phase is never lost.
//
// Everything is opt-in and compiled in unconditionally: when the option is off,
// mg_perf_active stays 0 and every hook below costs one predictable branch.
#ifndef MOBILEGLUES_PERF_H
#define MOBILEGLUES_PERF_H

#ifdef __cplusplus
extern "C"
{
#endif

    // 0 = inactive (default), 1 = recording. Written once at init; read on
    // every hook. Plain int on purpose: a relaxed atomic load and a load of a
    // never-written int cost the same here, and the C call sites stay simple.
    extern int mg_perf_active;

    // Call once at process init, AFTER init_settings() (it needs the config).
    // Reads global_settings.perf_log, honours the MG_PERF_LOG env override for
    // A/B runs, and opens the report files when active.
    void mg_perf_init();

    // --- recording hooks ---------------------------------------------------

    // Present path (egl presentSurface): one call per presented frame.
    void mg_perf_frame();

    // Single draw calls (glDraw* family). Counted in prepareForDraw() and the
    // glDrawArrays passthrough so every entry point is covered by two sites.
    void mg_perf_add_draws(unsigned long long n);

    // Multi-draw batches and how many sub-draws each expanded to.
    void mg_perf_add_multidraws(unsigned long long batches, unsigned long long subdraws);

    // glShaderSource: one per submitted source; direct = passed through
    // unconverted (ES-capable already).
    void mg_perf_add_shader_sources(unsigned long long n, unsigned long long direct);

    // One GLSL->GLSLES translation attempt, with the nanoseconds the whole
    // lookup-or-translate took. cache_hit tells which side of the cache it was.
    void mg_perf_add_shader_translate(unsigned long long ns, int cache_hit);

    // Buffer uploads (glBufferData / glBufferSubData).
    void mg_perf_add_buffer_upload(unsigned long long calls, unsigned long long bytes);

    // Texture uploads (glTexImage* / glTexSubImage*).
    void mg_perf_add_texture_upload(unsigned long long calls, unsigned long long bytes);

    // State changes (glEnable / glDisable) and program switches (glUseProgram).
    void mg_perf_add_state_change(unsigned long long n);
    void mg_perf_add_use_program(unsigned long long n);

    // Since 2.3.0 -- split of the useProgram counter: how many of the arriving
    // glUseProgram calls were actually forwarded to the driver because the
    // tracked program really changed, and how many were redundant (skipped).
    void mg_perf_add_use_program_forwarded(unsigned long long n);
    void mg_perf_add_use_program_skipped(unsigned long long n);

    // Since 2.3.0 -- driver-side shader compile path (pipeline cache):
    //   executed: a real glCompileShader ran on the driver, `ns` is its cost
    //   deferred: a glCompileShader was absorbed by the deferral (no driver
    //             call; the object's answers exist in the status cache or the
    //             program will come from a cached binary)
    //   status_hit: a compile-status / info-log query was answered from the
    //             persisted record without compiling
    void mg_perf_add_shader_compile_executed(unsigned long long ns);
    void mg_perf_add_shader_compile_deferred(unsigned long long n);
    void mg_perf_add_shader_status_hit(unsigned long long n);

    // Since 2.3.0 -- driver-side link path (pipeline cache). `hit` is 1 when a
    // cached program binary was tried; `ok` is 1 when it loaded and linked.
    // `ns` is the glProgramBinary cost on a hit, 0 otherwise.
    void mg_perf_add_program_link(unsigned long long ns);
    void mg_perf_add_program_binary(unsigned long long ns, int hit, int ok);

    // Device identity, captured once a real GLES context exists (set_es_version).
    // Strings are copied; passing null keeps whatever was recorded before.
    void mg_perf_set_device_info(const char* renderer, const char* gles_version);

    // Monotonic clock in nanoseconds (used by the shader timing hook).
    unsigned long long mg_perf_now_ns();

#ifdef __cplusplus
}
#endif

// Hook macros: the disabled path is a single load + branch that predicts
// perfectly, so GL entry points keep their cost even in the default state.
#define MG_PERF_DRAWS(n)                                                                                                 \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_draws(n);                                                                        \
    } while (0)
#define MG_PERF_MULTIDRAWS(b, s)                                                                                         \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_multidraws(b, s);                                                                \
    } while (0)
#define MG_PERF_SHADER_SOURCES(n, d)                                                                                     \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_shader_sources(n, d);                                                            \
    } while (0)
#define MG_PERF_BUFFER(c, b)                                                                                             \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_buffer_upload(c, b);                                                             \
    } while (0)
#define MG_PERF_TEXTURE(c, b)                                                                                            \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_texture_upload(c, b);                                                            \
    } while (0)
#define MG_PERF_STATE(n)                                                                                                 \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_state_change(n);                                                                 \
    } while (0)
#define MG_PERF_PROGRAM(n)                                                                                               \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_use_program(n);                                                                  \
    } while (0)
#define MG_PERF_PROGRAM_FORWARDED(n)                                                                                     \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_use_program_forwarded(n);                                                        \
    } while (0)
#define MG_PERF_PROGRAM_SKIPPED(n)                                                                                       \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_use_program_skipped(n);                                                          \
    } while (0)
#define MG_PERF_SHADER_COMPILE_EXECUTED(ns)                                                                              \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_shader_compile_executed(ns);                                                     \
    } while (0)
#define MG_PERF_SHADER_COMPILE_DEFERRED()                                                                                \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_shader_compile_deferred(1);                                                      \
    } while (0)
#define MG_PERF_SHADER_STATUS_HIT()                                                                                      \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_shader_status_hit(1);                                                            \
    } while (0)
#define MG_PERF_PROGRAM_LINK(ns)                                                                                         \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_program_link(ns);                                                                \
    } while (0)
#define MG_PERF_PROGRAM_BINARY(ns, hit, ok)                                                                              \
    do {                                                                                                                 \
        if (mg_perf_active) mg_perf_add_program_binary(ns, hit, ok);                                                     \
    } while (0)

#endif // MOBILEGLUES_PERF_H
