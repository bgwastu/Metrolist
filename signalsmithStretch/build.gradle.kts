import org.gradle.process.ExecOperations
import javax.inject.Inject

plugins {
    alias(libs.plugins.android.library)
}

val stretchNdkVersion = libs.versions.androidNdk.get()
val stretchCmakeVersion = "4.1.2"
val stretchAbis = listOf("arm64-v8a", "armeabi-v7a", "x86_64")

android {
    namespace = "com.metrolist.signalsmith"
    compileSdk {
        version = release(37) { minorApiLevel = 1 }
    }

    defaultConfig {
        minSdk = 24
        consumerProguardFiles("consumer-rules.pro")
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    lint {
        ignoreTestSources = true
    }
}

@CacheableTask
abstract class BuildSignalsmithJni
    @Inject
    constructor(
        private val exec: ExecOperations,
        private val fs: FileSystemOperations,
    ) : DefaultTask() {
        @get:InputFiles
        @get:PathSensitive(PathSensitivity.RELATIVE)
        @get:IgnoreEmptyDirectories
        abstract val jniSources: ConfigurableFileCollection

        @get:Input
        abstract val ndkVersion: Property<String>

        @get:Input
        abstract val cmakeVersion: Property<String>

        @get:Input
        abstract val abis: ListProperty<String>

        @get:Input
        abstract val minSdk: Property<Int>

        @get:Internal
        abstract val jniDirectory: DirectoryProperty

        @get:Internal
        abstract val sdkDirectory: DirectoryProperty

        @get:Internal
        abstract val workDirectory: DirectoryProperty

        @get:OutputDirectory
        abstract val outputDirectory: DirectoryProperty

        @TaskAction
        fun build() {
            val sdk = sdkDirectory.get().asFile
            val ndk = sdk.resolve("ndk/${ndkVersion.get()}")
            val cmakeBin = sdk.resolve("cmake/${cmakeVersion.get()}/bin")
            val executableSuffix = if (System.getProperty("os.name").startsWith("Windows")) ".exe" else ""
            if (!ndk.isDirectory || !cmakeBin.isDirectory) installToolchain(sdk)

            fs.delete { delete(outputDirectory) }
            for (abi in abis.get()) {
                val buildDir = workDirectory.get().asFile.resolve(abi)
                fs.delete { delete(buildDir) }
                exec.exec {
                    commandLine(
                        cmakeBin.resolve("cmake$executableSuffix"),
                        "-S",
                        jniDirectory.get().asFile,
                        "-B",
                        buildDir,
                        "-G",
                        "Ninja",
                        "-DCMAKE_MAKE_PROGRAM=${cmakeBin.resolve("ninja$executableSuffix")}",
                        "-DCMAKE_TOOLCHAIN_FILE=${ndk.resolve("build/cmake/android.toolchain.cmake")}",
                        "-DANDROID_ABI=$abi",
                        "-DANDROID_PLATFORM=android-${minSdk.get()}",
                        "-DCMAKE_BUILD_TYPE=Release",
                    )
                }
                exec.exec {
                    commandLine(
                        cmakeBin.resolve("cmake$executableSuffix"),
                        "--build",
                        buildDir,
                        "--target",
                        "signalsmithStretchJNI",
                    )
                }
                fs.copy {
                    from(buildDir.resolve("libsignalsmithStretchJNI.so"))
                    into(outputDirectory.get().asFile.resolve(abi))
                }
            }
        }

        private fun installToolchain(sdk: File) {
            val isWindows = System.getProperty("os.name").startsWith("Windows")
            val sdkManagerName = if (isWindows) "sdkmanager.bat" else "sdkmanager"
            val sdkManager =
                sdk
                    .resolve("cmdline-tools")
                    .listFiles()
                    .orEmpty()
                    .map { it.resolve("bin/$sdkManagerName") }
                    .firstOrNull { it.isFile && (isWindows || it.canExecute()) }
                    ?: error("Install NDK ${ndkVersion.get()} and CMake ${cmakeVersion.get()}, or the SDK command-line tools")
            val command = listOf(sdkManager.absolutePath, "--install", "ndk;${ndkVersion.get()}", "cmake;${cmakeVersion.get()}")
            exec.exec {
                commandLine(if (isWindows) listOf("cmd.exe", "/d", "/c", "call") + command else command)
            }
        }
    }

val buildSignalsmithJni =
    tasks.register<BuildSignalsmithJni>("buildSignalsmithJni") {
        jniDirectory.set(layout.projectDirectory.dir("src/main/jni"))
        jniSources.from(
            fileTree("src/main/jni") {
                exclude("**/.git", "**/.git/**")
            },
        )
        ndkVersion.set(stretchNdkVersion)
        cmakeVersion.set(stretchCmakeVersion)
        abis.set(stretchAbis)
        minSdk.set(24)
        sdkDirectory.set(androidComponents.sdkComponents.sdkDirectory)
        workDirectory.set(layout.buildDirectory.dir("signalsmith-cmake"))
        outputDirectory.set(layout.buildDirectory.dir("signalsmith-jniLibs"))
    }

androidComponents {
    onVariants { variant ->
        variant.sources.jniLibs?.addGeneratedSourceDirectory(buildSignalsmithJni, BuildSignalsmithJni::outputDirectory)
    }
}

dependencies {
    implementation(libs.media3.common)
    implementation(libs.androidx.annotation)
    compileOnly(libs.kotlin.annotations.jvm)
}
