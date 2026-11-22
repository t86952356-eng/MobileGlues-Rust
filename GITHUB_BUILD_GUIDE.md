# GITHUB_BUILD_GUIDE — دليل البناء والمشاكل

> دليل تشغيلي كامل لبناء المشروع عبر GitHub Actions وإصلاح مشاكله. يُحدَّث مع كل تغيير في نظام البناء أو مع كل مشكلة جديدة تظهر وتُحل. ميّز دائمًا بين **مشكلة فعلية حدثت** و**ملاحظة وقائية**.

## A. نظرة عامة على عملية البناء

```
push إلى main
   └─ Publish Release (publish-release.yml)
        ├─ prepare: قراءة version_map.json[0] → وسم الإصدار
        │           (يتخطى إن كانت للإصدار أصول موجودة)
        ├─ build: استدعاء mg.yml (workflow_call)
        │    ├─ rust-core: cargo test → بناء staticlib للمضيف →
        │    │            rsdiff (تكافؤ بايت-ببايت مقابل std::regex + قياسات)
        │    │            ⚠ أي اختلاف بايت واحد يفشل البناء بالكامل
        │    └─ build APK: Gradle assembleRelease
        │         (NDK r27 يبني C++ عبر CMake؛ CMake يستدعي cargo
        │          لبناء نواة Rust لكل ABI ويربطها داخل libmobileglues.so)
        └─ release: تنزيل الأصول → استخراج changelog →
             git archive (سورس ZIP) → إنشاء Release:
             APK + سورس ZIP + تقرير rsdiff + جسم changelog
```

مسار الملفات الناتجة: `app/build/outputs/apk/release/app-release.apk` (يُعاد تسميته إلى `MobileGlues-RS-<وسم>.apk` عند النشر).

## B. متطلبات البناء

| الأداة | الإصدار | ملاحظات |
|---|---|---|
| JDK | 17 (zulu في CI) | إلزامي لـAGP |
| Android Gradle Plugin | 8.12.3 | من `gradle/libs.versions.toml` |
| Gradle | 8.13 | wrapper مضمّن (`./gradlew`) |
| Android SDK | platform 36، compileSdk 36 | `android-actions/setup-android@v3` يقبل التراخيص وAGP يثبت المنصات |
| Android NDK | **27.3.13750724** | يُثبّت بـsdkmanager ويُسجل في local.properties |
| Rust | stable | + أهداف: `aarch64-linux-android`، `armv7-linux-androideabi`، `x86_64-linux-android`، `i686-linux-android` |
| CMake | 3.22.1+ | يأتي مع NDK |

- إصدار التطبيق: `versionCode/versionName` في `app/build.gradle.kts` (النسخة الحالية 2010 / "2.1.0").
- minSdk 26 (الإضافة)، والعارض AAR minSdk 21؛ ABIs: arm64-v8a، armeabi-v7a، x86، x86_64.
- **بلا cargo يعمل البناء أيضًا**: CMake يكتشف غيابه ويحذر ويتابع بالاحتياطي C++ (رموز weak) — لكن إصدارات Release الرسمية تُبنى دائمًا مع Rust.

## C. GitHub Actions — ملفات Workflow

| الملف | متى يعمل | ماذا يفعل |
|---|---|---|
| `.github/workflows/mg.yml` | push إلى main/dev (عدا `**.md`)، PR، استدعاء يدوي، استدعاء من الناشر | job `rust-core`: اختبارات الوحدة + بوابة التكافؤ rsdiff + تقرير قياسات (artifact `rsdiff-report`). job `build` (يعتمد على نجاح rust-core): بناء APK موقّع (artifact `mobileglues-apk`). |
| `.github/workflows/publish-release.yml` | push إلى main، أو دفع وسم، أو يدوي | job `prepare`: الوسم من `version_map.json[0]` مع تخطي الأصول الموجودة (يمنع التكرار عند دفع docs بعد الإصدار). job `build`: يستدعي mg.yml بالكامل. job `release`: APK + `git archive` سورس ZIP + تقرير rsdiff + changelog من `CHANGELOG.md` (قسم `## [الوسم]`). |

- التنافس (concurrency): مجموعة `release` واحدة بلا إلغاء — يمنع بناءين متزامنين لنفس الإصدار.
- الصلاحيات: `contents: write` لإنشاء الإصدارات. التوكن: سر `TOKEN` إن وُجد وإلا `GITHUB_TOKEN`.
- أسرار التوقيع الاختيارية: `SIGNING_STORE_PASSWORD`/`SIGNING_KEY_ALIAS`/`SIGNING_KEY_PASSWORD` — غيابها ليس مشكلة؛ fallback مضمّن (انظر E).

## D. إنتاج APK

1. CI: يعمل تلقائيًا مع كل push مؤثر (شوف A/C).
2. محليًا (اختياري للتطوير):
   ```bash
   export ANDROID_HOME=...           # SDK
   echo "ndk.dir=$ANDROID_HOME/ndk/27.3.13750724" > local.properties
   rustup target add aarch64-linux-android armv7-linux-androideabi x86_64-linux-android i686-linux-android
   ./gradlew assembleRelease
   ```
3. التحقق من النجاح: job `build` أخضر + artifact `mobileglues-apk` يحوي `app-release.apk` + سطور "mobileglues-rs: enabled for <triple> (api …)" في سجل CMake للأربع ABIs (دليل أن Rust رُبطت فعليًا).
4. فحص سريع أن Rust داخل الـso (اختياري):
   `strings libmobileglues.so | grep mobileglues-rs` — يظهر اسم النواة وإصدارها.

## E. إنشاء Release

- يُطلق آليًا من أي push إلى main **يغيّر شيئًا غير ملفات md** (سير النشر يتجاهل هذا الاستثناء عمدًا ويقرر بالأصول الموجودة). إن كان `version_map.json[0]` بلا إصدار منشور بعد → يبني وينشر.
- **قائمة فحص الإصدار (إلزامية، بهذا الترتيب):** (1) `version_map.json[0]` بالإصدار الجديد؛ (2) `app/build.gradle.kts` versionCode/versionName؛ (3) **`MobileGlues/MobileGlues-cpp/version.h` (MAJOR/MINOR/REVISION/PATCH) — درس 2.3.0: هذا الملف يغذي سطر renderer_version في تقرير الأداء وماكرو MG_MOBILEGLUES_VERSION في الشيدرات، وكان نسيانه جعل تقرير 2.2.0 يعرض 2.0.0**؛ (4) قسم جديد في CHANGELOG.md؛ (5) تحديث AI_PROJECT_CONTEXT.md (§5/§8/§10/§11).
- عناصر الإصدار: `MobileGlues-RS-<وسم>.apk` + `MobileGlues-RS-<وسم>-source.zip` (أرشيف git archive من كوميت الإصدار نفسه — يشمل العارض المتتبع كاملًا) + `rsdiff-report-<وسم>.txt` (أرقام القياس المستقلة من عداء CI) + جسم changelog ثنائي اللغة.
- التوقيع: مفتاح الـfork `mg-rs-signing.jks` (alias `mg-rs`، fallback password مضمّن في `app/build.gradle.kts`). أسرار المستودع تتفوق عند ضبطها. لا يمكن التوقيع بمفتاح upstream (كلمة مروره غير علنية) — هذا اختيار موثق لا عائق.
- قاعدة WAG: لا تعد استخدام وسم قديم أبدًا؛ كل إصدار وسم جديد من `version_map.json[0].versionName`.

## F. الأخطاء والمشاكل الفعلية (سجل)

| # | الخطأ كما ظهر | السبب الحقيقي | الحل | الملفات المعدلة | تحقق |
|---|---|---|---|---|---|
| 1 | `cargo test` فشل: تواقعات اختبار خاطئة (ترتيب precision، مسافة بعد الفاصلة في layout) | توقعات كتبت من فهم ذهني للـregex بدل السلوك الفعلي؛ std::regex له طرائق (backtracking للـgreedy `\s*` يلتقط مسافة واحدة في المؤشر الفارغ؛ `\s*` لا يوجد بين الأرقام والفاصلة فتبقى المسافة) | بناء مرجع C++ مضيف يحاكي التنفيذ الأصلي حرفيًا واعتماد خرجه كحقيقة مرجعية للتواقيع | `rust/src/lib.rs` (التوقعات + مصحح حالة المسافات-فقط في `match_texelfetch_lazy`) | نعم — 7/7 اختبارات + rsdiff 12/12 |
| 2 | `bench()` في rsdiff يعلق بلا تقدم (مهلة 300 ثانية محليًا) | صيغة نمو الاصطدات يمكن أن تصل لقيمة ثابتة (لا تتضاعف) | استبدالها بمضاعفة بسيطة بسقف 2^20 اصطدة | `tools/rsdiff/rsdiff.cpp` | نعم — اكتمل التشغيل بثوانٍ |
| 3 | (أول دفعة) Actions لا تعمل على المستودع إطلاقًا | المستودع fork والـActions معطلة فيه افتراضيًا | تمكينها عبر API: PUT `/repos/…/actions/permissions` | لا ملفات — إعداد حساب | نعم — workflow ظهر بحالة active |
| 4 | job «Build APK» يفشل مبكرًا: `Failed to find package 'tools'` ثم `sdkmanager failed with exit code 1` | إجراء android-actions/setup-android@v3 نفسه معطل upstream — يحاول تثبيت حزمة `tools` القديمة المحذوفة من مستودع Google | الاستغناء عنه: عدّاء ubuntu-latest يأتي بـSDK مسبق التثبيت وبرخص مقبولة — نضبط ANDROID_HOME ونضيف cmdline-tools/latest إلى PATH ثم نثبت NDK بـsdkmanager مباشرة | `.github/workflows/mg.yml` | نعم — في الدفعة التالية |
| 7 | **صامت وخطير**: الـAPK الأول لـ2.1.0 بُني بنجاح لكن فحص `nm -D` للـ`libmobileglues.so` المرفق أظهر رموز `mg_rs_*` كـ`w` (ضعيفة **غير معرفة**) — أي أن أرشيف Rust بُني (Compiling mobileglues-rs ×4) لكنه **لم يُربط**، فكانت كل النداءات ستعمل بالاحتياطي C++ دون إخبار أحد | سلوك GNU ld الموثق: المراجع الضعيفة غير المعرفة (weak undefined) **لا تسحب أعضاء من static archive أبدًا** — لا يُسحب عضو الأرشيف إلا لمرجع قوي | عند تعريف CMake لـ`MG_RS_INTEGRATED` تصبح التصريحات في rs_bridge.h **قوية** (تسحب الأرشيف) وتبقى ضعيفة فقط في بناء بلا Rust؛ وتوسيع تكامل CMake ليشمل مضيف Linux أتاح التحقق محليًا (كل الرموز الـ11 صارت `T` معرفة) — قاعدة ذهبية: بعد أي ربط staticlib تحقق بـ`nm -D` أن الرموز معرفة لا مجرد `w` | `gl/glsl/rs_bridge.h` + `gl/glsl/rs_bridge.cpp` + `CMakeLists.txt` | نعم — محليًا وعبر فحص الـAPK المنشور |
| 6 | `:app:packageRelease` يفشل: `KeytoolException: Failed to read key from store "mg-rs-signing.jks": keystore password was incorrect` | GitHub Actions يضبط الأسرار غير المضبوطة كمتغيرات بيئة **فارغة** (`""`) لا كغائبة — فيرجع `System.getenv` نصًا فارغًا غير-null فيدخل `""` ككلمة مرور بدل الـelvis fallback | `System.getenv(name)?.takeIf { it.isNotBlank() } ?: fallback` للقيم الثلاث | `app/build.gradle.kts` | نعم — |
| 5 | بناء العارض على مضيف Linux يفشل في `gpu_utils.cpp`: `vk_video/vulkan_video_codec_h264std.h: No such file` | ترويسات vulkin في المستودع تتطلب vk_video، وهي غير موجودة في المستودع ولا في توزيعات Linux المضيفة — لكن **NDK يوفرها في sysroot** (`usr/include/vk_video/`) ولهذا ينجح بناء أندرويد دائمًا | ليست مشكلة إصدار: بناء CI (أندرويد) سليم؛ للتحقق على المضيف أضف ترويسات vk_video من NDK عبر `-isystem` (مُوثقة هنا للتوثيق فقط) | لا شيء في السورس — بيئة المضيف فقط | نعم — بناء مضيف كامل 100% بـ`-isystem /path/to/ndk/sysroot/usr/include` |
| 8 | دفع كبير إلى مستودع فارغ حديث الإنشاء يفشل: `remote: fatal: did not receive expected object <sha>` + `remote unpack failed: index-pack failed` | دفع من المستودع العامل أرسل thin-pack يفترض كائنات أساسية (delta bases) غير موجودة عند المستلم الفارغ | الدفع من مستودع **bare** مستنسخ من البعيد (أو `git push --no-thin`) بدل المستودع العامل؛ وإن فشل الدفع مباشرة بعد إنشاء المستودع فانتظر ~15 ثانية وأعد المحاولة | لا ملفات — إجراء git فقط | نعم — الوسم دُفع من الـbare بنجاح بعد فشله من المستودع العامل |
| 9 | شريط لغات GitHub لا يُظهر Rust إطلاقًا رغم دفع نواة mobileglues-rs | المستودع كان fork، وGitHub **لا يعيد حساب نسب اللغات للـforks** — الإحصائيات تبقى مجمّدة على حالة المستودع الأصل؛ وبعد الفصل قد تُسقط مهام الإحصائيات للدفعات اللاحقة من الطابور (حدث 3 مرات متتالية) | فصل الشبكة بالخطوات الرسمية (bare ← حذف ← إنشاء ← دفع) + `.gitattributes` يحدد الاحتساب (شجرة upstream = vendored، نواة rust/ = محسوبة)؛ وعند إسقاط المهام: إعادة إنشاء المستودع تجدد التحليل الأولي فورًا | `.gitattributes` + إجراءات المستودع | نعم — Rust 6.47% ظاهرة في `/languages` وفي شريط الواجهة |

(يُضاف هنا كل خطأ فعلي جديد مع الحل والتحقق.)

## G. مشاكل متكررة محتملة وحلولها الجاهزة

- **"cargo not found — building WITHOUT the Rust core"** في سجل CMake: cargo ليس في PATH داخل job البناء. الحل: تأكد أن خطوة `dtolnay/rust-toolchain@stable` تسبق gradlew وأن الأهداف الأربع مثبتة (mg.yml يفعل).
- **فشل rsdiff (MISMATCH …)**: تعديل في Rust غيّر السلوك بايتًا واحدًا على الأقل. لا تتجاوز البوابة أبدًا — راجع الدالة في تقرير الفشل وقارن مع المرجع C++ (انسخ التنفيذ الأصلي إلى rsdiff إن تغير upstream).
- **فشل توقيع assembleRelease**: حدث فقط لو حُذف `mg-rs-signing.jks` أو غُيرت كلمة المرور المضمّنة. الحل: استرجاع الملف أو مزامنة كلمة المرور في `app/build.gradle.kts`.
- **فشل تثبيت منصات SDK**: تأكد أن `android-actions/setup-android@v3` قبل أي استخدام لـsdkmanager وأن التراخيص مقبولة (الفعل يقبلها تلقائيًا).
- **فشل rustup target**: أضف الأهداف في خطوة setup (`with: targets: …`) لا بأوامر منفصلة بعدها.
- **دفع docs بعد إصدار لا يبني شيئًا**: هذا سلوك مقصود — prepare يرى الأصول ويتخطى. لا تصلح ما ليس معطوبًا.
- **إعادة بناء rust-core بلا سبب**: مسار `paths-ignore` لا يشمل `tools/rsdiff/**` عمدًا — أي تعديل على النواة أو أداة التكافؤ يستحق إعادة التحقق.

## H. سجل تغييرات نظام البناء

- **2026-11-21 (2.1.0)**: أول هيكل كامل — mg.yml أعيدت كتابته (إضافة job rust-core كبوابة إلزامية + Rust toolchain + أهداف أندرويد الأربع)، أضيف publish-release.yml بنمط مشروع FCL (version_map + تخطي الأصول + APK/سورس ZIP/تقرير rsdiff + changelog ثنائي اللغة)، حُوّل العارض من submodule إلى مصدر متتبع (لم يعد `submodules: recursive` لازمًا)، أضيف توقيع الـfork.
- **2026-11-21 (فصل الشبكة)**: حُوّل المستودع من fork إلى مستودع مستقل بالخطوات اليدوية الرسمية (bare clone ← حذف ← إعادة إنشاء بنفس الاسم ← دفع mirror) لأن GitHub لا يعيد حساب نسب اللغات للـforks — لا يتأثر البناء إطلاقًا؛ سير العمل والوسم والإصدار أعيد تشغيلها آليًا على المستودع الجديد بنفس الـSHA. ملاحظة تشغيلية: أول دفع بعد الفصل أنشأ Release 2.1.0 تلقائيًا (لم يجد أصولًا في المستودع الفارغ) — هذا هو السلوك المتوقع لتخطي الأصول، لا يعتبر خللًا. أُضيف `.gitattributes` لضبط شريط اللغات (Rust محسوبة، upstream vendored مستبعد).
- **2026-11-22 (2.2.0)**: لا تغيير في هيكل البناء — أُضيفت `gl/perf.cpp` إلى قائمة مصادر CMake (وحدة سجل الأداء، انظر AI_PROJECT_CONTEXT §5-6) وأُضيف اختبار `tests/perf_smoke_test.cpp` إلى `sh MobileGlues-cpp/tests/run.sh` (اختبار host اختياري لا يدخل CI؛ ينتظر ~11 ثانية لعبور نافذة تقرير واحدة). الباقي كما في 2.1.0.
- **2026-11-22 (2.3.0)**: أُضيفت `gl/pipeline_cache.cpp` إلى قائمة مصادر CMake (وحدة كاش برامج الشيدرات — تخزين/تحميل ثنائيات glGetProgramBinary، بلا أي استدعاء GL في طبقة المخزن كي تبقى قابلة لاختبار host) وأُضيف اختبار `tests/pipeline_cache_test.cpp` (24 فحصًا) إلى `sh MobileGlues-cpp/tests/run.sh`. اعتراضات جديدة لمداخل GL نُقلت من التمرير الماكروي في gl_native.cpp إلى ملفاتها: glCompileShader وglGetShaderInfoLog (إلى gl/shader.cpp)، وglBindAttribLocation وglDetachShader (إلى gl/program.cpp) — النمط المعتمد: تعليق السطر في gl_native.cpp بملاحظة وجهته + التصريح extern "C" في ترويسة الملف الهدف قبل التعريف. درس إصداري موثق في §E: ترقيم `version.h` إلزامي مع كل إصدار (إغفاله جعل تقرير 2.2.0 يعرض renderer_version 2.0.0).
- (يُحدَّث هذا القسم مع كل تعديل على نظام البناء.)
