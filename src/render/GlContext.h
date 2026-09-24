#pragma once

#include <windows.h>

#include <GL/gl.h>

#include <string>

// Windows SDK 仅提供 GL 1.1 头文件，这里补充所需的类型、常量与函数指针
#ifndef GL_VERSION_2_0
typedef char GLchar;
#endif
#ifndef GL_VERSION_1_5
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#endif

typedef GLuint(APIENTRY* PFNGLCREATESHADERPROC)(GLenum);
typedef void(APIENTRY* PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*,
                                              const GLint*);
typedef void(APIENTRY* PFNGLCOMPILESHADERPROC)(GLuint);
typedef void(APIENTRY* PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void(APIENTRY* PFNGLDELETESHADERPROC)(GLuint);
typedef GLuint(APIENTRY* PFNGLCREATEPROGRAMPROC)(void);
typedef void(APIENTRY* PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void(APIENTRY* PFNGLLINKPROGRAMPROC)(GLuint);
typedef void(APIENTRY* PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void(APIENTRY* PFNGLUSEPROGRAMPROC)(GLuint);
typedef void(APIENTRY* PFNGLDELETEPROGRAMPROC)(GLuint);
typedef GLint(APIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar*);
typedef void(APIENTRY* PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void(APIENTRY* PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void(APIENTRY* PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void(APIENTRY* PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void(APIENTRY* PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void(APIENTRY* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean,
                                                     GLsizei, const void*);
typedef void(APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum);
typedef GLint(APIENTRY* PFNGLGETATTRIBLOCATIONPROC)(GLuint, const GLchar*);

typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT,
                                                    int*, UINT*);
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
typedef const char*(WINAPI* PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC);

namespace vb {
namespace gfx {

// GL 1.1 之外的枚举常量（Windows SDK 未提供）
namespace glc {
inline constexpr GLenum kArrayBuffer = 0x8892;
inline constexpr GLenum kDynamicDraw = 0x88E8;
inline constexpr GLenum kVertexShader = 0x8B31;
inline constexpr GLenum kFragmentShader = 0x8B30;
inline constexpr GLenum kCompileStatus = 0x8B81;
inline constexpr GLenum kLinkStatus = 0x8B82;
inline constexpr GLenum kInfoLogLength = 0x8B84;
inline constexpr GLenum kTexture0 = 0x84C0;
inline constexpr GLenum kClampToEdge = 0x812F;
inline constexpr GLenum kLinear = 0x2601;
inline constexpr GLenum kMultisample = 0x809D;
inline constexpr GLenum kSamples = 0x80A9;

inline constexpr int kWglDrawToWindowArb = 0x2001;
inline constexpr int kWglAccelerationArb = 0x2003;
inline constexpr int kWglSupportOpenglArb = 0x2010;
inline constexpr int kWglDoubleBufferArb = 0x2011;
inline constexpr int kWglPixelTypeArb = 0x2013;
inline constexpr int kWglColorBitsArb = 0x2014;
inline constexpr int kWglAlphaBitsArb = 0x201B;
inline constexpr int kWglFullAccelerationArb = 0x2027;
inline constexpr int kWglSampleBuffersArb = 0x2041;
inline constexpr int kWglSamplesArb = 0x2042;

inline constexpr int kWglContextMajorVersionArb = 0x2091;
inline constexpr int kWglContextMinorVersionArb = 0x2092;
inline constexpr int kWglContextProfileMaskArb = 0x9126;
inline constexpr int kWglContextCompatibilityProfileBitArb = 0x00000002;
} // namespace glc

// GL 1.2+ 函数指针（由 GlContext 创建上下文后加载）
namespace glfn {
extern PFNGLCREATESHADERPROC CreateShader;
extern PFNGLSHADERSOURCEPROC ShaderSource;
extern PFNGLCOMPILESHADERPROC CompileShader;
extern PFNGLGETSHADERIVPROC GetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
extern PFNGLDELETESHADERPROC DeleteShader;
extern PFNGLCREATEPROGRAMPROC CreateProgram;
extern PFNGLATTACHSHADERPROC AttachShader;
extern PFNGLLINKPROGRAMPROC LinkProgram;
extern PFNGLGETPROGRAMIVPROC GetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC UseProgram;
extern PFNGLDELETEPROGRAMPROC DeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
extern PFNGLUNIFORM1IPROC Uniform1i;
extern PFNGLUNIFORM4FPROC Uniform4f;
extern PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
extern PFNGLGENBUFFERSPROC GenBuffers;
extern PFNGLBINDBUFFERPROC BindBuffer;
extern PFNGLBUFFERDATAPROC BufferData;
extern PFNGLDELETEBUFFERSPROC DeleteBuffers;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
extern PFNGLACTIVETEXTUREPROC ActiveTexture;
extern PFNGLGETATTRIBLOCATIONPROC GetAttribLocation;
} // namespace glfn

// WGL 扩展：创建 OpenGL 上下文
class GlContext {
public:
    ~GlContext();

    // antialias 为真时请求 4x 多重采样（MSAA），驱动不支持时自动回退到无抗锯齿
    bool Create(HWND hwnd, bool vsync, bool doubleBuffer, bool antialias);
    void Destroy();

    void MakeCurrent();
    void Present();
    // 垂直同步可运行期切换（双缓冲需重建窗口，只能重启生效）
    void SetVsync(bool enabled);
    bool vsync() const { return vsync_; }

    bool IsValid() const { return context_ != nullptr; }
    // 实际生效的 MSAA 采样数（0 表示未启用）
    int sampleCount() const { return sampleCount_; }
    const std::string& rendererInfo() const { return rendererInfo_; }

private:
    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC context_ = nullptr;
    PFNWGLSWAPINTERVALEXTPROC swapInterval_ = nullptr;
    bool vsync_ = false;
    int sampleCount_ = 0;
    std::string rendererInfo_;
};

} // namespace gfx
} // namespace vb
