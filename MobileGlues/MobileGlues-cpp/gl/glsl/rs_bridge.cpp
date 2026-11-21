// MobileGlues-RS - gl/glsl/rs_bridge.cpp
// Copyright (c) 2026 MobileGlues-RS (fork of MobileGlues by MobileGL-Dev)
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include "rs_bridge.h"
#include <cstdlib>

namespace {

// RAII holder for strings handed back by the Rust core; the buffer is freed
// on the Rust side through mg_rs_free so the two allocators never mix.
class RsString {
public:
    explicit RsString(char* p) : ptr_(p) {}
    ~RsString() {
        if (ptr_ && mg_rs_free) mg_rs_free(ptr_);
    }
    RsString(const RsString&) = delete;
    RsString& operator=(const RsString&) = delete;
    bool ok() const { return ptr_ != nullptr; }
    std::string str() const { return ptr_ ? std::string(ptr_) : std::string(); }

private:
    char* ptr_;
};

bool rs_linked() {
#if defined(MG_RS_NO_WEAK)
    return false;
#else
    // The availability symbol is always exported by the Rust core, so a
    // non-null pointer means the whole FFI surface resolved.
    return mg_rs_available != nullptr && mg_rs_available() == 1;
#endif
}

bool rs_allowed() {
    static const int disabled = (getenv("MG_RS_DISABLE") != nullptr && getenv("MG_RS_DISABLE")[0] == '1');
    return !disabled;
}

} // namespace

namespace rs {

bool enabled() {
    static const bool linked = rs_linked();
    return linked && rs_allowed();
}

const char* version() {
    if (rs_linked() && mg_rs_version != nullptr) return mg_rs_version();
    return "cpp-fallback";
}

std::optional<int> get_glsl_version(const std::string& src) {
    if (!enabled() || mg_rs_get_glsl_version == nullptr) return std::nullopt;
    const int v = mg_rs_get_glsl_version(src.c_str());
    if (v < -1) return std::nullopt; // -2: Rust failed, fall back
    return v;
}

std::optional<std::string> remove_layout_binding(const std::string& src) {
    if (!enabled() || mg_rs_remove_layout_binding == nullptr) return std::nullopt;
    RsString r(mg_rs_remove_layout_binding(src.c_str()));
    if (!r.ok()) return std::nullopt;
    return r.str();
}

std::optional<std::string> process_out_color_locations(const std::string& src) {
    if (!enabled() || mg_rs_process_out_color_locations == nullptr) return std::nullopt;
    RsString r(mg_rs_process_out_color_locations(src.c_str()));
    if (!r.ok()) return std::nullopt;
    return r.str();
}

std::optional<std::string> force_supported_output(const std::string& src) {
    if (!enabled() || mg_rs_force_supported_output == nullptr) return std::nullopt;
    RsString r(mg_rs_force_supported_output(src.c_str()));
    if (!r.ok()) return std::nullopt;
    return r.str();
}

std::optional<std::string> process_sampler_buffer_rewrite(const std::string& src) {
    if (!enabled() || mg_rs_process_sampler_buffer_rewrite == nullptr) return std::nullopt;
    RsString r(mg_rs_process_sampler_buffer_rewrite(src.c_str()));
    if (!r.ok()) return std::nullopt;
    return r.str();
}

std::optional<size_t> find_uniform_insert_pos(const std::string& src) {
    if (!enabled() || mg_rs_find_uniform_insert_pos == nullptr) return std::nullopt;
    const long long v = mg_rs_find_uniform_insert_pos(src.c_str());
    if (v < 0) return std::nullopt; // -1: Rust failed, fall back
    return static_cast<size_t>(v);
}

std::optional<bool> has_texture_query_lod_def(const std::string& src) {
    if (!enabled() || mg_rs_has_texture_query_lod_def == nullptr) return std::nullopt;
    const int v = mg_rs_has_texture_query_lod_def(src.c_str());
    if (v < 0) return std::nullopt;
    return v != 0;
}

std::optional<bool> has_temporal_filter_def(const std::string& src) {
    if (!enabled() || mg_rs_has_temporal_filter_def == nullptr) return std::nullopt;
    const int v = mg_rs_has_temporal_filter_def(src.c_str());
    if (v < 0) return std::nullopt;
    return v != 0;
}

} // namespace rs
