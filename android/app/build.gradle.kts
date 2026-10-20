plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.micronut99.ares"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.micronut99.ares"
        minSdk = 26          // AAudio
        targetSdk = 34
        versionCode = 1
        versionName = "148-cd32x-1"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release")
                cppFlags += listOf("-O3")
            }
        }
    }

    signingConfigs {
        // A fixed debug-style key kept in the repo, so every CI build can update the last one
        // on the phone without uninstalling (an APK signed with a new key can't update).
        create("ci") {
            storeFile = file("ci-signing.jks")
            storePassword = "ares-cd32x"
            keyAlias = "ares"
            keyPassword = "ares-cd32x"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("ci")
        }
        debug {
            signingConfig = signingConfigs.getByName("ci")
            externalNativeBuild { cmake { arguments += listOf("-DCMAKE_BUILD_TYPE=Release") } }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../native/CMakeLists.txt")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    packaging { jniLibs { useLegacyPackaging = false } }
}
