// Linux stand-in for <jni.h>, so src/platform/android/video_gles.cpp compiles unchanged on the desktop.
//
// video_gles.cpp asks MDActivity.renderGlyphs (Java, through JNI) for its UI glyphs. Here the same call lands in
// linux_render_glyphs (linux/src/glyphs_linux.cpp, stb_truetype) instead. Only the handful of JNIEnv calls that
// file makes are provided; if upstream starts using another one, the build fails here and this file grows by one.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

typedef int32_t jint;
typedef int32_t jsize;
typedef float jfloat;
typedef uint8_t jboolean;
typedef int8_t jbyte;

struct _jobject { virtual ~_jobject() {} };
struct _jintArray : _jobject { std::vector<jint> v; };
struct _jbyteArray : _jobject { std::vector<jbyte> v; };
struct _jmethodID { int id; };
typedef _jobject* jobject;
typedef _jobject* jclass;
typedef _jintArray* jintArray;
typedef _jbyteArray* jbyteArray;
typedef _jmethodID* jmethodID;

// The glyph renderer (glyphs_linux.cpp): same contract as MDActivity.renderGlyphs. meta gets the ascent and the
// line height, then 5 ints per code point (advance * 64 or -1 if missing, left, top, width, height); the result is
// the glyph bitmaps (8-bit coverage, width x height each) one after the other.
std::vector<uint8_t> linux_render_glyphs(const std::vector<int32_t>& cps, float px, bool bold, bool title,
                                         std::vector<int32_t>& meta);

struct JNIEnv {
    jclass GetObjectClass(jobject) { return nullptr; }
    jmethodID GetStaticMethodID(jclass, const char* name, const char* sig) {
        static _jmethodID render = {1};
        return !strcmp(name, "renderGlyphs") && !strcmp(sig, "([IFZZ[I)[B") ? &render : nullptr;
    }
    jobject CallStaticObjectMethod(jclass, jmethodID m, jintArray cps, jfloat px, jboolean bold, jboolean title,
                                   jintArray meta) {
        if (!m || m->id != 1 || !cps || !meta) return nullptr;
        std::vector<int32_t> out_meta;
        std::vector<uint8_t> bits = linux_render_glyphs(cps->v, px, bold != 0, title != 0, out_meta);
        if (out_meta.size() > meta->v.size()) return nullptr;
        std::copy(out_meta.begin(), out_meta.end(), meta->v.begin());
        _jbyteArray* r = new _jbyteArray;
        r->v.assign(bits.begin(), bits.end());
        return r;
    }
    jintArray NewIntArray(jsize n) { _jintArray* a = new _jintArray; a->v.assign((size_t)n, 0); return a; }
    void SetIntArrayRegion(jintArray a, jsize start, jsize n, const jint* src) { std::copy(src, src + n, a->v.begin() + start); }
    void GetIntArrayRegion(jintArray a, jsize start, jsize n, jint* dst) { std::copy(a->v.begin() + start, a->v.begin() + start + n, dst); }
    jsize GetArrayLength(jbyteArray a) { return (jsize)a->v.size(); }
    void GetByteArrayRegion(jbyteArray a, jsize start, jsize n, jbyte* dst) { std::copy(a->v.begin() + start, a->v.begin() + start + n, dst); }
    jboolean ExceptionCheck() { return 0; }
    void ExceptionClear() {}
    void DeleteLocalRef(jobject o) { delete o; }
};

// SDL's Android hooks, as video_gles.cpp calls them: a JNIEnv that renders glyphs, and a non-null "activity".
inline void* SDL_AndroidGetJNIEnv() { static JNIEnv env; return &env; }
inline void* SDL_AndroidGetActivity() { return new _jobject; }
