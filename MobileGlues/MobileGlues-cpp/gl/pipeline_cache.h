// MobileGlues-RS - gl/pipeline_cache.h
// Copyright (c) 2026 MobileGlues-RS (fork of MobileGlues by MobileGL-Dev)
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
//
// Persistent program pipeline cache ("programBinaryCache").
//
// What problem this solves
// -----------------------
// The GLSL translation cache (gl/glsl/cache.cpp) persists the *translated ESSL
// text*, so the CPU side of a shader-pack load is already cheap on the second
// run. The driver side is not: every session still pays a full
// glCompileShader for every shader and a full glLinkProgram for every program,
// and on the reference device (Mali-G57, Helio G99) a shader pack toggle shows
// up as a multi-second frame stall -- the perf report measured a 10.95 s
// worst-case frame with 232 cache misses in one session.
//
// OpenGL ES 3.0 core exposes the way out: glGetProgramBinary hands back the
// driver's own linked program image, and glProgramBinary loads it back without
// compiling or linking anything. ANGLE ships the same mechanism as its program
// cache. This module stores those binaries (plus, per shader source, the
// driver's compile-status answers) in MG/pipeline_cache.bin, keyed by a hash
// over everything the binary depends on:
//
//   renderer version | GPU renderer string | GL version string | ES version
//   | texture-buffer emulation flag | every attached shader (type + final
//   translated source, in attach order) | pre-link attribute bindings (in
//   call order) | default-FS marker and source
//
// Deferred compilation
// --------------------
// To also skip the glCompileShader calls on a binary-cache hit, compilation of
// tracked shaders is deferred: glCompileShader only marks the shader pending.
// The driver compile happens lazily when something actually needs it:
//   * glGetShaderiv(GL_COMPILE_STATUS / GL_INFO_LOG_LENGTH) and
//     glGetShaderInfoLog: answered from the persisted per-source status record
//     when there is one, else the shader is compiled for real and the answers
//     are recorded;
//   * glLinkProgram with a binary-cache miss: every still-pending attached
//     shader is compiled first (a link needs compiled shader objects), then
//     the link runs as before.
// On a binary-cache hit none of that runs: glProgramBinary replaces both the
// compiles and the link, and the attached pending shaders are marked resolved.
//
// Safety properties
// -----------------
//  * Disabled (setting off or MG_NO_PROGBIN=1): glCompileShader is a plain
//    passthrough and glLinkProgram runs the historical path; behaviour is
//    byte-for-byte the pre-2.3.0 renderer.
//  * A glProgramBinary that fails to load or verify falls straight through to
//    the compile-and-link path, so a stale or hostile cache file can at worst
//    cost performance, never correctness.
//  * Only links the driver itself reported successful are stored.
//  * Shaders whose compile is still deferred are never evicted from the
//    tracking tables, so eviction cannot turn a link into a surprise failure.
//  * The whole store is behind one mutex: shader/program calls are
//    context-thread bound but several contexts can exist, and unlike the
//    upstream per-program maps this table also owns disk I/O.
//
// This header is the whole public surface; the GL orchestration lives in
// gl/shader.cpp and gl/program.cpp, the pure store (host-testable, no GL)
// lives in gl/pipeline_cache.cpp.
#ifndef MOBILEGLUES_PIPELINE_CACHE_H
#define MOBILEGLUES_PIPELINE_CACHE_H

#include <cstdint>
#include <string>
#include <vector>

#include "mg.h"

namespace pipeline_cache {

// True when the feature is on: config "programBinaryCache" (default on) and
// no MG_NO_PROGBIN=1 override. Cheap (one bool read); resolved once.
bool enabled();

// --- shader-side tracking (called from gl/shader.cpp and gl/program.cpp) ---

struct ShaderRecord {
    std::string source;      // final translated ESSL as uploaded to the driver
    GLenum shader_type = 0;  // captured at glAttachShader time
    bool compile_pending = false;  // deferred and not yet compiled by the driver
    bool resolved_by_binary = false; // the owning link came from a cached binary
};

// A recycled shader name must not inherit the previous object's record: a
// glCompileShader with no glShaderSource before it has to stay an
// (upstream-identical) compile of nothing instead of the old source.
void on_shader_created(GLuint shader);

// Records the final translated source (after every patch the layer applies).
void on_source_uploaded(GLuint shader, const std::string& essl, GLenum shader_type);

// glCompileShader: marks the shader pending (deferred). Returns false when the
// shader is not tracked (no source record) -- the caller then compiles it for
// real, exactly like upstream.
bool defer_compile(GLuint shader);

// Snapshot access. Copy on purpose: the caller (context thread) uses it across
// GL calls while another context's thread may mutate the table.
bool get_shader_record(GLuint shader, ShaderRecord& out);

// Marks the shader as really compiled (pending cleared, not binary-resolved).
void mark_compiled(GLuint shader);

// Marks every pending shader attached to `program` as resolved by a binary
// load: their compile status answers as TRUE for the rest of the session.
void mark_attached_resolved_by_binary(GLuint program);

// --- persisted per-source compile answers --------------------------------
//
// Keyed by the source text (plus ES version and the emulation flag), not by
// the shader name, so a second shader object with the same source -- the
// shader-pack toggle-off/on cycle produces them by the dozen -- is answered
// without compiling again, even within one session.
struct ShaderStatus {
    GLint compile_status = 0;    // GL_TRUE / GL_FALSE as the driver reported it
    GLint info_log_length = 0;   // the driver's own GL_INFO_LOG_LENGTH answer
    std::string info_log;        // the driver's own info log text
};

std::string shader_status_key(const std::string& source);

void record_shader_status(const std::string& source, GLint status, GLint log_length, const std::string& log);

bool lookup_shader_status(const std::string& source, ShaderStatus& out);

// --- program-side tracking ------------------------------------------------

struct ProgramRecord {
    std::vector<GLuint> attached;                          // attach call order
    std::vector<std::pair<GLuint, std::string>> attrib_bindings; // call order
};

void on_program_created(GLuint program);
void on_attach(GLuint program, GLuint shader, GLenum type);
void on_detach(GLuint program, GLuint shader);
void on_attrib_binding(GLuint program, GLuint index, const char* name);
bool get_program_record(GLuint program, ProgramRecord& out);

// --- program binary store ---------------------------------------------------

// Everything a linked image depends on, supplied by gl/program.cpp at link
// time (device identity is captured there where a context is current).
struct LinkKeyContext {
    uint es_version = 0;
    bool emulate_texture_buffer = false;
    bool default_fs_attached = false;
    std::string default_fs_source; // the generated default FS text, when attached
    std::string device_renderer;   // glGetString(GL_RENDERER) or ""
    std::string device_version;    // glGetString(GL_VERSION) or ""
};

// Builds the 32-byte key. Reads the attached shaders' tracked sources, so it
// must be called before anything clears their records.
std::vector<uint8_t> build_program_key(GLuint program, const LinkKeyContext& ctx);

void store_program_binary(const std::vector<uint8_t>& key, GLenum format, const void* data, size_t len);

bool lookup_program_binary(const std::vector<uint8_t>& key, GLenum* format, std::vector<uint8_t>& out);

// Flushes the deferred disk write immediately (used by the host test).
void flush_for_test();

// Drops all in-memory state (after flushing) and marks the store unloaded so
// the next access reloads from disk -- lets the host test verify persistence
// inside one process. Test-only; the renderer never calls it.
void reload_for_test();

} // namespace pipeline_cache

#endif // MOBILEGLUES_PIPELINE_CACHE_H
