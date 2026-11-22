# AI_PROJECT_CONTEXT — ذاكرة المشروع الدائمة

> **هذا الملف هو المصدر الأساسي لحالة المشروع.** عند بدء أي محادثة جديدة: اقرأ هذا الملف أولًا، ثم `GITHUB_BUILD_GUIDE.md` عند الحاجة للبناء أو إصلاح مشاكله، ثم افحص آخر الكوميتات وتأكد أن حالة الملفات تطابق التوثيق، وتابع من آخر مرحلة مكتملة. لا تكرر عملًا منجزًا، ولا تفترض نجاح مهمة لمجرد أنها مذكورة هنا — تحقق من حالة التنفيذ.

## 1. المشروع والهدف

- **الاسم**: MobileGlues-RS (نسخة fork مطورة من MobileGlues الرسمي بواسطة MobileGL-Dev).
- **الوصف**: عارض رسومي (renderer) لماينكرافت Java Edition على أندرويد — مكتبة `libmobileglues.so` تترجم OpenGL 4.0 (النسخ 1.17+) إلى OpenGL ES 3 — تُستخدم كإضافة (plugin) داخل مشغل FCL/FoldCraftLauncher.
- **الهدف الرئيسي**: معمارية هجينة **C++ + Rust** مع تحسينات أداء هندسية حقيقية وقابلة للقياس في العارض، **دون** خفض الجودة أو مسافة العرض أو الدقة أو تعطيل المؤثرات أو حذف أي ميزة. Rust يجب أن يكون جزءًا فعليًا من مسار التنفيذ لا مجرد مكتبة شكلية.
- **جهاز المستخدم المرجعي**: معالج Helio G99، رام 8GB، المشغل Fold Launcher (FCL).
- **مستودع المشروع**: https://github.com/t86952356-eng/MobileGlues-Rust (الدفع مباشرة إلى `main`). **مستودع مستقل (standalone) منذ 2026-11-21**: فُصل عن شبكة الـfork (كان fork من `MobileGL-Dev/MobileGlues-plugin`) بالخطوات اليدوية الرسمية (bare clone ← حذف ← إعادة إنشاء بنفس الاسم ← دفع mirror) لأن GitHub لا يعيد حساب نسب اللغات في الـforks؛ سجل git والوسوم لم تتغير (نفس الـSHA).
- **المصادر الأصلية (upstream)**:
  - المستودع الأصلي للإضافة: https://github.com/MobileGL-Dev/MobileGlues-plugin
  - العارض (كان submodule): https://github.com/MobileGL-Dev/MobileGlues — فرع `plugin`، نقطة الالتقاط `f56af31` (مُدمج مصدره داخل هذا المستودع في `MobileGlues/`).
- **الترخيص**: LGPL-2.1 (كل التعديلات تحترمه).

## 2. قواعد إلزامية (مختصرة)

1. لا خفض جودة/دقة/مسافة عرض ولا تعطيل مؤثرات ولا حذف ميزات لرفع FPS.
2. لا ادعاء تحسن بلا قياس فعلي؛ سجّل الأرقام وظروفها.
3. Rust فعّالة في مسار التنفيذ، مع إمكانية احتياطي C++ دائمًا.
4. التعديل المباشر على GitHub مع كوميتات منظمة، وبناء APK بعد كل مهمة مستقرة، ثم إصدار Release يحتوي: APK + ZIP السورس الكامل + Changelog (عربي/إنجليزي).
5. لا توكنات أو أسرار داخل السورس أو التوثيق.
6. تحديث هذا الملف و`GITHUB_BUILD_GUIDE.md` و`CHANGELOG.md` بعد كل مهمة.
7. عدم إعادة استخدام tag قديم، وعدم لمس الوسوم القديمة بعد النشر.

## 3. المعمارية الحالية

### 3.1 طبقات المستودع (353+ ملفًا متتبعًا)

| المسار | الوظيفة |
|---|---|
| `app/` | تطبيق الإضافة (Kotlin/Compose، applicationId = `com.fcl.plugin.mobileglues`): يحمّل `libmobileglues.so`، شاشة إعدادات (MGConfig)، تصدير/استيراد كاش GLSL، أدوات bench (MGBench/MultidrawBench)، خدمة استعلام MgQueryService لإظهار معلومات GPU داخل FCL. native صغير خاص به (`app/src/main/cpp/mobileglues_info_getter.cpp`). |
| `MobileGlues/` | **العارض نفسه** (كان submodule، الآن مصدر متتبع مباشرة): مكتبة أندرويد (AAR) تبني `libmobileglues.so` عبر CMake. |
| `MobileGlues/MobileGlues-cpp/` | الكود الأصلي C++20 (~166 ألف سطر بدون third-party): `gl/` (تنفيذ OpenGL: shader/buffer/texture/framebuffer/multidraw/drawing/enable/state + وحدة سجل الأداء `gl/perf.h/.cpp` منذ 2.2.0 + **وحدة كاش البرامج `gl/pipeline_cache.h/.cpp` منذ 2.3.0**)، `gl/glsl/` (مسار ترجمة GLSL)، `egl/`، `gles/`، `config/`، `3rdparty/` (glslang + SPIRV-Cross + xxHash + FSR1). |
| `MobileGlues/MobileGlues-cpp/rust/` | **نواة Rust الجديدة** (mobileglues-rs v0.1.0): staticlib بلا اعتماديات، ماسحات نصوص أحادية المرور. |
| `tools/rsdiff/` | أداة التكافؤ والقياس (C++ host): تقارن Rust بـstd::regex الأصلي بايت-ببايت على `corpus/` (14 ملف GLSL تمثيليًا — أُضيف ملفا #line وإعلانات uniform المُهيّأة في 2.3.0) وتقيس الأداء. |
| `.github/workflows/mg.yml` | سير البناء: اختبارات Rust + بوابة التكافؤ rsdiff ← بناء APK (Gradle + NDK r27 + Rust لأربع ABIs). |
| `.github/workflows/publish-release.yml` | النشر: يحل الإصدار من `version_map.json[0]`، يتخطى إن وُجدت أصول، يستدعي mg.yml، ينشئ Release بـ APK + سورس ZIP + تقرير rsdiff. |
| `version_map.json` | فهرس الإصدارات؛ العنصر [0] هو الأحدث (يحدد وسم النشر). |
| `.gitattributes` | قواعد GitHub Linguist لشريط اللغات: شجرة `MobileGlues/` (upstream المتتبع) = `linguist-vendored` مُستبعدة من النّسب، ونواة `rust/` تُحتسب دائمًا (الأسطر الأحدث تتقدم على الأقدم في الملف). |
| `mg-rs-signing.jks` | مفتاح توقيع الـfork (كلمة المرور الاحتياطية `mg-rs-release-2025` مضمّنة في app/build.gradle.kts بنمط FCL). `keystore.jks` الأصلي محفوظ لكن كلمة مروره غير علنية. |

### 3.2 مسار ترجمة الشيدرات (المسار الذي عُدّل في المرحلة الأولى)

`glShaderSource` (thread واحد، `shaderInfo` عام) ← تخزين مؤقت (`gl/glsl/cache.cpp`: SHA-256 + LRU + حفظ مؤجل) ← عند الفشل: `GLSLtoGLSLES_2`:
1. `preprocess_glsl`: إزالة أسطر `#line`، حقن GI_TemporalFilter (مسح أسطر uniform)، حقن textureQueryLod، حقن ماكرو MG، محاكاة samplerBuffer (إعادة كتابة texelFetch).
2. `glsl_to_spirv` (glslang) ← `spirv_to_essl` (SPIRV-Cross).
3. معالجة لاحقة: `removeLayoutBinding` ← `processOutColorLocations` ← `forceSupporterOutput` ← `process_uniform_declarations` ← `Cache::put`.

**الدوال العشر المنقولة إلى Rust** (تُستدعى أولًا، مع احتياطي C++ بنفس الدالة): `removeLayoutBinding`، `processOutColorLocations`، `getGLSLVersion`، `forceSupporterOutput`، مرحلتا texelFetch في `process_sampler_buffer`، مسح موضع إدراج uniform في `inject_temporal_filter`، مسبارا التعريف في `inject_textureQueryLod`/`inject_temporal_filter`، **`replace_line_starting_with` (بما فيه سلوكها التاريخي: المطابقة على "#line" حرفيًا بمعزل عن معامل `starting`)، و`process_uniform_declarations` (بكل طرائفها: رفع precision، ومطابقة substring للكلمة uniform، وإسقاط ما بعد المُهيّئ عند غياب الفاصلة)** — الأخيران منذ 2.3.0.

### 3.2-ب مسار تجميع/ربط البرامج (2.3.0 — كاش البرامج)

`glCompileShader` (معترَض في shader.cpp) ← تأجيل: تعليم pending فقط. `glGetShaderiv`(COMPILE_STATUS/INFO_LOG_LENGTH) و`glGetShaderInfoLog` ← إجابة من سجل إجابات الدايفر المحفوظ لكل مصدر (مفتاح SHA-256 للمصدر + إصدار ES + علم المحاكاة)، أو تجميع قسري مع تسجيل الإجابات. `glLinkProgram` (program.cpp): مفتاح SHA-256 يشمل إصدار العارض + GL_RENDERER + GL_VERSION + إصدار ES + علم المحاكاة + مصادر كل المرفقات بأنواعها وترتيبها + روابط glBindAttribLocation بترتيب النداء + نص الـdefault FS ← إصابة: `glProgramBinary` واحد + تعليم المرفقات محلولة؛ إخفاق كاش أو فشل تحميل: تجميع مؤجل المرفقات قسريًا ← ربط عادي ← عند النجاح `glGetProgramBinary` ← تخزين. الملف: `MG/pipeline_cache.bin` (سحر MGPC + إصدار 1؛ نوعان: ثنائيات برامج + سجلات حالة شيدرات؛ حد 64MB LRU موحّد؛ حفظ مؤجل 16 عنصرًا أو 5 ثوانٍ، tmp+rename). التعطيل: إعداد programBinaryCache (افتراضي مفعّل) أو MG_NO_PROGBIN=1 ← تمرير مطابق للأصل. سلامة الحواف: الأسماء المُعاد تدويرها تُمسح سجلاتها عند الإنشاء، والمؤجل لا يُقص أبدًا من جداول التتبع، ولا يُخزّن إلا ربط ناجح لدى الدايفر نفسه.

### 3.3 كيفية دمج Rust مع C++

- **CMake** (`MobileGlues-cpp/CMakeLists.txt`، قسم `mobileglues-rs`): عند البناء لأندرويد ووجود cargo يُبنى الـcrate للـtriple المطابق للـABI عبر wrapper يربط clang الخاص بالـNDK بالهدف الصحيح، والناتج `libmobileglues_rs.a` يُربط داخل `libmobileglues.so`. `MG_ENABLE_RUST=OFF` أو غياب cargo = بناء C++ خالص يعمل بالاحتياطي.
- **FFI**: دوال `extern "C"` بادئة `mg_rs_*` في `rust/src/lib.rs`؛ التصريحات في C++ كـ**weak symbols** (`gl/glsl/rs_bridge.h/.cpp`) — إن لم تُربط تبقى مؤشرات فارغة ويعمل الاحتياطي.
- **الأمان**: كل نقطة دخول Rust داخل `catch_unwind`؛ الذعر يعيد null/-1 فيتحول نداء الدالة إلى C++. `MG_RS_DISABLE=1` يعطل Rust زمن التشغيل (للمقارنة A/B والطوارئ).
- **الملكية**: نصوص تُعاد من Rust تُحرر بـ`mg_rs_free` (لا خلط mallocators).

## 4. الملفات المهمة (خريطة سريعة للتعديل)

- تعديل دوال النصوص أو إضافة جديدة: `MobileGlues/MobileGlues-cpp/rust/src/lib.rs` (التنفيذ + اختبارات الوحدة) + `gl/glsl/rs_bridge.h/.cpp` (FFI) + نقطة الاستدعاء في `gl/glsl/glsl_for_es.cpp` + حالة اختبار في `tools/rsdiff/rsdiff.cpp` وملف corpus إن كان نمطًا جديدًا.
- الكاش: `gl/glsl/cache.cpp` (مؤجّل الحفظ + memoization — محسّن سابقًا، لا تلمسه دون قياس).
- إعدادات التطبيق: `app/build.gradle.kts` (الإصدار/التسمية/التوقيع) وملفات Kotlin تحت `app/src/main/java/.../settings/`.
- الإصدار: `version_map.json` [0] + `app/build.gradle.kts` (versionCode/versionName) + قسم جديد في `CHANGELOG.md` بنمط `## [X.Y.Z] - date`.

## 5. الميزات المنفذة (حالة ما بعد 2.3.0)

- [x] دمج Rust فعلي في مسار التنفيذ (**عشر دوال** نصية) مع احتياطي كامل.
- [x] بوابة تكافؤ آلية (rsdiff) في كل بناء CI: فشل بايت واحد = فشل البناء.
- [x] قياس أداء موثق (انظر §8).
- [x] بنية مستودع مكتفية ذاتيًا (vendor بدل submodule) + توقيع إصدارات الـfork + سير نشر كامل.
- [x] سجل الأداء داخل اللعبة (2.2.0) كما هو مفصل سابقًا، و**تقرير v2 (2.3.0)**: قسم programs (زمن تجميع/ربط الدايفر + إصابات/إخفاقات كاش الثنائيات + زمن تحميلها) وفصل useProgram إلى forwarded/skipped_redundant وعمودان جديدان في CSV السجل.
- [x] **كاش برامج الشيدرات الدائم (2.3.0)**: ثنائيات glGetProgramBinary مفتاح SHA-256 شامل + تأجيل glCompileShader + سجل إجابات الدايفر لكل مصدر — بتفصيل §3.2-ب. إعداد programBinaryCache افتراضي مفعّل + MG_NO_PROGBIN=1 + مفتاح UI بالواجهتين وترجمات en/zh/ar.

## 6. الميزات قيد التطوير / المخطط لها (خارطة الطريق)

- **المرحلة 2 — منجزة**: أول تقرير أداء وصل من الجهاز الفعلي (Mali-G57، 2026-11-22): fps_avg 30.67 مع p50 16.25ms وp99 96.25ms وmax 10950ms و1% low 10.39؛ 232 إخفاق كاش ترجمة؛ use_program 33.5/إطار؛ effective_draws 59.2/إطار وstate_changes 23/إطار وbytes الرفع منخفضة — أي أن batching/state/upload تحت العتبات والبطل هو كومبايل+ربط الدايفر، ولهذا بُني كاش البرامج (2.3.0).
- **المرحلة 2-ب — انتظار تقرير 2.3.0 (الأهم الآن)**: بعد تثبيت 2.3.0 وتفعيل perfLog: التشغيل الأول بحزمة الشيدرات (يُتوقع misses مرتفعة للكاش الجديد + زمن تجميع/ربط كامل) ثم **إعادة تشغيل ثانية بنفس الحزمة** ومشاركة التقريرين — المفترض: program_binary_cache hits تحل محل معظم shader_driver_compiles وprogram_link، ويهبط frame_time_ms_max من ~11s إلى ما يمكن مقارنته. هذا هو الإثبات القياسي المنصوص عليه في قواعد المشروع.
- **المرحلة 3 — State Tracker**: تُفتح فقط إن أظهر تقرير ما بعد 2.3.0 use_program_forwarded مرتفعًا (>200/إطار فعلي للدايفر) أو state_changes >200 — عدادات v2 الآن تقيس الحقيقة لا الوصول.
- **المرحلة 4 — Buffer Management**: كما هي — بيانات الجهاز الحالية (0.035MB/إطار) لا تبررها.
- **المرحلة 5 — التوازي الآمن**: كما هي.

## 7. المشاكل المعروفة والأخطاء المصلحة

| المشكلة | الحالة | التفاصيل |
|---|---|---|
| كلمة مرور keystore الأصلي غير علنية | التفافي | مفتاح توقيع خاص بالـfork؛ لن تُثبّت نسخة الـfork فوق إصدار رسمي بنفس التوقيع (يلزم إزالة القديم أولًا). |
| `uniform Blk {...};` لا يطابق نمط موضع الإدراج | موروث (مقصود النقل الحرفي) | سلوك upstream الأصلي؛ نُقل كما هو عمدًا حفاظًا على التكافؤ. |
| pass الثاني لإعادة كتابة texelFetch (`bufferCoords`) غير قابل للتطابق عمليًا بعد pass الأول | موروث | نُقل كما هو؛ التوثيق في تعليقات الكود. |
| شريط لغات GitHub لا يظهر Rust رغم دفعه | **مصلح (مُتحقق)** | المستودع كان fork وGitHub لا يعيد حساب النسب للـforks. الحل على مرحلتين: (1) فصل الشبكة بالخطوات الرسمية؛ (2) `.gitattributes` يجعل الشريط يعكس كود الـfork نفسه — النسبة المُتحققة: **Rust 6.47%** (وKotlin 79.76%). ملاحظة: مهام الإحصائيات للدفعات اللاحقة قد تُسقط من طابور GitHub (حدث 3 مرات متتالية)؛ التحليل الأولي لمستودع حديث الإنشاء يُنفّذ دومًا — لذا نُفذت إعادة إنشاء ثانية (الوسم ثم main) فظهرت النتيجة في ثوانٍ. |
| bench() في rsdiff كان يعلق في حلقة لا تتقدم | **مصلح** | استُبدلت صيغة النمو بمضاعفة بسيطة بسقف 2^20. |

## 8. نتائج القياسات الفعلية

- **تقرير الجهاز الفعلي الأول (2.2.0 على جهاز المستخدم، Mali-G57 / Helio G99، جلسة 292 ثانية، 8964 إطارًا، 2026-11-22)**: fps_avg 30.67 (نافذة أخيرة 64.93) — p50 16.25ms — p90 35.25 — p95 47.25 — p99 96.25 — max **10950ms** — 1% low 10.39؛ draw_calls 32.4/إطار وmultidraw subdraws 28.55 في 1.78 دفعة (effective 59.18)؛ state_changes 22.96/إطار؛ use_program 33.52/إطار؛ buffer 0.035MB/إطار وtexture 0.0137MB/إطار؛ ترجمة الشيدرات: 472 مصدرًا، 240 إصابة/232 إخفاق (50.8%)، 1855ms إجمالًا. الخلاصة الموجهة للتحسين: عتبات how_to_read الأربع تحت المستوى عدا كاش الترجمة، والتوقف الأقصى = تجميع+ربط الدايفر — وهذا ما عالجه 2.3.0 (كاش الثنائيات).
- **rsdiff على مضيف التطوير بعد 2.3.0** (x86_64، g++ -O2 مقابل cargo --release، مجموع ns/نداء عبر 14 ملف corpus): getGLSLVersion 2.60x — removeLayoutBinding 8.82x — processOutColorLocations 7.85x — forceSupporterOutput 1.60x — process_sampler_buffer 22.70x — find_uniform_insert_pos 187.69x — has_texture_query_lod_def 42.26x — has_temporal_filter_def 38.68x — **process_uniform_declarations 2.01x** — **strip_line_directives 0.94x (تكافؤ؛ تنفيذ C++ الأصلي ماسح فعال لا regex — نُقل لاتساق العمارة دون تراجع)**. التكافؤ: **14/14 ملفًا بايت-ببايت على الدوال العشر**.
- **غير مقيس بعد على الجهاز**: أثر كاش البرامج على زمن التوقف عند تشغيل حزمة الشيدرات للمرة الثانية — عدادات تقرير v2 (program_binary_cache hits/misses + shader_driver_compile_ms + program_link_ms) صُممت لإثباته؛ لا تُكتب أي أرقام FPS أو توقف قبل ورود التقرير.
- **على عدّاء GitHub Actions** (التقرير الكامل مرفق بالإصدار 2.1.0): find_uniform_insert_pos **46.0x** — processOutColorLocations **8.75x** — removeLayoutBinding **8.28x** — مسبارا التعريف **13.1-13.5x** — process_sampler_buffer **11.6x** — getGLSLVersion **1.9x** — forceSupporterOutput **1.2x**. التكافؤ: PASS 12/12.
- **على مضيف التطوير** x86_64 (g++ -O2 ضد cargo --release، مجموع ns/نداء عبر 12 ملف corpus): getGLSLVersion 2.42x — removeLayoutBinding 8.51x — processOutColorLocations 8.40x — forceSupporterOutput 1.70x — process_sampler_buffer 21.26x — find_uniform_insert_pos 185.54x — مسبارا التعريف 35.5-36.8x.
- درس هندسي مهم من هذا الإصدار: المراجع الضعيفة لا تسحب أعضاء static archive — راجع GITHUB_BUILD_GUIDE §F/#7 وقاعدة فحص `nm -D` بعد كل ربط.
- getGLSLVersion: 3022→1247 ns (**2.42x**) — removeLayoutBinding: 1204453→141572 ns (**8.51x**) — processOutColorLocations: 670585→79831 ns (**8.40x**) — forceSupporterOutput: 18666→10959 ns (**1.70x**) — process_sampler_buffer: 3613421→169942 ns (**21.26x**) — find_uniform_insert_pos: 3482948→18772 ns (**185.54x**) — has_texture_query_lod_def: 832533→23427 ns (**35.54x**) — has_temporal_filter_def: 830172→22549 ns (**36.82x**).
- التكافؤ: 12/12 ملفًا متطابقًا بايت-ببايت على الدوال الثماني.
- **غير مقيس بعد**: الأداء على الجهاز الفعلي — لا تدّعِ أرقام FPS قبل قياسها (تقرير 2.2.0 وصل وحُلّل أعلاه؛ انتظار تقرير 2.3.0 الآن).

## 9. الاختبارات وحالتها

- `cargo test` (**9 اختبارات وحدة**): تعمل — تشمل الطرائق التاريخية للدوال العشر (مسح #line بمعامل starting التجاهلي، رفع precision، إسقاط ما بعد المُهيّئ بلا فاصلة، وطرائق regex السابقة).
- `rsdiff` (تكافؤ **10 دوال × 14 ملفًا**): PASS.
- `sh tests/run.sh` (host): pixel-size وframebuffer-shuffle وperf smoke (حقول v2) — و**pipeline_cache_test جديد (24 فحصًا)**: دورة حياة سجلات الشيدرات/البرامج (بما فيها الأسماء المُعاد تدويرها)، حتمية المفاتيح وحساسيتها (إصدار ES/نص المصدر/الروابط/علم default FS)، دورة سجلات الحالة، ثنائيات البرامج وثباتها عبر إعادة تشغيل محاكاة (flush+reload)، تقليد 64MB LRU، وثابت عدم قص المؤجل — كلها ناجحة.
- CI: بوابة rsdiff إلزامية قبل بناء APK (job `rust-core` ثم `build`).
- اختبار الجهاز الفعلي: تقرير 2.2.0 وُصّل وحُلّل (§8)؛ تقرير 2.3.0 (تشغيلان بنفس الحزمة) هو الانتظار الحالي.

## 10. الإصدارات وسجل الكوميتات

- **آخر إصدار منشور**: **2.3.0** (كاش برامج الشيدرات الدائم + تأجيل التجميع + نقلتان Rust جديدتان + تقرير أداء v2 + إصلاح version.h، 2026-11-22). سبقه: 2.2.0 (سجل الأداء داخل اللعبة) — 2.1.0 (أول إصدار هجين C++/Rust، أُعيد نشره بإصلاح ربط أرشيف Rust).
- **تحقق ما بعد النشر (2.1.0/2.2.0)**: فحص `nm -D` على الـAPK أكد رموز `mg_rs_*` معرفة داخل libmobileglues.so للمعماريات الأربع — Rust فعّالة في مسار التنفيذ.
- **درس إصداري (2.3.0)**: كان `version.h` (MAJOR/MINOR/REVISION) يجعل تقرير الأداء يعرض 2.0.0 رغم إصدارَي 2.1.0/2.2.0 — ترقيمه صار بندًا إلزاميًا في قائمة فحص الإصدار (GITHUB_BUILD_GUIDE §E).
- **آخر كوميتات مهمة**: راجع `git log --oneline -10` (توثّق دائمًا من الفعلي بدل الاعتماد على هذا القسم).

## 11. المرحلة الحالية والمهمة التالية

- **الحالية**: إنهاء إصدار 2.3.0 — كاش برامج الشيدرات الدائم (ثنائيات glGetProgramBinary بمفتاح SHA-256 شامل + تأجيل glCompileShader + سجل إجابات الدايفر لكل مصدر) + نقل replace_line_starting_with وprocess_uniform_declarations إلى نواة Rust (تكافؤ 14/14) + تقرير أداء v2 + إصلاح version.h + اختبار host جديد (pipeline_cache_test) + مفتاح UI وترجمات en/zh/ar.
- **المهمة التالية**: انتظار تقريري الجهاز لنسخة 2.3.0 (تشغيلان بنفس حزمة الشيدرات: الأول يبني الكاش والثاني يثبت الأثر عبر program_binary_cache hits وshader_driver_compile_ms/program_link_ms وهبوط frame_time_ms_max) ثم اختيار المرحلة التالية من §6 بناء على الأرقام الفعلية.

## 12. ما يحتاجه أي AI جديد للبدء

1. الأدوات محليًا: JDK 17، Android SDK + NDK 27.3.13750724، Rust stable (+ targets: `aarch64-linux-android armv7-linux-androideabi x86_64-linux-android i686-linux-android`)، CMake 3.22+.
2. بناء محلي سريع للنواة: `cargo test --manifest-path MobileGlues/MobileGlues-cpp/rust/Cargo.toml` ثم أداة rsdiff (الأوامر في رأس `tools/rsdiff/rsdiff.cpp`).
3. أي دالة نصية جديدة تُنقل إلى Rust: انقل السلوك حرفيًا (بما فيه الطرائق)، أضف اختبار وحدة + حالة corpus + تحقق rsdiff، واحتفظ بالاحتياطي C++.
4. أي إصدار جديد: حدّث versionCode/versionName في `app/build.gradle.kts` + أدرج عنصرًا [0] في `version_map.json` + قسم `## [الوسم]` في `CHANGELOG.md`، ادفع إلى main، وراقب `Publish Release`.
5. دروس تشغيلية من فصل الشبكة (2026-11-21): (أ) دفع حزمة كبيرة إلى مستودع فارغ حديث الإنشاء قد يفشل بخطأ thin-pack — ادفع من مستودع bare مستنسخ من البعيد أو استخدم `--no-thin`؛ (ب) إحصائيات اللغات للدفعات اللاحقة قد تُسقط من طابور GitHub — لا تفترض فشل `.gitattributes` قبل التحقق بـ`git check-attr` ومصدر linguist؛ إعادة إنشاء المستودع (حذف ← إنشاء ← دفع) تجدد التحليل الأولي فورًا؛ (ج) الوسوم لا تُلمس أبدًا — دفع الوسم بنفس الـSHA عبر مستودع جديد ليس «لمسًا». النسخ الاحتياطية للعمليات الكبيرة في `/home/z/my-project/mg-detach-backup/` (bare clone + أصول الإصدار الأصلية).
