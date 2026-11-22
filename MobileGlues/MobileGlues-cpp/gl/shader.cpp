// MobileGlues - gl/shader.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>
#include "shader.h"

#include <GL/gl.h>
#include "log.h"
#include "program.h"
#include "../gles/loader.h"
#include "../includes.h"
#include "glsl/glsl_for_es.h"
#include "../config/settings.h"
#include "FSR1/FSR1.h"
#include "perf.h"
#include "pipeline_cache.h"

#define DEBUG 0

struct shader_t shaderInfo;

UnorderedMap<GLuint, bool> shader_map_is_sampler_buffer_emulated;

bool can_run_essl3(unsigned int esversion, const char* glsl) {
    if (strncmp(glsl, "#version 100", 12) == 0) {
        return true;
    }

    unsigned int glsl_version = 0;
    if (strncmp(glsl, "#version 300 es", 15) == 0) {
        glsl_version = 300;
    } else if (strncmp(glsl, "#version 310 es", 15) == 0) {
        glsl_version = 310;
    } else if (strncmp(glsl, "#version 320 es", 15) == 0) {
        glsl_version = 320;
    } else {
        return false;
    }
    return esversion >= glsl_version;
}

bool is_direct_shader(const char* glsl) {
    bool es3_ability = can_run_essl3(hardware->es_version, glsl);
    return es3_ability;
}

bool check_if_sampler_buffer_used(std::string str) {
    return str.find("samplerBuffer") != std::string::npos;
}

// Answers a deferred shader's COMPILE_STATUS / GL_INFO_LOG_LENGTH from the
// recorded driver answers. Returns false when the driver must be asked (the
// object was really compiled, or the source has no record yet -- in which
// case the caller compiles first and then asks the driver).
static bool answer_shader_query_from_cache_impl(GLuint shader, GLenum pname, GLint* out) {
    pipeline_cache::ShaderRecord rec;
    if (!pipeline_cache::get_shader_record(shader, rec) || rec.source.empty()) return false;
    if (rec.resolved_by_binary) {
        // The owning link came from a cached binary: the shader IS valid (its
        // compiled image is inside that binary), so the status is TRUE.
        *out = (pname == GL_COMPILE_STATUS) ? GL_TRUE : 0;
        return true;
    }
    if (!rec.compile_pending) return false; // really compiled: ask the driver
    pipeline_cache::ShaderStatus st;
    if (pipeline_cache::lookup_shader_status(rec.source, st)) {
        *out = (pname == GL_COMPILE_STATUS) ? st.compile_status : st.info_log_length;
        return true;
    }
    mg_force_compile_shader(shader);
    return false;
}

bool answer_shader_query_from_cache(GLuint shader, GLenum pname, GLint* out) {
    return answer_shader_query_from_cache_impl(shader, pname, out);
}

static void write_cached_info_log(const std::string& text, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    // GL contract: at most bufSize-1 characters plus a NUL; *length excludes
    // the NUL. A non-positive bufSize writes nothing.
    if (bufSize <= 0 || !infoLog) {
        if (length) *length = 0;
        return;
    }
    const size_t n = std::min(static_cast<size_t>(bufSize - 1), text.size());
    memcpy(infoLog, text.data(), n);
    infoLog[n] = '\0';
    if (length) *length = static_cast<GLsizei>(n);
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {
    LOG()
    shaderInfo.id = 0;
    shaderInfo.converted = "";
    shaderInfo.frag_data_changed_converted.clear();
    shaderInfo.frag_data_changed = 0;
    size_t l = 0;
    for (int i = 0; i < count; i++)
        l += (length && length[i] >= 0) ? length[i] : strlen(string[i]);
    std::string glsl_src, essl_src;
    glsl_src.reserve(l + 1);
    if (length) {
        for (int i = 0; i < count; i++) {
            if (length[i] >= 0)
                glsl_src += std::string_view(string[i], length[i]);
            else
                glsl_src += string[i];
        }
    } else {
        for (int i = 0; i < count; i++) {
            glsl_src += string[i];
        }
    }

    bool is_sampler_buffer_emulated = hardware->emulate_texture_buffer && check_if_sampler_buffer_used(glsl_src);

    GLint shaderType = 0;
    if (pipeline_cache::enabled()) {
        // Queried once here (both paths) so the pipeline cache can key the
        // program binary on the shader type without a second query at attach.
        GLES.glGetShaderiv(shader, GL_SHADER_TYPE, &shaderType);
    }

    if (is_direct_shader(glsl_src.c_str())) {
        LOG_D("[INFO] [Shader] Direct shader source: ")
        LOG_D("%s", glsl_src.c_str())
        MG_PERF_SHADER_SOURCES(1, 1);
        essl_src = glsl_src;
    } else {
        int glsl_version = getGLSLVersion(glsl_src.c_str());
        LOG_D("[INFO] [Shader] Shader source: ")
        LOG_D("%s", glsl_src.c_str())
        GLint type_direct = shaderType;
        if (!type_direct) {
            GLES.glGetShaderiv(shader, GL_SHADER_TYPE, &type_direct);
        }
        int return_code = 0;
        MG_PERF_SHADER_SOURCES(1, 0);
        essl_src = GLSLtoGLSLES(glsl_src.c_str(), type_direct, hardware->es_version, glsl_version, return_code);

        if (essl_src.empty()) {
            LOG_E("Failed to convert shader %d.", shader)
            return;
        }
        LOG_D("\n[INFO] [Shader] Converted Shader source: \n%s", essl_src.c_str())
    }
    if (!essl_src.empty()) {
        shaderInfo.id = shader;
        shaderInfo.converted = essl_src;
        const char* s[] = {essl_src.c_str()};
        GLES.glShaderSource(shader, count, s, nullptr);
        // The final translated text (what the driver just received) is the
        // pipeline cache's key material for both the binary and the recorded
        // compile answers.
        pipeline_cache::on_source_uploaded(shader, essl_src, static_cast<GLenum>(shaderType));
        if (hardware->emulate_texture_buffer)
            shader_map_is_sampler_buffer_emulated[shader] = is_sampler_buffer_emulated;
    } else
        LOG_E("Failed to convert glsl.")
    CHECK_GL_ERROR
}

// Runs the deferred driver compile for real, times it, and records the
// driver's own answers (status, INFO_LOG_LENGTH, log text) against the
// tracked source so a later query -- this session or the next one -- can be
// answered without compiling again. No-op for untracked shaders.
void mg_force_compile_shader(GLuint shader) {
    const unsigned long long t0 = mg_perf_now_ns();
    GLES.glCompileShader(shader);
    MG_PERF_SHADER_COMPILE_EXECUTED(mg_perf_now_ns() - t0);

    pipeline_cache::ShaderRecord rec;
    if (pipeline_cache::get_shader_record(shader, rec) && !rec.source.empty()) {
        GLint status = 0, log_length = 0;
        GLES.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        GLES.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        std::string log_text;
        if (log_length > 1) {
            // INFO_LOG_LENGTH includes the NUL on every driver seen so far.
            std::vector<char> buf(static_cast<size_t>(log_length) + 1, '\0');
            GLsizei written = 0;
            GLES.glGetShaderInfoLog(shader, log_length, &written, buf.data());
            log_text.assign(buf.data(), written > 0 ? static_cast<size_t>(written) : 0);
        }
        pipeline_cache::record_shader_status(rec.source, status, log_length, log_text);
        pipeline_cache::mark_compiled(shader);
    }
    CHECK_GL_ERROR_NO_INIT
}

// glCompileShader (intercepted since 2.3.0 for the pipeline cache).
//
// With the cache on, a tracked shader's compile is deferred: the driver call
// is skipped until something actually needs it -- a status/info-log query, or
// the link when no cached binary covers the program. Most of those compiles
// never happen at all: the answers come from the persisted status records and
// the program from a cached binary. With the cache off (or MG_NO_PROGBIN=1)
// this is exactly the upstream passthrough.
void glCompileShader(GLuint shader) {
    LOG()
    LOG_D("glCompileShader(%d)", shader)

    if (pipeline_cache::enabled()) {
        if (pipeline_cache::defer_compile(shader)) {
            MG_PERF_SHADER_COMPILE_DEFERRED();
            return;
        }
        // Untracked shader (no source record): compile exactly like upstream.
        const unsigned long long t0 = mg_perf_now_ns();
        GLES.glCompileShader(shader);
        MG_PERF_SHADER_COMPILE_EXECUTED(mg_perf_now_ns() - t0);
        CHECK_GL_ERROR
        return;
    }

    GLES.glCompileShader(shader);
    CHECK_GL_ERROR
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    LOG()
    // Deferred-compile path: COMPILE_STATUS and INFO_LOG_LENGTH are answered
    // from the recorded driver answers when the driver never compiled this
    // object. Everything else (SHADER_TYPE, DELETE_STATUS, SOURCE_LENGTH)
    // never depends on the compile and goes straight through.
    if (pipeline_cache::enabled() && (pname == GL_COMPILE_STATUS || pname == GL_INFO_LOG_LENGTH)) {
        GLint cached = 0;
        if (answer_shader_query_from_cache(shader, pname, &cached)) {
            *params = cached;
            MG_PERF_SHADER_STATUS_HIT();
            LOG_D("glGetShaderiv(%d, 0x%x) answered from the pipeline cache: %d", shader, pname, cached)
            CHECK_GL_ERROR
            return;
        }
    }
    GLES.glGetShaderiv(shader, pname, params);
    if (global_settings.ignore_error >= IgnoreErrorLevel::Partial && pname == GL_COMPILE_STATUS && !*params) {
        GLchar infoLog[512];
        GLES.glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOG_W_FORCE("Shader %d compilation failed: \n%s", shader, infoLog)
        LOG_W_FORCE("Now try to cheat.")
        *params = GL_TRUE;
    }
    CHECK_GL_ERROR
}

// glGetShaderInfoLog (intercepted since 2.3.0): replayed from the pipeline
// cache for a deferred shader, so a shader pack's status polling cannot force
// the compile the deferral just skipped.
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    LOG()
    if (pipeline_cache::enabled()) {
        pipeline_cache::ShaderRecord rec;
        if (pipeline_cache::get_shader_record(shader, rec) && !rec.source.empty()) {
            if (rec.resolved_by_binary) {
                // The program came from a cached binary; this object was never
                // compiled and has no log. An empty string is the only honest
                // answer that also keeps the game on its success path.
                if (length) *length = 0;
                if (bufSize > 0 && infoLog) infoLog[0] = '\0';
                MG_PERF_SHADER_STATUS_HIT();
                CHECK_GL_ERROR
                return;
            }
            if (rec.compile_pending) {
                pipeline_cache::ShaderStatus st;
                if (pipeline_cache::lookup_shader_status(rec.source, st)) {
                    write_cached_info_log(st.info_log, bufSize, length, infoLog);
                    MG_PERF_SHADER_STATUS_HIT();
                    CHECK_GL_ERROR
                    return;
                }
                // No recorded answer for this source: compile now so the
                // driver can answer truthfully (and record it for next time).
                mg_force_compile_shader(shader);
            }
        }
    }
    GLES.glGetShaderInfoLog(shader, bufSize, length, infoLog);
    CHECK_GL_ERROR
}

// The aliases gl_native.cpp's macro used to emit for the non-Apple platforms;
// glXGetProcAddress can hand these names out, so they stay.
extern "C" GLAPI GLAPIENTRY void glCompileShaderARB(GLuint shader) __attribute__((alias("glCompileShader")));
extern "C" GLAPI GLAPIENTRY void glGetShaderInfoLogARB(GLuint shader, GLsizei bufSize, GLsizei* length,
                                                       GLchar* infoLog) __attribute__((alias("glGetShaderInfoLog")));

GLuint glCreateShader(GLenum shaderType) {
    if (global_settings.fsr1_setting != FSR1_Quality_Preset::Disabled && !fsrInitialized) {
        InitFSRResources();
    }

    LOG()
    LOG_D("glCreateShader(%s)", glEnumToString(shaderType))
    GLuint shader = GLES.glCreateShader(shaderType);
    if (shader != 0 && hardware->emulate_texture_buffer) shader_map_is_sampler_buffer_emulated[shader] = false;
    // A recycled shader name must not inherit the previous object's source
    // record: a compile before any glShaderSource has to stay a compile of
    // nothing, exactly like upstream.
    if (shader != 0) pipeline_cache::on_shader_created(shader);
    CHECK_GL_ERROR
    return shader;
}