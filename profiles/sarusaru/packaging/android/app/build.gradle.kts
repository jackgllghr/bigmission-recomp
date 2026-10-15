import java.net.URI
import java.security.MessageDigest

plugins {
    id("com.android.application")
}

// The repository, whose CMake project builds libmain.so (the port's shell)
// and everything it loads.
val repository: File = rootDir.resolve("../../../..").canonicalFile
val profile: File = repository.resolve("profiles/sarusaru")

// One pin for everything: the version comes from the CMake project, and
// SDL's Java half from the same release (and hash) as its C half.
fun cmakeValue(file: File, pattern: String): String =
    Regex(pattern).find(file.readText())?.groupValues?.get(1)
        ?: throw GradleException("cannot find $pattern in $file")
val portVersion = cmakeValue(repository.resolve("CMakeLists.txt"), """project\(PSPRecomp VERSION ([0-9.]+)""")
val graphics = profile.resolve("cmake/Graphics.cmake")
val sdlVersion = cmakeValue(graphics, """set\(SARUSARU_SDL3_VERSION ([0-9.]+)\)""")
val sdlSha256 = cmakeValue(graphics, """set\(SARUSARU_SDL3_SHA256 ([0-9a-f]{64})\)""")
val sdlUrl = "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVersion/SDL3-$sdlVersion.tar.gz"

val generated = layout.buildDirectory.dir("generated/sarusaru")

// SDL's Java half, from the pinned SDL release.
abstract class SdlJavaSources : DefaultTask() {
    @get:Input abstract val url: Property<String>
    @get:Input abstract val sha256: Property<String>
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
        versionCode = 1
        versionName = portVersion
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DPSPRECOMP_PROFILE=sarusaru",
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

    buildTypes {
        all {
            val assets = generated.get().dir("assets/$name").asFile
            externalNativeBuild.cmake.arguments += "-DSARUSARU_ANDROID_ASSETS=$assets"
            sourceSets.getByName(name).assets.directories += assets.path
        }
        release {
            isMinifyEnabled = false
            // Unsigned until the release policy (PLAN.md, milestone 7) settles signing.
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

// The assets come out of the native build (SARUSARU_ANDROID_ASSETS).
tasks.configureEach {
    val variant = Regex("""^merge(\w+)Assets$""").find(name)?.groupValues?.get(1)
    if (variant != null) dependsOn("externalNativeBuild$variant")
}
