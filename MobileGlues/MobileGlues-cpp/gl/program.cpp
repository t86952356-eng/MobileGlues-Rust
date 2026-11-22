// MobileGlues - gl/program.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include <regex.h>
#include "GL/glext.h"
#include "GLES3/gl32.h"
#include "log.h"
#include "shader.h"
#include "program.h"
#include "pipeline_cache.h"
#include <regex>
#include <cstring>
#include <iostream>
#include <vector>
#include "../config/settings.h"
#include "drawing.h"
#include "perf.h"

#define DEBUG 0

extern UnorderedMap<GLuint, bool> shader_map_is_sampler_buffer_emulated;
UnorderedMap<GLuint, bool> program_map_is_sampler_buffer_emulated;

enum class ShouldGenerateFSState : int {
    Never = 0,
    Maybe = 1,
    Unknown = 2
};

UnorderedMap<GLuint, ShouldGenerateFSState> program_map_should_generate_fs;

std::string updateLayoutLocation(const std::string& esslSource, GLuint color, const char* name) {
    const std::string& shaderCode = esslSource;

    std::string pattern = std::string(R"((layout\s*$[^)]*location\s*=\s*\d+[^)]*$\s*)?)") +
                          R"(out\s+((?:highp|mediump|lowp|\w+\s+)*\w+)\s+)" + name + R"(\s*;)";

    std::string replacement = "layout (location = " + std::to_string(color) + ") out $2 " + name + ";";

    std::regex reg(pattern);
    std::string modifiedCode = std::regex_replace(shaderCode, reg, replacement);

    return modifiedCode;
}

void glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name) {
    LOG()
    LOG_D("glBindFragDataLocation(%d, %d, %s)", program, color, name)

    if (strlen(name) > 8 && strncmp(name, "outColor", 8) == 0) {
        const char* numberStr = name + 8;
        bool isNumber = true;
        for (int i = 0; numberStr[i] != '\0'; ++i) {
            if (!isdigit(numberStr[i])) {
                isNumber = false;
                break;
            }
        }

        if (isNumber) {
            unsigned int extractedColor = static_cast<unsigned int>(std::stoul(numberStr));
            if (extractedColor == color) {
                // outColor was bound in glsl process. exit now
                LOG_D("Find outColor* with color *, skipping")
                return;
            }
        }
    }

    // Copied before the call, not aliased into it: the result is assigned back
    // over the same member that supplies the input.
    const std::string origin_glsl =
        shaderInfo.frag_data_changed ? shaderInfo.frag_data_changed_converted : shaderInfo.converted;

    shaderInfo.frag_data_changed_converted = updateLayoutLocation(origin_glsl, color, name);
    shaderInfo.frag_data_changed = 1;
}

static std::string DefaultFSSource;
static unsigned CurrentDefaultFSSourceVersion = 0; // the version (hardware->es_version) may change during runtime

void GenerateDefaultFSSource() {
    if (CurrentDefaultFSSourceVersion != hardware->es_version) {
        CurrentDefaultFSSourceVersion = hardware->es_version;
        std::ostringstream ss;
        ss << "#version " << CurrentDefaultFSSourceVersion << " es\n";
        ss << "precision mediump float;\n\n";
        ss << "out vec4 fragColor;\n\n";
        ss << "void main() {\n";
        ss << "    fragColor = vec4(1.0, 1.0, 1.0, 1.0);\n";
        ss << "}\n";

        DefaultFSSource = ss.str();
    }
}

static UnorderedMap<unsigned, GLuint> DefaultFSMap; // essl version <-> shader id

// Pipeline-cache helpers for glLinkProgram (defined below glCreateProgram).
static void force_pending_attached(GLuint program);
static void store_program_binary_after_link(GLuint program, const std::vector<uint8_t>& key);

void glLinkProgram(GLuint program) {
    LOG()

    LOG_D("glLinkProgram(%d)", program)
    if (!shaderInfo.converted.empty() && shaderInfo.frag_data_changed) {
        const GLchar* patched = shaderInfo.frag_data_changed_converted.c_str();
        GLES.glShaderSource(shaderInfo.id, 1, &patched, nullptr);
        const unsigned long long patch_t0 = mg_perf_now_ns();
        GLES.glCompileShader(shaderInfo.id);
        MG_PERF_SHADER_COMPILE_EXECUTED(mg_perf_now_ns() - patch_t0);
        GLint status = 0;
        GLES.glGetShaderiv(shaderInfo.id, GL_COMPILE_STATUS, &status);
        std::string patch_log;
        if (status != GL_TRUE) {
            char tmp[500];
            GLES.glGetShaderInfoLog(shaderInfo.id, 500, nullptr, tmp);
            patch_log = tmp;
            LOG_E("Failed to compile patched shader, log:\n%s", tmp)
        }
        GLES.glDetachShader(program, shaderInfo.id);
        GLES.glAttachShader(program, shaderInfo.id);
        // The patched text replaces the tracked source: a program binary
        // depends on exactly what the driver compiled, patches included. The
        // compile answers are recorded against the patched source so a later
        // status query is replayed instead of recompiled.
        pipeline_cache::on_source_uploaded(shaderInfo.id, shaderInfo.frag_data_changed_converted, 0);
        pipeline_cache::record_shader_status(shaderInfo.frag_data_changed_converted, status,
                                             static_cast<GLint>(patch_log.size()), patch_log);
        pipeline_cache::on_detach(program, shaderInfo.id);
        pipeline_cache::on_attach(program, shaderInfo.id, 0);
        CHECK_GL_ERROR
    }
    shaderInfo.id = 0;
    shaderInfo.converted = "";
    shaderInfo.frag_data_changed_converted.clear();
    shaderInfo.frag_data_changed = 0;

    // Generate defaut fragment shader if needed
    bool default_fs_attached = false;
    if (program_map_should_generate_fs[program] == ShouldGenerateFSState::Maybe) {
        GenerateDefaultFSSource();
        GLuint& default_fs = DefaultFSMap[CurrentDefaultFSSourceVersion];
        if (!default_fs) {
            default_fs = GLES.glCreateShader(GL_FRAGMENT_SHADER);
            const char* src = DefaultFSSource.c_str();
            GLES.glShaderSource(default_fs, 1, &src, nullptr);

            GLES.glCompileShader(default_fs);

            GLint success = 0;
            GLES.glGetShaderiv(default_fs, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLint logLength = 0;
                GLES.glGetShaderiv(default_fs, GL_INFO_LOG_LENGTH, &logLength);
                std::vector<char> log(logLength);
                GLES.glGetShaderInfoLog(default_fs, logLength, nullptr, log.data());
                LOG_E("Default fragment shader compile error for program %u :\n%s\n", program, log.data());
                GLES.glDeleteShader(default_fs);
                default_fs = 0;
            }
        }

        if (default_fs) {
            LOG_D("Try to attach missing default FS for program %u...", program);
            GLES.glAttachShader(program, default_fs);
            default_fs_attached = true;
        }
    }

    // Pipeline cache (2.3.0): a cached program binary replaces every driver
    // compile and the link itself. On a hit the attached shaders are marked
    // resolved (their status queries answer TRUE without compiling); on a
    // miss, or when a stored binary fails to load, the historical path runs:
    // force-compile anything still deferred (a link needs compiled objects),
    // link, and store the fresh binary for the next session.
    if (pipeline_cache::enabled()) {
        std::vector<uint8_t> key;
        {
            pipeline_cache::LinkKeyContext ctx;
            ctx.es_version = hardware->es_version;
            ctx.emulate_texture_buffer = hardware->emulate_texture_buffer;
            ctx.default_fs_attached = default_fs_attached;
            if (default_fs_attached) ctx.default_fs_source = DefaultFSSource;
            const GLubyte* renderer = GLES.glGetString(GL_RENDERER);
            const GLubyte* version = GLES.glGetString(GL_VERSION);
            ctx.device_renderer = renderer ? reinterpret_cast<const char*>(renderer) : "";
            ctx.device_version = version ? reinterpret_cast<const char*>(version) : "";
            key = pipeline_cache::build_program_key(program, ctx);
        }

        GLenum binary_format = 0;
        std::vector<uint8_t> binary;
        if (pipeline_cache::lookup_program_binary(key, &binary_format, binary)) {
            const unsigned long long t0 = mg_perf_now_ns();
            GLES.glProgramBinary(program, binary_format, binary.data(), static_cast<GLsizei>(binary.size()));
            GLint linked = GL_FALSE;
            GLES.glGetProgramiv(program, GL_LINK_STATUS, &linked);
            MG_PERF_PROGRAM_BINARY(mg_perf_now_ns() - t0, 1, linked == GL_TRUE ? 1 : 0);
            if (linked == GL_TRUE) {
                LOG_D("Program %d restored from the pipeline cache (binary %zu bytes)", program, binary.size())
                pipeline_cache::mark_attached_resolved_by_binary(program);
                CHECK_GL_ERROR
                return;
            }
            // A binary that fails to load or verify is the one risk this cache
            // carries, and this is where it dies: fall through to a full
            // compile+link exactly as if nothing was cached.
            LOG_W("Pipeline cache binary rejected for program %d; falling back to compile+link", program)
        } else {
            MG_PERF_PROGRAM_BINARY(0, 0, 0);
        }

        // Miss: a real link needs every attached shader compiled by the driver.
        force_pending_attached(program);

        const unsigned long long link_t0 = mg_perf_now_ns();
        GLES.glLinkProgram(program);
        MG_PERF_PROGRAM_LINK(mg_perf_now_ns() - link_t0);
        CHECK_GL_ERROR

        store_program_binary_after_link(program, key);
        return;
    }

    const unsigned long long link_t0 = mg_perf_now_ns();
    GLES.glLinkProgram(program);
    MG_PERF_PROGRAM_LINK(mg_perf_now_ns() - link_t0);

    CHECK_GL_ERROR
}

void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    LOG()
    GLES.glGetProgramiv(program, pname, params);
    if (global_settings.ignore_error >= IgnoreErrorLevel::Partial &&
        (pname == GL_LINK_STATUS || pname == GL_VALIDATE_STATUS) && !*params) {
        GLchar infoLog[512];
        GLES.glGetProgramInfoLog(program, 512, nullptr, infoLog);

        LOG_W_FORCE("Program %d linking failed: \n%s", program, infoLog);
        LOG_W_FORCE("Now try to cheat.");
        *params = GL_TRUE;
    }
    CHECK_GL_ERROR
}

void glUseProgram(GLuint program) {
    LOG()
    LOG_D("glUseProgram(%d)", program)
    MG_PERF_PROGRAM(1);
    if (program != gl_state->current_program) {
        gl_state->current_program = program;
        MG_PERF_PROGRAM_FORWARDED(1);
        GLES.glUseProgram(program);
        CHECK_GL_ERROR
    } else {
        // The tracked program already matches: the driver never sees this
        // call. Counted separately since 2.3.0 so the perf report can show
        // the real driver-side rate.
        MG_PERF_PROGRAM_SKIPPED(1);
    }
}

void glAttachShader(GLuint program, GLuint shader) {
    LOG()
    LOG_D("glAttachShader(%u, %u)", program, shader)
    if (hardware->emulate_texture_buffer && shader_map_is_sampler_buffer_emulated[shader])
        program_map_is_sampler_buffer_emulated[program] = true;

    GLint type = 0;
    GLES.glGetShaderiv(shader, GL_SHADER_TYPE, &type);
    auto& should_gen_fs_map = program_map_should_generate_fs;
    if (type == GL_FRAGMENT_SHADER) {
        should_gen_fs_map[program] = ShouldGenerateFSState::Never;
    } else if (type == GL_VERTEX_SHADER) {
        auto it = should_gen_fs_map.find(program);
        if (it == should_gen_fs_map.end() || should_gen_fs_map[program] != ShouldGenerateFSState::Never) {
            should_gen_fs_map[program] = ShouldGenerateFSState::Maybe;
        }
    }

    // Pipeline cache: the attachment set (with types) is part of the program
    // binary's key material.
    pipeline_cache::on_attach(program, shader, static_cast<GLenum>(type));

    GLES.glAttachShader(program, shader);
    CHECK_GL_ERROR
}

// glDetachShader (intercepted since 2.3.0 for the pipeline cache): a detached
// shader is no longer part of the program binary's key material.
void glDetachShader(GLuint program, GLuint shader) {
    LOG()
    LOG_D("glDetachShader(%u, %u)", program, shader)
    GLES.glDetachShader(program, shader);
    pipeline_cache::on_detach(program, shader);
    CHECK_GL_ERROR
}

// glBindAttribLocation (intercepted since 2.3.0 for the pipeline cache):
// pre-link attribute bindings change the linked image, so they are recorded
// (in call order) as key material. Behaviour is otherwise unchanged.
void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    LOG()
    LOG_D("glBindAttribLocation(%u, %u, %s)", program, index, name ? name : "<null>")
    pipeline_cache::on_attrib_binding(program, index, name);
    GLES.glBindAttribLocation(program, index, name);
    CHECK_GL_ERROR
}

// The aliases gl_native.cpp's macro used to emit for the non-Apple platforms;
// glXGetProcAddress can hand these names out, so they stay.
extern "C" GLAPI GLAPIENTRY void glBindAttribLocationARB(GLuint program, GLuint index, const GLchar* name)
    __attribute__((alias("glBindAttribLocation")));
extern "C" GLAPI GLAPIENTRY void glDetachShaderARB(GLuint program, GLuint shader)
    __attribute__((alias("glDetachShader")));

extern UnorderedMap<GLuint, SamplerInfo> g_samplerCacheForSamplerBuffer;

GLuint glCreateProgram() {
    LOG()
    LOG_D("glCreateProgram")
    GLuint program = GLES.glCreateProgram();
    if (hardware->emulate_texture_buffer) {
        program_map_is_sampler_buffer_emulated[program] = false;
        if (g_samplerCacheForSamplerBuffer.find(program) != g_samplerCacheForSamplerBuffer.end()) {
            g_samplerCacheForSamplerBuffer.erase(program);
        }
    }
    program_map_should_generate_fs[program] = ShouldGenerateFSState::Unknown;
    // A recycled program name must not inherit the previous object's
    // attachment/binding record.
    pipeline_cache::on_program_created(program);

    CHECK_GL_ERROR
    return program;
}

// Pipeline-cache helpers used by glLinkProgram above.

static void force_pending_attached(GLuint program) {
    pipeline_cache::ProgramRecord record;
    if (!pipeline_cache::get_program_record(program, record)) return;
    for (GLuint shader : record.attached) {
        pipeline_cache::ShaderRecord rec;
        if (pipeline_cache::get_shader_record(shader, rec) && rec.compile_pending && !rec.resolved_by_binary) {
            LOG_D("Forcing deferred compile of shader %u for program %u", shader, program)
            mg_force_compile_shader(shader);
        }
    }
}

static void store_program_binary_after_link(GLuint program, const std::vector<uint8_t>& key) {
    GLint linked = GL_FALSE;
    GLES.glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) return; // only links the driver itself accepted are cached

    GLint length = 0;
    GLES.glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0) return; // driver does not hand out binaries for this program

    // Sanity cap per entry: a shader-pack program runs to hundreds of KB, but
    // anything into the tens of MB is a driver anomaly, not a cache asset.
    constexpr GLint kMaxEntryBytes = 8 * 1024 * 1024;
    if (length > kMaxEntryBytes) return;

    std::vector<uint8_t> buffer(static_cast<size_t>(length));
    GLenum format = 0;
    GLsizei written = 0;
    GLES.glGetProgramBinary(program, length, &written, &format, buffer.data());
    if (written <= 0) return;

    pipeline_cache::store_program_binary(key, format, buffer.data(), static_cast<size_t>(written));
    LOG_D("Stored program %u binary (%d bytes, format 0x%x) in the pipeline cache", program, written, format)
}

// GL 3.1's name-only half of the active-uniform query, on top of the ES call that
// already returns the same string.
//
// It was a stub -- a no-op that wrote neither the name nor the length and, being a
// stub rather than an error, left glGetError clean. Callers got whatever was
// already in the buffer they passed.
//
// That is not a cosmetic gap. The standard way to build a name -> location map is
// to walk the active uniforms by index and ask for each name, and a caller doing
// that ended up with a map keyed on garbage: every later lookup missed, so the
// uniforms never got set and kept whatever the driver had zero-initialised them
// to. NeoForge's early loading window does exactly this, and a screenSize of
// (0, 0) turned its every vertex into a division by zero -- gl_Position came out
// non-finite, every primitive was discarded, and the window rendered black with
// nothing anywhere reporting a problem.
void glGetActiveUniformName(GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei* length,
                            GLchar* uniformName) {
    LOG()
    LOG_D("glGetActiveUniformName(program: %u, index: %u, bufSize: %d)", program, uniformIndex, bufSize)

    if (length) *length = 0;
    if (bufSize <= 0 || uniformName == nullptr) {
        // Nothing to write. Still forwarded when bufSize is negative so the driver
        // raises the GL_INVALID_VALUE the caller is owed.
        if (bufSize < 0) GLES.glGetActiveUniform(program, uniformIndex, bufSize, nullptr, nullptr, nullptr, nullptr);
        CHECK_GL_ERROR
        return;
    }

    // Same buffer contract in both calls: at most bufSize-1 characters plus the
    // terminator, and a length that excludes it. The size and type this also
    // returns are what glGetActiveUniformsiv is for; they are discarded here.
    GLint size = 0;
    GLenum type = 0;
    GLsizei written = 0;
    uniformName[0] = '\0';
    GLES.glGetActiveUniform(program, uniformIndex, bufSize, &written, &size, &type, uniformName);
    if (length) *length = written;

    LOG_D("  -> \"%s\"", uniformName)
    CHECK_GL_ERROR
}
