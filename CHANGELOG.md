# Changelog

## [2.1.0] - 2026-11-21

### العربية

- أول دمج فعلي للغة Rust داخل محرك العارض: أُنشئ نواة `mobileglues-rs` (Crate بلغة Rust بلا أي اعتماديات خارجية، تُبنى كـ staticlib وتُربط داخل `libmobileglues.so` عبر CMake لأربع معماريات: arm64-v8a وarmeabi-v7a وx86 وx86_64).
- نقل ثماني عمليات معالجة نصوص في مسار ترجمة الشيدرات (GLSL ← GLSL ES) من `std::regex` البطيء إلى ماسحات Rust أحادية المرور: إزالة `layout(binding=N)` (مرحلتا replace)، مواقع `outColor0-7`، كشف `#version`، حقن عبارات `precision highp`، إعادة كتابة استدعاءات `texelFetch` لمحاكاة samplerBuffer (مرحلتا المسح)، إيجاد موضع إدراج آخر سطر uniform (كان يعمل بـsregex_iterator متعدد الأسطر)، ومسبارا تعريف `mg_textureQueryLod` و`GI_TemporalFilter`.
- تطابق تام في المخرجات: منفذو Rust يحافظون على سلوك الـregex حرفيًا بايت-ببايت، بما فيه السلوكيات الطريفة (مثل التقاط مسافة واحدة في الفهارس الفارغة نتيجة تراجع الـgreedy `\s*`، وفقدان السطر الأخير عند إعادة بناء المصدر في حقن precision، وعدم مطابقة كتل uniform blocks للنمط أصلًا) — تحقق آلي في CI عبر أداة `rsdiff` التي تقارن الناتجين بايت-ببايت على 12 ملف GLSL تمثيليًا وتفشل البناء عند أول اختلاف.
- قياسات أداء فعلية (على مضيف التطوير x86_64، مجموع عبر المُختبَرات، نانوثانية/نداء — تُنشر أرقام CI المستقلة مع كل إصدار في ملف `rsdiff-report`): `find_uniform_insert_pos` أسرع 185.5 مرة، مسبارا التعريف أسرع 35-37 مرة، `process_sampler_buffer` أسرع 21.3 مرة، `removeLayoutBinding` أسرع 8.5 مرة، `processOutColorLocations` أسرع 8.4 مرة، `getGLSLVersion` أسرع 2.4 مرة، `forceSupporterOutput` أسرع 1.7 مرة.
- أمان في العمق: كل نقطة FFI في Rust محمية بـcatch_unwind فلا يمكن لذعر Rust أن يُسقط ماينكرافت — أي فشل يعيد قيمة خطأ فيتحول التنفيذ تلقائيًا إلى التنفيذ الأصلي C++ لنفس الدالة. كما تعمل كل الدوال الثماني بالاحتياطي C++ عند بناء المشروع بلا Rust (رموز weak) أو عند ضبط متغير البيئة `MG_RS_DISABLE=1`.
- بنية المشروع: تحويل العارض من git submodule إلى مصدر متتبع داخل المستودع (نسخة `f56af31` من فرع plugin الرسمي) ليصبح المستودع مكتفًا ذاتيًا — بناء الإصدار وأرشيف السورس ZIP يعملان من هذا المستودع وحده.
- توقيع الإصدارات: مفتاح توقيع خاص بالـfork (`mg-rs-signing.jks` بكلمة مرور احتياطية مضمّنة بنمط مشروع FCL سابقًا) لأن كلمة مرور مفتاح upstream غير علنية؛ أسرار المستودع (إن ضُبطت) تتفوق دائمًا. التطبيق يعمل كإضافة FCL بنفس applicationId مع اسم مميز «MobileGlues-RS».
- لا تغيير في جودة الصورة أو مسافة العرض أو الدقة أو الميزات: التعديل كله في طبقة معالجة نصوص الترجمة، ولم يُحذف أي زر أو إعداد أو واجهة أو مسار وظيفي.

### English

- First real Rust integration inside the renderer: a dependency-free `mobileglues-rs` crate (staticlib, linked into `libmobileglues.so` by CMake for arm64-v8a / armeabi-v7a / x86 / x86_64).
- Eight text passes of the shader translation pipeline (GLSL -> GLSL ES) moved from slow `std::regex` to single-pass Rust scanners: `layout(binding=N)` removal (both replace passes), `outColor0-7` locations, `#version` detection, `precision highp` injection, `texelFetch` rewrites for samplerBuffer emulation (both scan passes), the last-uniform-line insertion scan (previously a multiline `sregex_iterator`), and the `mg_textureQueryLod` / `GI_TemporalFilter` definition probes.
- Byte-for-byte output equivalence: the Rust ports reproduce the regex semantics literally, quirks included (the single-space capture on whitespace-only indices caused by greedy `\s*` backtracking, the dropped trailing newline when the precision path rebuilds the source, and uniform blocks never matching the uniform-line pattern) — enforced in CI by the `rsdiff` harness comparing both outputs byte-for-byte over 12 representative GLSL corpus files and failing the build on the first mismatch.
- Measured performance (development host x86_64, sum over corpus, ns/call — CI publishes its own independent numbers with every release in `rsdiff-report`): `find_uniform_insert_pos` 185.5x faster, definition probes 35-37x, `process_sampler_buffer` 21.3x, `removeLayoutBinding` 8.5x, `processOutColorLocations` 8.4x, `getGLSLVersion` 2.4x, `forceSupporterOutput` 1.7x.
- Defense in depth: every Rust FFI entry point is wrapped in `catch_unwind`, so a Rust panic can never crash Minecraft — any failure returns an error value and the caller transparently falls back to the original C++ implementation for that call. All eight functions also run their C++ fallback when built without Rust (weak symbols) or when `MG_RS_DISABLE=1` is set.
- Project structure: the renderer moved from a git submodule to a vendored tracked tree (upstream `f56af31`, branch plugin) so the repository is self-contained — release builds and the source ZIP archive work from this repository alone.
- Release signing: fork-owned keystore (`mg-rs-signing.jks` with an in-repo fallback password, same pattern as the FoldCraftLauncher-Elite project) because the upstream keystore password is not public; repository secrets always take precedence when set. The app keeps the upstream applicationId and works as an FCL renderer plugin under the distinct label "MobileGlues-RS".
- No change to image quality, render distance, resolution or features: the change is entirely inside the translation text-processing layer; no button, setting, interface or functional path was removed.

### Validation

- `cargo test`: 7 unit tests covering every ported function, including the regex-quirk cases verified against a host build of the original std::regex code.
- `rsdiff` differential harness: 12/12 corpus files byte-identical on all 8 functions between the original C++ and the Rust core (equivalence gate runs in CI before every Gradle build).
- The full pipeline order is unchanged: preprocess -> glslang -> SPIRV-Cross -> post-process; only the text passes inside pre/post-processing switched implementations, each with per-call C++ fallback.
- The APK builds through GitHub Actions with the Rust core compiled for all four ABIs by the NDK clang through cargo; no physical-device runtime test has been performed yet (Phase 2 plan: on-device FPS/frame-time comparison with MG_RS_DISABLE A/B runs on a Helio G99).

## [2.0.0] - 2026-11-21 (upstream baseline)

- Upstream MobileGlues-plugin 2.0.0 as forked: the OpenGL 4.0 (1.17+) renderer plugin for FCL, with glslang + SPIRV-Cross shader translation, texture-buffer emulation, multidraw paths and the Compose settings app.
- Fork starting point recorded at MobileGL-Dev/MobileGlues@f56af31 (branch plugin) vendored into this repository.
