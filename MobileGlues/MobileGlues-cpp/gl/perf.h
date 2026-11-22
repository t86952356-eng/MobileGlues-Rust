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

#endif // MOBILEGLUES_PERF_H
