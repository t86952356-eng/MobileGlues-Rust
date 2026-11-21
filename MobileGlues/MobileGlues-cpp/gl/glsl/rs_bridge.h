// MobileGlues-RS - gl/glsl/rs_bridge.h
// Copyright (c) 2026 MobileGlues-RS (fork of MobileGlues by MobileGL-Dev)
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#ifndef MOBILEGLUES_RS_BRIDGE_H
#define MOBILEGLUES_RS_BRIDGE_H

#include <optional>
#include <string>

// Bridge to the Rust text-processing core (mobileglues-rs).
//
// The Rust symbols are declared weak: when libmobileglues_rs.a is linked
// into libmobileglues.so (CMake finds cargo and the Rust targets) the
// pointers resolve; otherwise they stay null and every dispatcher below
// reports "unavailable" so the original C++ implementations run. That keeps
// a Rust-less build (no cargo on the machine, -DMG_ENABLE_RUST=OFF) fully
// functional.
//
// Runtime escape hatch: MG_RS_DISABLE=1 forces the C++ paths even when the
// Rust core is linked, for A/B comparison and emergency fallback.

#if defined(__GNUC__) || defined(__clang__)
#define MG_RS_WEAK __attribute__((weak))
#else
#define MG_RS_WEAK
// MSVC has no weak symbols: build as if the Rust core were absent.
#define MG_RS_NO_WEAK 1
#endif

extern "C" {
MG_RS_WEAK int mg_rs_available();
MG_RS_WEAK const char* mg_rs_version();
MG_RS_WEAK void mg_rs_free(char* ptr);
MG_RS_WEAK int mg_rs_get_glsl_version(const char* src);
MG_RS_WEAK char* mg_rs_remove_layout_binding(const char* src);
MG_RS_WEAK char* mg_rs_process_out_color_locations(const char* src);
MG_RS_WEAK char* mg_rs_force_supported_output(const char* src);
MG_RS_WEAK char* mg_rs_process_sampler_buffer_rewrite(const char* src);
MG_RS_WEAK long long mg_rs_find_uniform_insert_pos(const char* src);
MG_RS_WEAK int mg_rs_has_texture_query_lod_def(const char* src);
MG_RS_WEAK int mg_rs_has_temporal_filter_def(const char* src);
}

namespace rs {

// True when the Rust core is linked AND not disabled via MG_RS_DISABLE.
bool enabled();

// Human-readable identity for logs: "mobileglues-rs/0.1.0" or "cpp-fallback".
const char* version();

// Each dispatcher returns std::nullopt when the Rust core is unavailable,
// disabled, or returned a failure value (panic / null input): the caller then
// runs its original C++ implementation. No exceptions cross the boundary.

std::optional<int> get_glsl_version(const std::string& src);
std::optional<std::string> remove_layout_binding(const std::string& src);
std::optional<std::string> process_out_color_locations(const std::string& src);
std::optional<std::string> force_supported_output(const std::string& src);
std::optional<std::string> process_sampler_buffer_rewrite(const std::string& src);
std::optional<size_t> find_uniform_insert_pos(const std::string& src);
std::optional<bool> has_texture_query_lod_def(const std::string& src);
std::optional<bool> has_temporal_filter_def(const std::string& src);

} // namespace rs

#endif // MOBILEGLUES_RS_BRIDGE_H
