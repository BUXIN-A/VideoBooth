#include "render/GlContext.h"

#include "util/Log.h"
#include "util/Strings.h"

#include <cstring>
#include <vector>

namespace vb {
namespace gfx {

namespace glfn {
PFNGLCREATESHADERPROC CreateShader = nullptr;
PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
PFNGLCOMPILESHADERPROC CompileShader = nullptr;
PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC DeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
PFNGLATTACHSHADERPROC AttachShader = nullptr;
PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC UseProgram = nullptr;
PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC Uniform1i = nullptr;
PFNGLUNIFORM4FPROC Uniform4f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv = nullptr;
PFNGLGENBUFFERSPROC GenBuffers = nullptr;
PFNGLBINDBUFFERPROC BindBuffer = nullptr;
PFNGLBUFFERDATAPROC BufferData = nullptr;
PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;
PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;
PFNGLGETATTRIBLOCATIONPROC GetAttribLocation = nullptr;
} // namespace glfn

namespace {

const wchar_t kDummyWindowClass[] = L"VideoBoothGlProbe";

LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

struct WglExtensions {
    PFNWGLCHOOSEPIXELFORMATARBPROC choosePixelFormat = nullptr;
    PFNWGLCREATECONTEXTATTRIBSARBPROC createContextAttribs = nullptr;
    PFNWGLSWAPINTERVALEXTPROC swapInterval = nullptr;
};

// 通过临时窗口 + 临时上下文获取 WGL 扩展入口点
bool LoadWglExtensions(WglExtensions& extensions) {
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = ProbeWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kDummyWindowClass;
    ::RegisterClassExW(&wc);

    HWND window = ::CreateWindowExW(0, kDummyWindowClass, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64,
                                    64, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        VB_ERROR("创建 OpenGL 探测窗口失败");
        return false;
    }

    HDC dc = ::GetDC(window);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 0;

    bool ok = false;
    const int format = ::ChoosePixelFormat(dc, &pfd);
    if (format != 0 && ::SetPixelFormat(dc, format, &pfd)) {
        HGLRC context = ::wglCreateContext(dc);
        if (context != nullptr && ::wglMakeCurrent(dc, context)) {
            extensions.choosePixelFormat =
                reinterpret_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(
                    reinterpret_cast<void*>(::wglGetProcAddress("wglChoosePixelFormatARB")));
            extensions.createContextAttribs =
                reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
                    reinterpret_cast<void*>(::wglGetProcAddress("wglCreateContextAttribsARB")));
            extensions.swapInterval = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(
                reinterpret_cast<void*>(::wglGetProcAddress("wglSwapIntervalEXT")));
            ::wglMakeCurrent(nullptr, nullptr);
            ok = extensions.choosePixelFormat != nullptr &&
                 extensions.createContextAttribs != nullptr;
        }
        if (context != nullptr) {
            ::wglDeleteContext(context);
        }
    }

    ::ReleaseDC(window, dc);
    ::DestroyWindow(window);
    return ok;
}

// 按采样数请求像素格式；samples 为 0 表示不使用多重采样
bool ChoosePixelFormatArb(PFNWGLCHOOSEPIXELFORMATARBPROC choose, HDC dc, bool doubleBuffer,
                          int samples, int* formatOut) {
    std::vector<int> attribs = {
        glc::kWglDrawToWindowArb, 1,
        glc::kWglSupportOpenglArb, 1,
        glc::kWglDoubleBufferArb, doubleBuffer ? 1 : 0,
        glc::kWglPixelTypeArb, 0x202B /* WGL_TYPE_RGBA_ARB */,
        glc::kWglAccelerationArb, glc::kWglFullAccelerationArb,
        glc::kWglColorBitsArb, 32,
        glc::kWglAlphaBitsArb, 8,
    };
    if (samples > 0) {
        attribs.push_back(glc::kWglSampleBuffersArb);
        attribs.push_back(1);
        attribs.push_back(glc::kWglSamplesArb);
        attribs.push_back(samples);
    }
    attribs.push_back(0);
    attribs.push_back(0);

    int format = 0;
    UINT count = 0;
    if (choose(dc, attribs.data(), nullptr, 1, &format, &count) == FALSE || count == 0) {
        return false;
    }
    *formatOut = format;
    return true;
}

void* ResolveGlProc(const char* name) {
    void* proc = reinterpret_cast<void*>(::wglGetProcAddress(name));
    if (proc == nullptr) {
        static HMODULE opengl = ::GetModuleHandleW(L"opengl32.dll");
        if (opengl != nullptr) {
            proc = reinterpret_cast<void*>(::GetProcAddress(opengl, name));
        }
    }
    return proc;
}

bool LoadGlFunctions() {
    glfn::CreateShader = reinterpret_cast<PFNGLCREATESHADERPROC>(
        ResolveGlProc("glCreateShader"));
    glfn::ShaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(
        ResolveGlProc("glShaderSource"));
    glfn::CompileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(
        ResolveGlProc("glCompileShader"));
    glfn::GetShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(ResolveGlProc("glGetShaderiv"));
    glfn::GetShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(
        ResolveGlProc("glGetShaderInfoLog"));
    glfn::DeleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(
        ResolveGlProc("glDeleteShader"));
    glfn::CreateProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(
        ResolveGlProc("glCreateProgram"));
    glfn::AttachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(
        ResolveGlProc("glAttachShader"));
    glfn::LinkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(ResolveGlProc("glLinkProgram"));
    glfn::GetProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(
        ResolveGlProc("glGetProgramiv"));
    glfn::GetProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(
        ResolveGlProc("glGetProgramInfoLog"));
    glfn::UseProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(ResolveGlProc("glUseProgram"));
    glfn::DeleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(
        ResolveGlProc("glDeleteProgram"));
    glfn::GetUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(
        ResolveGlProc("glGetUniformLocation"));
    glfn::Uniform1i = reinterpret_cast<PFNGLUNIFORM1IPROC>(ResolveGlProc("glUniform1i"));
    glfn::Uniform4f = reinterpret_cast<PFNGLUNIFORM4FPROC>(ResolveGlProc("glUniform4f"));
    glfn::UniformMatrix4fv = reinterpret_cast<PFNGLUNIFORMMATRIX4FVPROC>(
        ResolveGlProc("glUniformMatrix4fv"));
    glfn::GenBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(ResolveGlProc("glGenBuffers"));
    glfn::BindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(ResolveGlProc("glBindBuffer"));
    glfn::BufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(ResolveGlProc("glBufferData"));
    glfn::DeleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(
        ResolveGlProc("glDeleteBuffers"));
    glfn::EnableVertexAttribArray =
        reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(
            ResolveGlProc("glEnableVertexAttribArray"));
    glfn::VertexAttribPointer = reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(
        ResolveGlProc("glVertexAttribPointer"));
    glfn::ActiveTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(
        ResolveGlProc("glActiveTexture"));
    glfn::GetAttribLocation = reinterpret_cast<PFNGLGETATTRIBLOCATIONPROC>(
        ResolveGlProc("glGetAttribLocation"));

    const bool complete = glfn::CreateShader != nullptr && glfn::CreateProgram != nullptr &&
                          glfn::LinkProgram != nullptr && glfn::UseProgram != nullptr &&
                          glfn::GetUniformLocation != nullptr && glfn::GenBuffers != nullptr &&
                          glfn::VertexAttribPointer != nullptr &&
                          glfn::ActiveTexture != nullptr &&
                          glfn::GetAttribLocation != nullptr;
    if (!complete) {
        VB_ERROR("OpenGL 函数加载不完整，当前驱动可能不支持 OpenGL 2.0");
    }
    return complete;
}

// 请求的多重采样数（MSAA），驱动不支持时自动回退
constexpr int kRequestedSamples = 4;

} // namespace

GlContext::~GlContext() {
    Destroy();
}

bool GlContext::Create(HWND hwnd, bool vsync, bool doubleBuffer, bool antialias) {
    if (hwnd == nullptr || context_ != nullptr) {
        return false;
    }
    hwnd_ = hwnd;
    dc_ = ::GetDC(hwnd);
    if (dc_ == nullptr) {
        VB_ERROR("获取窗口设备上下文失败");
        return false;
    }

    WglExtensions extensions;
    if (!LoadWglExtensions(extensions)) {
        VB_ERROR("加载 WGL 扩展失败，无法创建 OpenGL 上下文");
        return false;
    }

    int samples = antialias ? kRequestedSamples : 0;
    int pixelFormat = 0;
    if (samples > 0 &&
        !ChoosePixelFormatArb(extensions.choosePixelFormat, dc_, doubleBuffer, samples,
                              &pixelFormat)) {
        VB_WARN("驱动不支持 %dx 多重采样，回退到无抗锯齿", samples);
        samples = 0;
    }
    if (samples == 0 &&
        !ChoosePixelFormatArb(extensions.choosePixelFormat, dc_, doubleBuffer, 0, &pixelFormat)) {
        VB_ERROR("未找到可用的 OpenGL 像素格式");
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    if (::DescribePixelFormat(dc_, pixelFormat, sizeof(pfd), &pfd) == 0 ||
        !::SetPixelFormat(dc_, pixelFormat, &pfd)) {
        VB_ERROR("设置 OpenGL 像素格式失败");
        return false;
    }

    const int contextAttribs[] = {
        glc::kWglContextMajorVersionArb, 2,
        glc::kWglContextMinorVersionArb, 0,
        glc::kWglContextProfileMaskArb, glc::kWglContextCompatibilityProfileBitArb,
        0};
    context_ = extensions.createContextAttribs(dc_, nullptr, contextAttribs);
    if (context_ == nullptr) {
        VB_WARN("创建 OpenGL 2.0 上下文失败，回退到默认上下文");
        context_ = ::wglCreateContext(dc_);
    }
    if (context_ == nullptr) {
        VB_ERROR("创建 OpenGL 上下文失败");
        return false;
    }

    MakeCurrent();
    if (!LoadGlFunctions()) {
        Destroy();
        return false;
    }

    if (extensions.swapInterval != nullptr) {
        extensions.swapInterval(vsync ? 1 : 0);
    }
    swapInterval_ = extensions.swapInterval;
    vsync_ = vsync;

    // 查询实际生效的采样数并启用多重采样
    if (samples > 0) {
        GLint actualSamples = 0;
        ::glGetIntegerv(glc::kSamples, &actualSamples);
        sampleCount_ = static_cast<int>(actualSamples);
        ::glEnable(glc::kMultisample);
    }

    const char* version = reinterpret_cast<const char*>(::glGetString(GL_VERSION));
    const char* renderer = reinterpret_cast<const char*>(::glGetString(GL_RENDERER));
    const char* vendor = reinterpret_cast<const char*>(::glGetString(GL_VENDOR));
    rendererInfo_ = FormatA("%s | %s | %s", vendor != nullptr ? vendor : "?",
                            renderer != nullptr ? renderer : "?",
                            version != nullptr ? version : "?");
    VB_INFO("OpenGL 上下文已创建: %s (vsync=%s, doubleBuffer=%s, MSAA=%dx)",
            rendererInfo_.c_str(), vsync ? "on" : "off", doubleBuffer ? "on" : "off",
            sampleCount_);

    ::glDisable(GL_DEPTH_TEST);
    ::glDisable(GL_CULL_FACE);
    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // 预乘 alpha 混合
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return true;
}

void GlContext::Destroy() {
    sampleCount_ = 0;
    if (context_ != nullptr) {
        ::wglMakeCurrent(nullptr, nullptr);
        ::wglDeleteContext(context_);
        context_ = nullptr;
    }
    if (dc_ != nullptr && hwnd_ != nullptr) {
        ::ReleaseDC(hwnd_, dc_);
        dc_ = nullptr;
    }
    hwnd_ = nullptr;
}

void GlContext::MakeCurrent() {
    if (context_ != nullptr && dc_ != nullptr) {
        ::wglMakeCurrent(dc_, context_);
    }
}

void GlContext::Present() {
    if (context_ != nullptr && dc_ != nullptr) {
        ::SwapBuffers(dc_);
    }
}

void GlContext::SetVsync(bool enabled) {
    vsync_ = enabled;
    if (context_ == nullptr || swapInterval_ == nullptr) {
        return;
    }
    MakeCurrent();
    swapInterval_(enabled ? 1 : 0);
    VB_INFO("垂直同步已%s", enabled ? "开启" : "关闭");
}

} // namespace gfx
} // namespace vb
