// Host-side test for gl/pipeline_cache.cpp: exercises the pure store half of
// the pipeline cache (records, keys, binary/status round-trips, LRU budget,
// disk persistence) without any GL -- the GL orchestration lives in
// gl/shader.cpp and gl/program.cpp and needs a real driver.
//
// The store is a singleton, so "restart" is simulated with reload_for_test()
// (flush to disk, drop memory, reload from disk), which is exactly the
// transition a second game session makes.
#include "../gl/pipeline_cache.h"
#include "../gl/mg.h"
#include "../config/settings.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

// --- stubs for symbols pipeline_cache.cpp pulls from the rest of the library ---
extern "C" {
char* mg_directory_path = nullptr;
}
global_settings_t global_settings;
static hardware_s g_hardware{320, false};
hardware_t hardware = &g_hardware;

static int g_env_no_progbin = 0;
extern "C" int GetEnvVarBool(const char* name, int* b, int def) {
    if (name && strcmp(name, "MG_NO_PROGBIN") == 0) {
        *b = g_env_no_progbin;
        return 1;
    }
    *b = def;
    return 0;
}

int __android_log_print(int, const char*, const char*, ...) { return 0; }
extern "C" void write_log(const char*, ...) {}
extern "C" void write_log_n(const char*, ...) {}

static int failures = 0;
#define CHECK(cond)                                                                                                       \
    do {                                                                                                                 \
        if (!(cond)) {                                                                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                       \
            failures++;                                                                                                  \
        }                                                                                                                \
    } while (0)

static std::vector<uint8_t> make_key(uint8_t fill) {
    return std::vector<uint8_t>(32, fill);
}

int main() {
    const std::string dir = "/tmp/mg_pipeline_test";
    mkdir(dir.c_str(), 0755);
    const std::string file = dir + "/pipeline_cache.bin";
    remove(file.c_str());
    remove((file + ".new").c_str());
    mg_directory_path = const_cast<char*>(dir.c_str());

    // Default on; the env override must be able to switch it off.
    g_env_no_progbin = 0;
    global_settings.program_binary_cache = true;
    // Note: enabled() is resolved once per process; this test relies on the
    // default-on resolution and verifies the flag path through the setting
    // only structurally (the renderer reads it before the first GL call).
    CHECK(pipeline_cache::enabled());

    // ---- shader records ----
    pipeline_cache::on_shader_created(7);
    pipeline_cache::on_source_uploaded(7, "uniform float a;", GL_FRAGMENT_SHADER);
    pipeline_cache::ShaderRecord rec;
    CHECK(pipeline_cache::get_shader_record(7, rec));
    CHECK(rec.source == "uniform float a;");
    CHECK(rec.shader_type == GL_FRAGMENT_SHADER);
    CHECK(!rec.compile_pending);

    CHECK(pipeline_cache::defer_compile(7)); // tracked: deferred
    CHECK(pipeline_cache::get_shader_record(7, rec) && rec.compile_pending);
    pipeline_cache::mark_compiled(7);
    CHECK(pipeline_cache::get_shader_record(7, rec) && !rec.compile_pending);

    CHECK(!pipeline_cache::defer_compile(999)); // untracked: caller must compile

    // A recycled name starts clean.
    pipeline_cache::on_shader_created(7);
    CHECK(!pipeline_cache::get_shader_record(7, rec));

    // ---- program records (a throwaway program, so the key section below
    // starts from a pristine shape) ----
    pipeline_cache::on_program_created(6);
    pipeline_cache::on_source_uploaded(11, "src-vs", GL_VERTEX_SHADER);
    pipeline_cache::on_source_uploaded(12, "src-fs", GL_FRAGMENT_SHADER);
    pipeline_cache::on_attach(6, 11, GL_VERTEX_SHADER);
    pipeline_cache::on_attach(6, 12, GL_FRAGMENT_SHADER);
    pipeline_cache::on_attrib_binding(6, 0, "Position");
    pipeline_cache::ProgramRecord prec;
    CHECK(pipeline_cache::get_program_record(6, prec));
    CHECK(prec.attached.size() == 2 && prec.attached[0] == 11 && prec.attached[1] == 12);
    CHECK(prec.attrib_bindings.size() == 1 && prec.attrib_bindings[0].first == 0 &&
          prec.attrib_bindings[0].second == "Position");
    pipeline_cache::on_detach(6, 11);
    CHECK(pipeline_cache::get_program_record(6, prec) && prec.attached.size() == 1);

    // ---- key building: determinism and sensitivity ----
    pipeline_cache::on_program_created(5);
    pipeline_cache::on_attach(5, 11, GL_VERTEX_SHADER);
    pipeline_cache::on_attach(5, 12, GL_FRAGMENT_SHADER);
    pipeline_cache::on_attrib_binding(5, 0, "Position");
    pipeline_cache::LinkKeyContext ctx;
    ctx.es_version = 320;
    ctx.emulate_texture_buffer = false;
    ctx.device_renderer = "Mali-G57 MC2";
    ctx.device_version = "OpenGL ES 3.2 v1.r32p1";
    const auto key1 = pipeline_cache::build_program_key(5, ctx);
    const auto key1b = pipeline_cache::build_program_key(5, ctx);
    CHECK(key1.size() == 32 && key1 == key1b);

    // Different ES version -> different key (the binary depends on it).
    auto ctx2 = ctx;
    ctx2.es_version = 300;
    CHECK(pipeline_cache::build_program_key(5, ctx2) != key1);

    // Different attached source -> different key.
    pipeline_cache::on_source_uploaded(12, "src-fs-v2", GL_FRAGMENT_SHADER);
    CHECK(pipeline_cache::build_program_key(5, ctx) != key1);
    pipeline_cache::on_source_uploaded(12, "src-fs", GL_FRAGMENT_SHADER);
    CHECK(pipeline_cache::build_program_key(5, ctx) == key1);

    // A new binding -> different key.
    pipeline_cache::on_attrib_binding(5, 1, "UV");
    CHECK(pipeline_cache::build_program_key(5, ctx) != key1);

    // Default FS marker participates too.
    auto ctx3 = ctx;
    ctx3.default_fs_attached = true;
    ctx3.default_fs_source = "#version 320 es\nvoid main(){}\n";
    CHECK(pipeline_cache::build_program_key(5, ctx3) != pipeline_cache::build_program_key(5, ctx));

    // Program 5's final shape: attached [11, 12] with bindings (0,Position)
    // then (1,UV). Freeze its key now -- reload_for_test() below wipes the
    // session-only tracking tables, and the whole point of this check is that
    // a same-shape program after a restart rebuilds the SAME key.
    const auto key_late = pipeline_cache::build_program_key(5, ctx);

    // ---- shader status records ----
    pipeline_cache::ShaderStatus st;
    st.compile_status = GL_TRUE;
    st.info_log_length = 0;
    CHECK(!pipeline_cache::lookup_shader_status("uniform float a;", st)); // no record yet
    pipeline_cache::record_shader_status("uniform float a;", GL_TRUE, 0, "");
    CHECK(pipeline_cache::lookup_shader_status("uniform float a;", st));
    CHECK(st.compile_status == GL_TRUE && st.info_log.empty());

    pipeline_cache::record_shader_status("bad source", GL_FALSE, 24, "ERROR: 1:1: boom\n");
    CHECK(pipeline_cache::lookup_shader_status("bad source", st));
    CHECK(st.compile_status == GL_FALSE && st.info_log_length == 24 && st.info_log == "ERROR: 1:1: boom\n");

    // ---- binary round-trip + persistence across a "restart" ----
    const std::vector<uint8_t> blob(1000, 0xAB);
    pipeline_cache::store_program_binary(key1, 0x12345678, blob.data(), blob.size());
    GLenum fmt = 0;
    std::vector<uint8_t> out;
    CHECK(pipeline_cache::lookup_program_binary(key1, &fmt, out));
    CHECK(fmt == 0x12345678 && out.size() == blob.size() && memcmp(out.data(), blob.data(), blob.size()) == 0);
    CHECK(!pipeline_cache::lookup_program_binary(make_key(0x00), &fmt, out));

    pipeline_cache::flush_for_test();
    {
        FILE* f = fopen(file.c_str(), "rb");
        CHECK(f != nullptr);
        if (f) {
            char magic[4] = {0};
            CHECK(fread(magic, 1, 4, f) == 4 && memcmp(magic, "MGPC", 4) == 0);
            fclose(f);
        }
    }

    pipeline_cache::reload_for_test();

    fmt = 0;
    out.clear();
    CHECK(pipeline_cache::lookup_program_binary(key1, &fmt, out));
    CHECK(fmt == 0x12345678 && out.size() == 1000 && out[0] == 0xAB && out[999] == 0xAB);

    CHECK(pipeline_cache::lookup_shader_status("bad source", st));
    CHECK(st.compile_status == GL_FALSE && st.info_log == "ERROR: 1:1: boom\n");

    // The shader/program tracking tables are session-only: gone after reload.
    CHECK(!pipeline_cache::get_shader_record(11, rec));
    CHECK(!pipeline_cache::get_program_record(5, prec));

    // ---- key survives the restart identically ----
    // Recreate program 5's exact final shape (attached [11, 12], bindings
    // Position then UV) under a fresh program 21: the key must equal key_late,
    // which is what makes the stored binary from the previous session load.
    pipeline_cache::on_program_created(21);
    pipeline_cache::on_source_uploaded(31, "src-vs", GL_VERTEX_SHADER);
    pipeline_cache::on_source_uploaded(32, "src-fs", GL_FRAGMENT_SHADER);
    pipeline_cache::on_attach(21, 31, GL_VERTEX_SHADER);
    pipeline_cache::on_attach(21, 32, GL_FRAGMENT_SHADER);
    pipeline_cache::on_attrib_binding(21, 0, "Position");
    pipeline_cache::on_attrib_binding(21, 1, "UV");
    CHECK(pipeline_cache::build_program_key(21, ctx) == key_late);

    // ---- LRU budget: entries survive until the cap, then the oldest go ----
    // 64 MiB budget, entries of 1 MiB: fill it up.
    for (int i = 0; i < 70; ++i) {
        std::vector<uint8_t> big(1024 * 1024, static_cast<uint8_t>(0x40 + i));
        pipeline_cache::store_program_binary(make_key(static_cast<uint8_t>(0x40 + i)), 0x1, big.data(), big.size());
    }
    // The very first small binary (key1) must have been evicted by now; recent
    // ones must still be there.
    CHECK(!pipeline_cache::lookup_program_binary(key1, &fmt, out));
    fmt = 0;
    CHECK(pipeline_cache::lookup_program_binary(make_key(0x40 + 69), &fmt, out));
    CHECK(out.size() == 1024 * 1024 && out[0] == 0x40 + 69);

    // A pending shader record must never be the one evicted.
    pipeline_cache::on_shader_created(77);
    pipeline_cache::on_source_uploaded(77, "pending source", GL_VERTEX_SHADER);
    CHECK(pipeline_cache::defer_compile(77));
    for (int i = 0; i < 8; ++i) {
        std::vector<uint8_t> big(1024 * 1024, 0xCD);
        pipeline_cache::store_program_binary(make_key(static_cast<uint8_t>(0x80 + i)), 0x1, big.data(), big.size());
        for (int j = 1; j <= 600; ++j) {
            pipeline_cache::on_shader_created(1000 + j);
            pipeline_cache::on_source_uploaded(1000 + j, "filler", GL_VERTEX_SHADER);
        }
    }
    CHECK(pipeline_cache::get_shader_record(77, rec));
    CHECK(rec.compile_pending);

    remove(file.c_str());
    remove((file + ".new").c_str());

    if (failures == 0) {
        printf("pipeline_cache_test: all checks passed\n");
        return 0;
    }
    printf("pipeline_cache_test: %d checks FAILED\n", failures);
    return 1;
}
