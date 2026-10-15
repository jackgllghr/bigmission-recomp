import java.net.URI
import java.security.MessageDigest

plugins {
    id("com.android.application")
}

// The repository, whose CMake project builds libmain.so (the port's shell)
// and everything it loads.
val repository: File = rootDir.resolve("../../../..").canonicalFile
val profile: File = repository.resolve("profiles/sarusaru")

// One release identity for CMake, packaging and the APK;
// SDL's Java half from the same release (and hash) as its C half.
fun cmakeValue(file: File, pattern: String): String =
    Regex(pattern).find(file.readText())?.groupValues?.get(1)
        ?: throw GradleException("cannot find $pattern in $file")
val portVersion = providers.gradleProperty("releaseVersion").orElse(provider {
    profile.resolve("VERSION").readText().trim()
}).get()
val versionParts = Regex("""(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)""").matchEntire(portVersion)
    ?.groupValues?.drop(1)?.map { it.toLong() }
    ?: throw GradleException("releaseVersion must be X.Y.Z")
if (versionParts[1] > 999 || versionParts[2] > 999) throw GradleException("minor and patch must be at most 999")
val releaseVersionCode = versionParts[0] * 1_000_000L + versionParts[1] * 1_000L + versionParts[2] + 1L
if (releaseVersionCode !in 1..2_100_000_000L) throw GradleException("releaseVersion is outside Android's versionCode range")
val signingStore = providers.environmentVariable("SARUSARU_ANDROID_KEYSTORE").orNull
val releaseRequested = gradle.startParameter.taskNames.any { it.contains("Release", ignoreCase = true) }
if (releaseRequested && signingStore.isNullOrBlank()) throw GradleException("Release builds require SARUSARU_ANDROID_KEYSTORE and signing credentials")
val graphics = profile.resolve("cmake/Graphics.cmake")
val sdlVersion = cmakeValue(graphics, """set\(SARUSARU_SDL3_VERSION ([0-9.]+)\)""")
val sdlSha256 = cmakeValue(graphics, """set\(SARUSARU_SDL3_SHA256 ([0-9a-f]{64})\)""")
val sdlUrl = "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVersion/SDL3-$sdlVersion.tar.gz"

val generated = layout.buildDirectory.dir("generated/sarusaru")

// SDL's Java half, from the pinned SDL release.
abstract class SdlJavaSources : DefaultTask() {
    @get:Input abstract val url: Property<String>
    @get:Input abstract val sha256: Property<String>
    // Bump when the patch below changes, so the sources are extracted again.
    @get:Input val patchVersion = 1
    @get:Internal abstract val archive: RegularFileProperty
    @get:OutputDirectory abstract val outputDir: DirectoryProperty
    @get:Inject abstract val files: FileSystemOperations
    @get:Inject abstract val archives: ArchiveOperations

    @TaskAction
    fun extract() {
        val file = archive.get().asFile
        fun digest(of: File) = MessageDigest.getInstance("SHA-256").digest(of.readBytes())
            .joinToString("") { "%02x".format(it) }
        if (!file.isFile || digest(file) != sha256.get()) {
            file.parentFile.mkdirs()
            val part = File(file.path + ".part")
            URI(url.get()).toURL().openStream().use { input -> part.outputStream().use { input.copyTo(it) } }
            val actual = digest(part)
            if (actual != sha256.get()) {
                part.delete()
                throw GradleException("${url.get()} has SHA-256 $actual, expected ${sha256.get()}")
            }
            part.renameTo(file)
        }
        files.sync {
            from(archives.tarTree(archives.gzip(file)))
            include("*/android-project/app/src/main/java/**")
            eachFile { path = path.substringAfter("/app/src/main/java/") }
            includeEmptyDirs = false
            into(outputDir)
        }
        // The game runs on SDL's thread with deeply chained guest calls, which
        // need the stack a desktop main thread has (64 MiB, as on Windows),
        // not a Java thread's default of about 1 MiB.
        val activity = outputDir.get().file("org/libsdl/app/SDLActivity.java").asFile
        val start = "new Thread(new SDLMain(), \"SDLThread\")"
        val source = activity.readText()
        if (source.split(start).size != 2) throw GradleException("SDLActivity.java no longer starts SDLThread as expected")
        activity.writeText(source.replace(start, "new Thread(null, new SDLMain(), \"SDLThread\", 64L * 1024 * 1024)"))
    }
}

// The launcher icon is the logo the desktop builds use.
abstract class LauncherIcon : DefaultTask() {
    @get:InputFile abstract val logo: RegularFileProperty
    @get:OutputDirectory abstract val outputDir: DirectoryProperty

    @TaskAction
    fun copy() {
        val mipmap = outputDir.get().dir("mipmap").asFile
        mipmap.mkdirs()
        logo.get().asFile.copyTo(mipmap.resolve("ic_launcher.png"), overwrite = true)
    }
}

val sdlJava = tasks.register<SdlJavaSources>("sdlJava") {
    url = sdlUrl
    sha256 = sdlSha256
    archive = layout.buildDirectory.file("downloads/SDL3-$sdlVersion.tar.gz")
}
val launcherIcon = tasks.register<LauncherIcon>("launcherIcon") {
    logo = layout.file(provider { repository.resolve("include/image/logo.png") })
}

androidComponents {
    onVariants { variant ->
        variant.sources.java?.addGeneratedSourceDirectory(sdlJava, SdlJavaSources::outputDir)
        variant.sources.res?.addGeneratedSourceDirectory(launcherIcon, LauncherIcon::outputDir)
    }
}

android {
    namespace = "io.github.sarusaru_recomp"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "io.github.sarusaru_recomp.SaruSaruRecomp"
        // Android 10: Vulkan 1.1 on every device that has it.
        minSdk = 29
        targetSdk = 36
        versionCode = releaseVersionCode.toInt()
        versionName = portVersion
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DPSPRECOMP_PROFILE=sarusaru",
                    "-DSARUSARU_RELEASE_VERSION=$portVersion",
                    "-DPSPRECOMP_BUILD_TESTS=OFF",
                    "-DPSPRECOMP_BUILD_PROFILE_TESTS=OFF",
                    // One C++ runtime for libmain.so, the shared PSPRecomp
                    // runtime and (later) the game module built on the device.
                    "-DANDROID_STL=c++_shared",
                )
                targets += "sarusaru"
            }
        }
    }

    signingConfigs {
        if (!signingStore.isNullOrBlank()) create("release") {
            storeFile = file(signingStore)
            storePassword = providers.environmentVariable("SARUSARU_ANDROID_STORE_PASSWORD").get()
            keyAlias = providers.environmentVariable("SARUSARU_ANDROID_KEY_ALIAS").get()
            keyPassword = providers.environmentVariable("SARUSARU_ANDROID_KEY_PASSWORD").get()
        }
    }
    buildTypes {
        all {
            // Filled by the native build: the shipped resources and the SDK
            // as assets, the code generator and compiler as native libraries.
            val assets = generated.get().dir("assets/$name").asFile
            val jniLibs = generated.get().dir("jniLibs/$name").asFile
            externalNativeBuild.cmake.arguments += listOf("-DSARUSARU_ANDROID_ASSETS=$assets",
                "-DSARUSARU_ANDROID_JNILIBS=$jniLibs")
            sourceSets.getByName(name).assets.directories += assets.path
            sourceSets.getByName(name).jniLibs.directories += jniLibs.path
        }
        debug {
            // Optimised native code with its debug information: at -O0 the
            // code generator alone takes over half an hour on a phone.
            externalNativeBuild.cmake.arguments += "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
        }
        release {
            isMinifyEnabled = false
            if (!signingStore.isNullOrBlank()) signingConfig = signingConfigs.getByName("release")
            externalNativeBuild.cmake.arguments += "-DCMAKE_BUILD_TYPE=Release"
        }
    }

    externalNativeBuild {
        cmake {
            path = repository.resolve("CMakeLists.txt")
            version = "4.1.2"
        }
    }

    packaging {
        jniLibs {
            // Extracted to the native library directory, where the on-device
            // build will link the game module against them.
            useLegacyPackaging = true
        }
    }

    lint {
        abortOnError = false
    }
}

// The assets and extra native libraries come out of the native build
// (SARUSARU_ANDROID_ASSETS and SARUSARU_ANDROID_JNILIBS).
tasks.configureEach {
    val variant = Regex("""^merge(\w+)(Assets|JniLibFolders)$""").find(name)?.groupValues?.get(1)
    if (variant != null) dependsOn("externalNativeBuild$variant")
}

// Aggregate tasks (assemble/build) also must not silently produce unsigned APKs.
tasks.matching { it.name == "preReleaseBuild" }.configureEach {
    doFirst {
        if (signingStore.isNullOrBlank()) throw GradleException("Release builds require SARUSARU_ANDROID_KEYSTORE and signing credentials")
    }
}
