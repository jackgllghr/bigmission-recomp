#include "platform/background_work.hpp"

#include <SDL3/SDL_system.h>

#include <algorithm>
#include <jni.h>
#include <string>

namespace sarusaru::platform {
namespace {

// SaruSaruActivity.backgroundWork(title, text, permille, ongoing): permille
// is -1 while the end is unknown.
void call_activity(std::string_view title, std::string_view text, int permille, bool ongoing) {
    // Attaches this thread to the VM if it is not yet (SDL detaches it when it ends).
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) return;
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (activity == nullptr) return;
    // The activity's own class: FindClass on a native thread sees only the system's.
    jclass type = env->GetObjectClass(activity);
    if (jmethodID method = env->GetMethodID(type, "backgroundWork", "(Ljava/lang/String;Ljava/lang/String;IZ)V")) {
        jstring jtitle = env->NewStringUTF(std::string(title).c_str());
        jstring jtext = env->NewStringUTF(std::string(text).c_str());
        if (jtitle != nullptr && jtext != nullptr)
            env->CallVoidMethod(activity, method, jtitle, jtext, static_cast<jint>(permille), static_cast<jboolean>(ongoing));
        if (jtitle != nullptr) env->DeleteLocalRef(jtitle);
        if (jtext != nullptr) env->DeleteLocalRef(jtext);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}

} // namespace

void show_background_work(std::string_view title, std::string_view text, std::optional<float> fraction) {
    const int permille = fraction ? static_cast<int>(std::clamp(*fraction, 0.0f, 1.0f) * 1000.0f) : -1;
    call_activity(title, text, permille, true);
}

void end_background_work(std::string_view title, std::string_view result) { call_activity(title, result, -1, false); }

} // namespace sarusaru::platform
