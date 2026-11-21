# AI_PROJECT_CONTEXT — ذاكرة المشروع الدائمة

> **هذا الملف هو المصدر الأساسي لحالة المشروع.** عند بدء أي محادثة جديدة: اقرأ هذا الملف أولًا، ثم `GITHUB_BUILD_GUIDE.md` عند الحاجة للبناء أو إصلاح مشاكله، ثم افحص آخر الكوميتات وتأكد أن حالة الملفات تطابق التوثيق، وتابع من آخر مرحلة مكتملة. لا تكرر عملًا منجزًا، ولا تفترض نجاح مهمة لمجرد أنها مذكورة هنا — تحقق من حالة التنفيذ.

## 1. المشروع والهدف

- **الاسم**: MobileGlues-RS (نسخة fork مطورة من MobileGlues الرسمي بواسطة MobileGL-Dev).
- **الوصف**: عارض رسومي (renderer) لماينكرافت Java Edition على أندرويد — مكتبة `libmobileglues.so` تترجم OpenGL 4.0 (النسخ 1.17+) إلى OpenGL ES 3 — تُستخدم كإضافة (plugin) داخل مشغل FCL/FoldCraftLauncher.
- **الهدف الرئيسي**: معمارية هجينة **C++ + Rust** مع تحسينات أداء هندسية حقيقية وقابلة للقياس في العارض، **دون** خفض الجودة أو مسافة العرض أو الدقة أو تعطيل المؤثرات أو حذف أي ميزة. Rust يجب أن يكون جزءًا فعليًا من مسار التنفيذ لا مجرد مكتبة شكلية.
- **جهاز المستخدم المرجعي**: معالج Helio G99، رام 8GB، المشغل Fold Launcher (FCL).
- **مستودع المشروع**: https://github.com/t86952356-eng/MobileGlues-Rust (الدفع مباشرة إلى `main`).
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
| `MobileGlues/MobileGlues-cpp/` | الكود الأصلي C++20 (~166 ألف سطر بدون third-party): `gl/` (تنفيذ OpenGL: shader/buffer/texture/framebuffer/multidraw/drawing/enable/state)، `gl/glsl/` (مسار ترجمة GLSL)، `egl/`، `gles/`، `config/`، `3rdparty/` (glslang + SPIRV-Cross + xxHash + FSR1). |
| `MobileGlues/MobileGlues-cpp/rust/` | **نواة Rust الجديدة** (mobileglues-rs v0.1.0): staticlib بلا اعتماديات، ماسحات نصوص أحادية المرور. |
| `tools/rsdiff/` | أداة التكافؤ والقياس (C++ host): تقارن Rust بـstd::regex الأصلي بايت-ببايت على `corpus/` (12 ملف GLSL تمثيليًا) وتقيس الأداء. |
| `.github/workflows/mg.yml` | سير البناء: اختبارات Rust + بوابة التكافؤ rsdiff ← بناء APK (Gradle + NDK r27 + Rust لأربع ABIs). |
| `.github/workflows/publish-release.yml` | النشر: يحل الإصدار من `version_map.json[0]`، يتخطى إن وُجدت أصول، يستدعي mg.yml، ينشئ Release بـ APK + سورس ZIP + تقرير rsdiff. |
| `version_map.json` | فهرس الإصدارات؛ العنصر [0] هو الأحدث (يحدد وسم النشر). |
| `mg-rs-signing.jks` | مفتاح توقيع الـfork (كلمة المرور الاحتياطية `mg-rs-release-2025` مضمّنة في app/build.gradle.kts بنمط FCL). `keystore.jks` الأصلي محفوظ لكن كلمة مروره غير علنية. |

### 3.2 مسار ترجمة الشيدرات (المسار الذي عُدّل في المرحلة الأولى)

`glShaderSource` (thread واحد، `shaderInfo` عام) ← تخزين مؤقت (`gl/glsl/cache.cpp`: SHA-256 + LRU + حفظ مؤجل) ← عند الفشل: `GLSLtoGLSLES_2`:
1. `preprocess_glsl`: إزالة أسطر `#line`، حقن GI_TemporalFilter (مسح أسطر uniform)، حقن textureQueryLod، حقن ماكرو MG، محاكاة samplerBuffer (إعادة كتابة texelFetch).
2. `glsl_to_spirv` (glslang) ← `spirv_to_essl` (SPIRV-Cross).
3. معالجة لاحقة: `removeLayoutBinding` ← `processOutColorLocations` ← `forceSupporterOutput` ← `process_uniform_declarations` ← `Cache::put`.

**الدوال الثماني المنقولة إلى Rust** (تُستدعى أولًا، مع احتياطي C++ بنفس الدالة): `removeLayoutBinding`، `processOutColorLocations`، `getGLSLVersion`، `forceSupporterOutput`، مرحلتا texelFetch في `process_sampler_buffer`، مسح موضع إدراج uniform في `inject_temporal_filter`، ومسبارا التعريف في `inject_textureQueryLod`/`inject_temporal_filter`.

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

## 5. الميزات المنفذة (حالة ما بعد 2.1.0)

- [x] دمج Rust فعلي في مسار التنفيذ (الدوال الثماني أعلاه) مع احتياطي كامل.
- [x] بوابة تكافؤ آلية (rsdiff) في كل بناء CI: فشل بايت واحد = فشل البناء.
- [x] قياس أداء موثق (انظر §8).
- [x] بنية مستودع مكتفية ذاتيًا (vendor بدل submodule).
- [x] توقيع إصدارات الـfork + سير نشر كامل (APK + سورس ZIP + تقرير rsdiff + changelog ثنائي اللغة).

## 6. الميزات قيد التطوير / المخطط لها (خارطة الطريق)

- **المرحلة 2 — قياس على الجهاز الفعلي**: تشغيل A/B على Helio G99 (`MG_RS_DISABLE=0/1`) مع عالم وشيدرات ثابتة، قياس FPS المتوسط وframe time و1% low وزمن تحميل العالم (نقل المزيد من الشيدرات أول تشغيل). توثيق النتائج هنا.
- **المرحلة 3 — State Tracker**: تحليل `gl/enable.cpp` + التتبع في `drawing.cpp`/`multidraw.cpp` (خفض المقارنات المتكررة، تمثيل حالات أسرع) — يحتاج قياس perfetto (ملفات `mg.pbtx` جاهزة) قبل أي تعديل.
- **المرحلة 4 — Buffer Management**: مناطق staging لإرسال بيانات الرسم (`gl/buffer.cpp`/`transfer.cpp`) وتقليل النسخ.
- **المرحلة 5 — التوازي الآمن**: تقييم نقل ترجمات مستقلة إلى خيوط عاملة مع احترام ملكية سياق GL (اليوم `glShaderSource` وحيد الخيط عبر `shaderInfo` العام).

## 7. المشاكل المعروفة والأخطاء المصلحة

| المشكلة | الحالة | التفاصيل |
|---|---|---|
| كلمة مرور keystore الأصلي غير علنية | التفافي | مفتاح توقيع خاص بالـfork؛ لن تُثبّت نسخة الـfork فوق إصدار رسمي بنفس التوقيع (يلزم إزالة القديم أولًا). |
| `uniform Blk {...};` لا يطابق نمط موضع الإدراج | موروث (مقصود النقل الحرفي) | سلوك upstream الأصلي؛ نُقل كما هو عمدًا حفاظًا على التكافؤ. |
| pass الثاني لإعادة كتابة texelFetch (`bufferCoords`) غير قابل للتطابق عمليًا بعد pass الأول | موروث | نُقل كما هو؛ التوثيق في تعليقات الكود. |
| bench() في rsdiff كان يعلق في حلقة لا تتقدم | **مصلح** | استُبدلت صيغة النمو بمضاعفة بسيطة بسقف 2^20. |

## 8. نتائج القياسات الفعلية

- **على عدّاء GitHub Actions** (التقرير الكامل مرفق بالإصدار 2.1.0): find_uniform_insert_pos **46.0x** — processOutColorLocations **8.75x** — removeLayoutBinding **8.28x** — مسبارا التعريف **13.1-13.5x** — process_sampler_buffer **11.6x** — getGLSLVersion **1.9x** — forceSupporterOutput **1.2x**. التكافؤ: PASS 12/12.
- **على مضيف التطوير** x86_64 (g++ -O2 ضد cargo --release، مجموع ns/نداء عبر 12 ملف corpus): getGLSLVersion 2.42x — removeLayoutBinding 8.51x — processOutColorLocations 8.40x — forceSupporterOutput 1.70x — process_sampler_buffer 21.26x — find_uniform_insert_pos 185.54x — مسبارا التعريف 35.5-36.8x.
- درس هندسي مهم من هذا الإصدار: المراجع الضعيفة لا تسحب أعضاء static archive — راجع GITHUB_BUILD_GUIDE §F/#7 وقاعدة فحص `nm -D` بعد كل ربط.
- getGLSLVersion: 3022→1247 ns (**2.42x**) — removeLayoutBinding: 1204453→141572 ns (**8.51x**) — processOutColorLocations: 670585→79831 ns (**8.40x**) — forceSupporterOutput: 18666→10959 ns (**1.70x**) — process_sampler_buffer: 3613421→169942 ns (**21.26x**) — find_uniform_insert_pos: 3482948→18772 ns (**185.54x**) — has_texture_query_lod_def: 832533→23427 ns (**35.54x**) — has_temporal_filter_def: 830172→22549 ns (**36.82x**).
- التكافؤ: 12/12 ملفًا متطابقًا بايت-ببايت على الدوال الثماني.
- **غير مقيس بعد**: الأداء على الجهاز الفعلي (مرحلة 2) — لا تدّعِ أرقام FPS قبل قياسها.

## 9. الاختبارات وحالتها

- `cargo test` (7 اختبارات وحدة): تعمل — تشمل حالات الطرائق الطريفة للـregex.
- `rsdiff` (تكافؤ 8 دوال × 12 ملف): PASS.
- CI: بوابة rsdiff إلزامية قبل بناء APK (job `rust-core` ثم `build`).
- اختبار جهاز فعلي: **لم يُجرَ بعد** (خطوة المرحلة 2).

## 10. الإصدارات وسجل الكوميتات

- **آخر إصدار منشور**: 2.1.0 (وسم `2.1.0`) — أول إصدار هجين C++/Rust — أصوله: APK موقّع + سورس ZIP + تقرير rsdiff + changelog عربي/إنجليزي.
- **آخر كوميتات مهمة**: راجع `git log --oneline -10` (توثّق دائمًا من الفعلي بدل الاعتماد على هذا القسم).

## 11. المرحلة الحالية والمهمة التالية

- **الحالية**: إنهاء المرحلة الأولى (دمج Rust في مسار النصوص + البنية + التوثيق + الإصدار 2.1.0).
- **المهمة التالية المقترحة**: المرحلة 2 — قياس A/B على الجهاز (Helio G99) وتوثيق أرقام FPS/frame time/زمن التحميل، ثم فتح المرحلة 3 (State Tracker) بقياس perfetto أولًا.

## 12. ما يحتاجه أي AI جديد للبدء

1. الأدوات محليًا: JDK 17، Android SDK + NDK 27.3.13750724، Rust stable (+ targets: `aarch64-linux-android armv7-linux-androideabi x86_64-linux-android i686-linux-android`)، CMake 3.22+.
2. بناء محلي سريع للنواة: `cargo test --manifest-path MobileGlues/MobileGlues-cpp/rust/Cargo.toml` ثم أداة rsdiff (الأوامر في رأس `tools/rsdiff/rsdiff.cpp`).
3. أي دالة نصية جديدة تُنقل إلى Rust: انقل السلوك حرفيًا (بما فيه الطرائق)، أضف اختبار وحدة + حالة corpus + تحقق rsdiff، واحتفظ بالاحتياطي C++.
4. أي إصدار جديد: حدّث versionCode/versionName في `app/build.gradle.kts` + أدرج عنصرًا [0] في `version_map.json` + قسم `## [الوسم]` في `CHANGELOG.md`، ادفع إلى main، وراقب `Publish Release`.
