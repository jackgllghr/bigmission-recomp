#include "platform/save_transfer.hpp"

#include <SDL3/SDL_system.h>

#include <jni.h>

namespace sarusaru::platform {

// SaruSaruActivity.saveTransfer(mode, directory, prefix, texts): mode 0
// exports, 1 imports a zip, 2 imports a folder.
bool start_save_transfer(SaveTransfer transfer, const std::filesystem::path &savedata, std::string_view prefix,
                         const SaveTransferTexts &texts) {
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) return false;
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (activity == nullptr) return false;
    bool started = false;
    jclass type = env->GetObjectClass(activity);
    jclass string_type = env->FindClass("java/lang/String");
    jmethodID method = env->GetMethodID(type, "saveTransfer", "(ILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;)Z");
    if (method != nullptr && string_type != nullptr) {
        const auto directory = savedata.u8string();
        jstring jdirectory = env->NewStringUTF(std::string(directory.begin(), directory.end()).c_str());
        jstring jprefix = env->NewStringUTF(std::string(prefix).c_str());
        jobjectArray jtexts = env->NewObjectArray(4, string_type, nullptr);
        if (jdirectory != nullptr && jprefix != nullptr && jtexts != nullptr) {
            const std::string *values[] = {&texts.exported, &texts.imported, &texts.nothing_found, &texts.failed};
            for (jsize i = 0; i < 4; ++i) {
                jstring text = env->NewStringUTF(values[i]->c_str());
                env->SetObjectArrayElement(jtexts, i, text);
                if (text != nullptr) env->DeleteLocalRef(text);
            }
            const jint mode = transfer == SaveTransfer::Export ? 0 : transfer == SaveTransfer::ImportArchive ? 1 : 2;
            started = env->CallBooleanMethod(activity, method, mode, jdirectory, jprefix, jtexts) == JNI_TRUE;
        }
        if (jdirectory != nullptr) env->DeleteLocalRef(jdirectory);
        if (jprefix != nullptr) env->DeleteLocalRef(jprefix);
        if (jtexts != nullptr) env->DeleteLocalRef(jtexts);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        started = false;
    }
    if (string_type != nullptr) env->DeleteLocalRef(string_type);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    return started;
}

} // namespace sarusaru::platform
