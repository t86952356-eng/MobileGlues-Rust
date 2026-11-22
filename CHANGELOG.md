# Changelog

## [2.2.0] - 2026-11-22

### العربية

- **ميزة «سجل الأداء» (perfLog) — اقتراح المستخدم**: خيار جديد في تطبيق الإضافة (مجموعة «التشخيص») يفعّل تسجيل ما يفعله العارض داخل اللعبة، ليعرف المطور التالي ما الذي يجب تحسينه بأرقام حقيقية من الجهاز لا بتخمين. بعد التفعيل والدخول للعبة يكتب العارض ملفين في مجلد MG:
  - `perf_report.txt`: لقطة كاملة تُحدَّث كل 10 ثوانٍ (كتابة ذرّية tmp+rename فلن تُقرأ أبدًا ناقصة) تشمل: هوية الجهاز الفعلية من التعريفات الخلفية، حالة نواة Rust، إعدادات الجلسة كاملة، توزيع زمن الإطار (المتوسط/p50/p90/p95/p99/الأقصى + متوسط FPS + 1% low)، وعدادات نداءات GL للإطار الواحد وللثانية الواحدة: نداءات الرسم المفردة ودفعات multidraw وعدد رسماتها الفرعية والعدد الفعلي للرسمات، نداءات وبايتات رفع المخازن (glBufferData/glBufferSubData) والقوام، تغييرات الحالة (glEnable/glDisable)، وتبديل البرامج (glUseProgram)، وقسم الشيدرات: عدد المصادر والمباشرة منها وقرارات الكاش (إصابة/إخفاق + النسبة) وزمن الترجمة الكلي والمتوسط.
  - `perf_history.log`: سطر CSV لكل نافذة 10 ثوانٍ عبر الجلسة كاملة — بما فيها مرحلة تحميل العالم التي تضيع عادة من اللقطات اللحظية.
- زر «مشاركة تقرير الأداء» في نفس المجموعة يفتح نافذة مشاركة أندرويد بالتقرير نصًّا خامًا (واتساب/تيليغرام/…) لإرساله للمطور بنقرة واحدة، مع رسالة إرشادية إن لم يوجد تقرير بعد.
- نقاط القياس مدمجة في المسارات الحقيقية: توقيت الإطارات من نقطة العرض الوحيدة `presentSurface` (تغطي مساري eglSwapBuffers وeglSwapBuffersWithDamage)؛ عدادات الرسم من `prepareForDraw` التي يمر بها كل مدخل رسم (نُقل glDrawArrays من التمرير الماكروي في gl_native.cpp إلى drawing.cpp ليمكن عدّه مع الحفاظ على الاسم البديل glDrawArraysARB)؛ عدادات multidraw من `mg_multidraw_enter`؛ توقيت ترجمة الشيدرات مع تمييز إصابة الكاش داخل `GLSLtoGLSLES`؛ بايتات القوام عبر `pixel_sizeof` الأصلية.
- صفر تكلفة عند الإيقاف: كل نقطة قياس تتحقق أولًا من علم `mg_perf_active` — فرع واحد متوقع التفرع، والعدادات ذرّية بترتيب relaxed فلا توقف تنفيذًا متوازيًا. الخيار مطفأ افتراضيًا ولا يتغير سلوك الرسم شيئًا مطلقًا (تغيّر واحد محسوب: نقل تعريف glDrawArrays دون أي فرق سلوكي).
- متغير البيئة `MG_PERF_LOG=1/0` يتجاوز config.json — لتشغيل مقارنات A/B دون فتح التطبيق (نفس فلسفة MG_RS_DISABLE).
- الاختبار `tests/perf_smoke_test.cpp` (يُبنى ويُشغّل مع `sh tests/run.sh`): يقود العدادات ومجرى العرض ويتحقظ من اكتمال الملفين ومفاتيحهما — نجح محليًا 15/15.
- ترجمات الواجهة الجديدة بالثلاث: الإنجليزية والصينية + ملف عربي كامل لهذه المفاتيح (أول ترجمة عربية في التطبيق؛ بقية المفاتيح تسقط إلى الإنجليزية كالمعتاد في أندرويد).

### English

- **Performance log (perfLog) feature — user's suggestion**: a new option in the plugin app's "Diagnostics" group records what the renderer does in-game, so the next optimization target is decided from real on-device numbers instead of guesswork. Once enabled, entering a game makes the renderer write two files into the MG directory:
  - `perf_report.txt`: a complete snapshot rewritten atomically every 10 seconds (tmp+rename, never readable half-written): actual device identity from the backend strings, Rust core status, full session settings, frame-time distribution (avg/p50/p90/p95/p99/max + average FPS + 1% low), and per-frame/per-second GL call counters — single draws, multidraw batches with their sub-draw count and the effective draw total, buffer upload calls and bytes (glBufferData/glBufferSubData), texture upload calls and bytes, state changes (glEnable/glDisable), program switches (glUseProgram) — plus the shader section: sources, direct passthroughs, cache hits/misses with the rate, and total/average translation time.
  - `perf_history.log`: one CSV line per 10-second window for the whole session — including the world-loading phase that momentary snapshots always lose.
- A "Share performance report" button in the same group opens the Android share sheet with the report as plain text (WhatsApp/Telegram/...) for one-tap delivery to the developer, with a guidance message when no report exists yet.
- The measurement points are wired into the real paths: frame timing from the single present choke point `presentSurface` (covers both eglSwapBuffers and eglSwapBuffersWithDamage); draw counters from `prepareForDraw`, which every draw entry crosses (glDrawArrays moved from the macro passthrough in gl_native.cpp into drawing.cpp so it can be counted, keeping the glDrawArraysARB alias); multidraw counters inside `mg_multidraw_enter`; shader translation timing with cache hit/miss discrimination inside `GLSLtoGLSLES`; texture bytes through the original `pixel_sizeof`.
- Zero cost when off: every hook checks the `mg_perf_active` flag first — one perfectly predicted branch; counters are relaxed-order atomics so nothing stalls concurrent execution. The option defaults to off and changes no rendering behavior (one deliberate, behavior-preserving change: relocating the glDrawArrays definition).
- The `MG_PERF_LOG=1/0` environment variable overrides config.json — for A/B runs without opening the app (same philosophy as MG_RS_DISABLE).
- New host test `tests/perf_smoke_test.cpp` (built and run by `sh tests/run.sh`): drives the counters and the present stream, then verifies both files and their keys — passed locally 15/15.
- UI strings in three languages: English, Chinese, plus a complete Arabic resources file for the new keys (the app's first Arabic strings; every other key falls back to English as Android partial translations do).

### Validation

- Host syntax pass (g++ -std=gnu++20 -fsyntax-only) over every touched translation unit: perf.cpp, settings.cpp, main.cpp, egl.cpp, drawing.cpp, gl_native.cpp, multidraw.cpp, shader.cpp, glsl_for_es.cpp, buffer.cpp, texture.cpp, enable.cpp, program.cpp, getter.cpp — clean.
- Existing host suites still green after the hooks (`sh tests/run.sh`): pixel-size and framebuffer-shuffle checks all pass.
- New perf smoke test passed 15/15 locally, producing a well-formed report (verified by reading the generated file: header, device info, rust status, frame stats, all counter sections, window block, history CSV).
- Full APK build and byte-level verification happen in CI as always (mg.yml builds all four ABIs with the Rust core; publish-release.yml signs and attaches APK + source ZIP + rsdiff report).

## [Unreleased-2026-11-21: repository detach] - 2026-11-21

### العربية

- فُصل المستودع عن شبكة الـfork الخاصة بـ`MobileGL-Dev/MobileGlues-plugin` ليصبح مستودعًا مستقلًا، وفق الخطوات اليدوية الرسمية الموثقة من GitHub (استنساخ bare ← حذف المستودع ← إعادة إنشائه بنفس الاسم ← دفع mirror). السبب: GitHub لا يعيد حساب نسب اللغات في مستودعات الـfork، فبقيت إحصائيات اللغة مجمّدة على حالة المستودع الأصل (Kotlin/C++/Python) ولا تظهر نسبة Rust إطلاقًا رغم وجود نواة `mobileglues-rs` في مسار التنفيذ منذ 2.1.0. سجل git والوسم `2.1.0` لم يتغيرا (نفس مُعرّفات SHA بالضبط)، وأصول الإصدار أُعيد بناؤها آليًا عبر سير النشر.
- أُضيف `.gitattributes` لتحديد ما يُحتسب في شريط اللغات بدقة: شجرة العارض المتتبعة `MobileGlues/` (مصدر upstream المنقول) تُعد `linguist-vendored` فتُستبعد من النِّسب، بينما نواة Rust الخاصة بالمشروع `MobileGlues/MobileGlues-cpp/rust/` تُحتسب دائمًا — فيظهر Rust بنسبة واضحة (~7%) بجانب Kotlin بدل أن يغرق كود upstream المنقول (8.7MB من C++) النِّسب كلها.

### English

- Detached the repository from the `MobileGL-Dev/MobileGlues-plugin` fork network into a standalone repository, following GitHub's documented manual steps (bare clone -> delete -> recreate with the same name -> mirror push). Reason: GitHub never recalculates language statistics for forks, so the language bar stayed frozen at the parent's state (Kotlin/C++/Python) with no Rust percentage at all, despite the `mobileglues-rs` core shipping in the execution path since 2.1.0. The git history and the `2.1.0` tag are unchanged (exact same SHAs), and the release assets were rebuilt automatically by the publish workflow.
- Added a `.gitattributes` to define exactly what the language bar counts: the vendored renderer tree `MobileGlues/` (upstream source vendored into this repo) is marked `linguist-vendored` and excluded from the statistics, while this project's own Rust core `MobileGlues/MobileGlues-cpp/rust/` is always counted — so Rust shows up as a clear percentage (~7%) next to Kotlin instead of being buried by the 8.7 MB of vendored upstream C++.

### Validation

- Verified post-detach language statistics (GitHub `/languages` API, 2026-11-21): Kotlin 79.76%, **Rust 6.47%**, C++ 5.60%, GLSL 5.18%, Python 2.65%, plus small remainders — the language bar at https://github.com/t86952356-eng/MobileGlues-Rust now shows Rust clearly.
- `.gitattributes` semantics confirmed before pushing via `git check-attr` (rust/ core un-vendored, MobileGlues/ tree vendored) and against linguist's own source (attributes resolved by the libgit2 engine; the value "false" explicitly cancels vendoring).
- Operational lesson: GitHub's per-push stats jobs can be dropped from the queue (three consecutive pushes here produced no update despite a correct file); the initial analysis of a freshly created repository always runs and is fast — a second recreate (tag first, then main) landed the correct statistics in under 40 seconds.
- Second operational lesson: pushing a large pack to a brand-new empty repository can fail with a thin-pack error ("did not receive expected object") — push from a bare clone of the remote (or use `--no-thin`) instead of the working clone.
- `git ls-remote` matches the pre-detach snapshot: `refs/tags/2.1.0` = `faa11b98` (lightweight tag, untouched), `refs/heads/main` at the current branch head.

## [2.1.0] - 2026-11-21

### العربية

- أول دمج فعلي للغة Rust داخل محرك العارض: أُنشئ نواة `mobileglues-rs` (Crate بلغة Rust بلا أي اعتماديات خارجية، تُبنى كـ staticlib وتُربط داخل `libmobileglues.so` عبر CMake لأربع معماريات: arm64-v8a وarmeabi-v7a وx86 وx86_64).
- نقل ثماني عمليات معالجة نصوص في مسار ترجمة الشيدرات (GLSL ← GLSL ES) من `std::regex` البطيء إلى ماسحات Rust أحادية المرور: إزالة `layout(binding=N)` (مرحلتا replace)، مواقع `outColor0-7`، كشف `#version`، حقن عبارات `precision highp`، إعادة كتابة استدعاءات `texelFetch` لمحاكاة samplerBuffer (مرحلتا المسح)، إيجاد موضع إدراج آخر سطر uniform (كان يعمل بـsregex_iterator متعدد الأسطر)، ومسبارا تعريف `mg_textureQueryLod` و`GI_TemporalFilter`.
- تطابق تام في المخرجات: منفذو Rust يحافظون على سلوك الـregex حرفيًا بايت-ببايت، بما فيه السلوكيات الطريفة (مثل التقاط مسافة واحدة في الفهارس الفارغة نتيجة تراجع الـgreedy `\s*`، وفقدان السطر الأخير عند إعادة بناء المصدر في حقن precision، وعدم مطابقة كتل uniform blocks للنمط أصلًا) — تحقق آلي في CI عبر أداة `rsdiff` التي تقارن الناتجين بايت-ببايت على 12 ملف GLSL تمثيليًا وتفشل البناء عند أول اختلاف.
- قياسات أداء فعلية على عدّاء GitHub Actions نفسه (مجموع نانوثانية/نداء عبر المُختبَرات الـ12 — التقرير الكامل مرفق بالإصدار: `rsdiff-report-2.1.0.txt`): `find_uniform_insert_pos` أسرع **46.0x**، `processOutColorLocations` أسرع **8.75x**، `removeLayoutBinding` أسرع **8.28x**، مسبارا التعريف أسرع **13.1-13.5x**، `process_sampler_buffer` أسرع **11.6x**، `getGLSLVersion` أسرع **1.9x**، `forceSupporterOutput` أسرع **1.2x**. (على مضيف التطوير x86_64 كانت النتائج أعلى: حتى 185x لمسح uniform.)
- أمان في العمق: كل نقطة FFI في Rust محمية بـcatch_unwind فلا يمكن لذعر Rust أن يُسقط ماينكرافت — أي فشل يعيد قيمة خطأ فيتحول التنفيذ تلقائيًا إلى التنفيذ الأصلي C++ لنفس الدالة. كما تعمل كل الدوال الثماني بالاحتياطي C++ عند بناء المشروع بلا Rust (رموز weak) أو عند ضبط متغير البيئة `MG_RS_DISABLE=1`.
- بنية المشروع: تحويل العارض من git submodule إلى مصدر متتبع داخل المستودع (نسخة `f56af31` من فرع plugin الرسمي) ليصبح المستودع مكتفًا ذاتيًا — بناء الإصدار وأرشيف السورس ZIP يعملان من هذا المستودع وحده.
- توقيع الإصدارات: مفتاح توقيع خاص بالـfork (`mg-rs-signing.jks` بكلمة مرور احتياطية مضمّنة بنمط مشروع FCL سابقًا) لأن كلمة مرور مفتاح upstream غير علنية؛ أسرار المستودع (إن ضُبطت) تتفوق دائمًا. التطبيق يعمل كإضافة FCL بنفس applicationId مع اسم مميز «MobileGlues-RS».
- لا تغيير في جودة الصورة أو مسافة العرض أو الدقة أو الميزات: التعديل كله في طبقة معالجة نصوص الترجمة، ولم يُحذف أي زر أو إعداد أو واجهة أو مسار وظيفي.

### English

- First real Rust integration inside the renderer: a dependency-free `mobileglues-rs` crate (staticlib, linked into `libmobileglues.so` by CMake for arm64-v8a / armeabi-v7a / x86 / x86_64).
- Eight text passes of the shader translation pipeline (GLSL -> GLSL ES) moved from slow `std::regex` to single-pass Rust scanners: `layout(binding=N)` removal (both replace passes), `outColor0-7` locations, `#version` detection, `precision highp` injection, `texelFetch` rewrites for samplerBuffer emulation (both scan passes), the last-uniform-line insertion scan (previously a multiline `sregex_iterator`), and the `mg_textureQueryLod` / `GI_TemporalFilter` definition probes.
- Byte-for-byte output equivalence: the Rust ports reproduce the regex semantics literally, quirks included (the single-space capture on whitespace-only indices caused by greedy `\s*` backtracking, the dropped trailing newline when the precision path rebuilds the source, and uniform blocks never matching the uniform-line pattern) — enforced in CI by the `rsdiff` harness comparing both outputs byte-for-byte over 12 representative GLSL corpus files and failing the build on the first mismatch.
- Measured on the GitHub Actions runner itself (sum of ns/call over the 12-file corpus — the full report ships with this release: `rsdiff-report-2.1.0.txt`): `find_uniform_insert_pos` **46.0x** faster, `processOutColorLocations` **8.75x**, `removeLayoutBinding` **8.28x**, definition probes **13.1-13.5x**, `process_sampler_buffer` **11.6x**, `getGLSLVersion` **1.9x**, `forceSupporterOutput` **1.2x**. (Development-host x86_64 numbers were higher still: up to 185x for the uniform scan.)
- Defense in depth: every Rust FFI entry point is wrapped in `catch_unwind`, so a Rust panic can never crash Minecraft — any failure returns an error value and the caller transparently falls back to the original C++ implementation for that call. All eight functions also run their C++ fallback when built without Rust (weak symbols) or when `MG_RS_DISABLE=1` is set.
- Project structure: the renderer moved from a git submodule to a vendored tracked tree (upstream `f56af31`, branch plugin) so the repository is self-contained — release builds and the source ZIP archive work from this repository alone.
- Release signing: fork-owned keystore (`mg-rs-signing.jks` with an in-repo fallback password, same pattern as the FoldCraftLauncher-Elite project) because the upstream keystore password is not public; repository secrets always take precedence when set. The app keeps the upstream applicationId and works as an FCL renderer plugin under the distinct label "MobileGlues-RS".
- No change to image quality, render distance, resolution or features: the change is entirely inside the translation text-processing layer; no button, setting, interface or functional path was removed.

### Validation

- `cargo test`: 7 unit tests covering every ported function, including the regex-quirk cases verified against a host build of the original std::regex code.
- `rsdiff` differential harness: 12/12 corpus files byte-identical on all 8 functions between the original C++ and the Rust core (equivalence gate runs in CI before every Gradle build).
- The full pipeline order is unchanged: preprocess -> glslang -> SPIRV-Cross -> post-process; only the text passes inside pre/post-processing switched implementations, each with per-call C++ fallback.
- The APK builds through GitHub Actions with the Rust core compiled for all four ABIs by the NDK clang through cargo, and the shipped libmobileglues.so carries the mg_rs_* symbols as DEFINED (verified with nm on the APK's arm64-v8a/libmobileglues.so after release).
- Fixed during this release: weak undefined references never extract members from a static archive, so the first build silently shipped with the Rust archive present but unlinked (dispatchers would have run the C++ fallback). The bridge now uses STRONG references when CMake defines MG_RS_INTEGRATED (archive extraction works) and weak ones only in Rust-less builds. Verified locally: all 11 mg_rs_* symbols defined (T) in the linked library.
- No physical-device runtime test has been performed yet (Phase 2 plan: on-device FPS/frame-time comparison with MG_RS_DISABLE A/B runs on a Helio G99).

## [2.0.0] - 2026-11-21 (upstream baseline)

- Upstream MobileGlues-plugin 2.0.0 as forked: the OpenGL 4.0 (1.17+) renderer plugin for FCL, with glslang + SPIRV-Cross shader translation, texture-buffer emulation, multidraw paths and the Compose settings app.
- Fork starting point recorded at MobileGL-Dev/MobileGlues@f56af31 (branch plugin) vendored into this repository.
