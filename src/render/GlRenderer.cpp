#include "render/GlRenderer.h"

#include "util/Log.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace vb {
namespace gfx {
namespace {

const char* kVertexShaderSource = R"(#version 110
attribute vec2 aPosition;
attribute vec2 aTexCoord;
// 声明为 vec4 以匹配 glUniform4f，避免 uniform 尺寸不匹配导致设置失败
uniform vec4 uViewport;
varying vec2 vTexCoord;
void main() {
    vTexCoord = aTexCoord;
    vec2 ndc = vec2((aPosition.x / uViewport.x) * 2.0 - 1.0,
                    1.0 - (aPosition.y / uViewport.y) * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

const char* kFragmentShaderSource = R"(#version 110
uniform sampler2D uTexture;
uniform vec4 uTint;
varying vec2 vTexCoord;
void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord) * uTint;
}
)";

GLuint CompileShader(GLenum type, const char* source) {
    const GLuint shader = glfn::CreateShader(type);
    if (shader == 0) {
        return 0;
    }
    glfn::ShaderSource(shader, 1, &source, nullptr);
    glfn::CompileShader(shader);

    GLint status = 0;
    glfn::GetShaderiv(shader, glc::kCompileStatus, &status);
    if (status == 0) {
        char log[1024] = {};
        GLsizei length = 0;
        glfn::GetShaderInfoLog(shader, sizeof(log) - 1, &length, log);
        VB_ERROR("着色器编译失败: %s", log);
        glfn::DeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Texture::~Texture() {
    Destroy();
}

bool Texture::Upload(const uint8_t* bgra, int width, int height, int stride) {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (stride <= 0) {
        stride = width * 4;
    }

    const uint8_t* source = bgra;
    if (stride != width * 4) {
        packed_.resize(static_cast<size_t>(width) * 4u * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y) {
            const uint8_t* src = bgra + static_cast<ptrdiff_t>(y) * stride;
            uint8_t* dst = packed_.data() + static_cast<size_t>(y) * width * 4u;
            for (int x = 0; x < width * 4; ++x) {
                dst[x] = src[x];
            }
        }
        source = packed_.data();
    }

    if (id_ == 0) {
        ::glGenTextures(1, &id_);
        if (id_ == 0) {
            return false;
        }
    }

    const bool sameSize = (width_ == width && height_ == height);
    ::glBindTexture(GL_TEXTURE_2D, id_);
    if (!sameSize) {
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(glc::kLinear));
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(glc::kLinear));
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(glc::kClampToEdge));
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(glc::kClampToEdge));
        ::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA_EXT,
                       GL_UNSIGNED_BYTE, source);
        width_ = width;
        height_ = height;
    } else {
        ::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        ::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA_EXT,
                          GL_UNSIGNED_BYTE, source);
    }
    ::glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool Texture::UploadRegion(const uint8_t* bgra, int width, int height, int stride,
                           const RECT& region) {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (stride <= 0) {
        stride = width * 4;
    }
    // 尺寸不一致或尚未创建时退化为整图上传
    if (id_ == 0 || width_ != width || height_ != height || stride != width * 4) {
        return Upload(bgra, width, height, stride);
    }

    RECT area = region;
    area.left = std::max<LONG>(0, std::min<LONG>(area.left, width));
    area.top = std::max<LONG>(0, std::min<LONG>(area.top, height));
    area.right = std::max<LONG>(0, std::min<LONG>(area.right, width));
    area.bottom = std::max<LONG>(0, std::min<LONG>(area.bottom, height));
    const int areaWidth = static_cast<int>(area.right - area.left);
    const int areaHeight = static_cast<int>(area.bottom - area.top);
    if (areaWidth <= 0 || areaHeight <= 0) {
        return true;
    }

    ::glBindTexture(GL_TEXTURE_2D, id_);
    ::glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
    ::glPixelStorei(GL_UNPACK_SKIP_PIXELS, static_cast<GLint>(area.left));
    ::glPixelStorei(GL_UNPACK_SKIP_ROWS, static_cast<GLint>(area.top));
    ::glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(area.left),
                      static_cast<GLint>(area.top), areaWidth, areaHeight, GL_BGRA_EXT,
                      GL_UNSIGNED_BYTE, bgra);
    ::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    ::glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    ::glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    ::glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Texture::Destroy() {
    if (id_ != 0) {
        ::glDeleteTextures(1, &id_);
        id_ = 0;
    }
    width_ = 0;
    height_ = 0;
    packed_.clear();
}

GlRenderer::~GlRenderer() {
    Shutdown();
}

bool GlRenderer::Init() {
    const GLuint vertexShader = CompileShader(glc::kVertexShader, kVertexShaderSource);
    const GLuint fragmentShader = CompileShader(glc::kFragmentShader, kFragmentShaderSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        return false;
    }

    program_ = glfn::CreateProgram();
    glfn::AttachShader(program_, vertexShader);
    glfn::AttachShader(program_, fragmentShader);
    glfn::LinkProgram(program_);

    GLint status = 0;
    glfn::GetProgramiv(program_, glc::kLinkStatus, &status);
    glfn::DeleteShader(vertexShader);
    glfn::DeleteShader(fragmentShader);
    if (status == 0) {
        char log[1024] = {};
        GLsizei length = 0;
        glfn::GetProgramInfoLog(program_, sizeof(log) - 1, &length, log);
        VB_ERROR("着色器链接失败: %s", log);
        glfn::DeleteProgram(program_);
        program_ = 0;
        return false;
    }

    uniformViewport_ = glfn::GetUniformLocation(program_, "uViewport");
    uniformTexture_ = glfn::GetUniformLocation(program_, "uTexture");
    uniformTint_ = glfn::GetUniformLocation(program_, "uTint");
    attribPosition_ = glfn::GetAttribLocation(program_, "aPosition");
    attribTexCoord_ = glfn::GetAttribLocation(program_, "aTexCoord");

    glfn::GenBuffers(1, &vbo_);
    if (vbo_ == 0) {
        VB_ERROR("创建顶点缓冲失败");
        return false;
    }

    const uint8_t white[4] = {255, 255, 255, 255};
    if (!whiteTexture_.Upload(white, 1, 1, 4)) {
        VB_ERROR("创建纯色纹理失败");
        return false;
    }
    return true;
}

void GlRenderer::Shutdown() {
    whiteTexture_.Destroy();
    if (vbo_ != 0) {
        glfn::DeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (program_ != 0) {
        glfn::DeleteProgram(program_);
        program_ = 0;
    }
}

void GlRenderer::BeginFrame(int width, int height, const Color& clearColor) {
    viewportWidth_ = width > 0 ? width : 1;
    viewportHeight_ = height > 0 ? height : 1;

    ::glViewport(0, 0, viewportWidth_, viewportHeight_);
    ::glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    ::glClear(GL_COLOR_BUFFER_BIT);

    if (program_ == 0) {
        return;
    }
    glfn::UseProgram(program_);
    if (uniformViewport_ >= 0) {
        glfn::Uniform4f(uniformViewport_, static_cast<float>(viewportWidth_),
                        static_cast<float>(viewportHeight_), 0.0f, 0.0f);
    }
    if (uniformTexture_ >= 0) {
        glfn::Uniform1i(uniformTexture_, 0);
    }
    glfn::ActiveTexture(glc::kTexture0);
    glfn::BindBuffer(glc::kArrayBuffer, vbo_);
    if (attribPosition_ >= 0) {
        glfn::EnableVertexAttribArray(static_cast<GLuint>(attribPosition_));
        glfn::VertexAttribPointer(static_cast<GLuint>(attribPosition_), 2, GL_FLOAT, GL_FALSE,
                                  sizeof(Vertex), reinterpret_cast<const void*>(0));
    }
    if (attribTexCoord_ >= 0) {
        glfn::EnableVertexAttribArray(static_cast<GLuint>(attribTexCoord_));
        glfn::VertexAttribPointer(static_cast<GLuint>(attribTexCoord_), 2, GL_FLOAT, GL_FALSE,
                                  sizeof(Vertex),
                                  reinterpret_cast<const void*>(offsetof(Vertex, u)));
    }
}

void GlRenderer::DrawQuad(const Vertex vertices[6], const Texture& texture, const Color& tint) {
    if (program_ == 0 || !texture.valid()) {
        return;
    }
    if (uniformTint_ >= 0) {
        // 预乘 alpha：颜色分量乘以 alpha
        glfn::Uniform4f(uniformTint_, tint.r * tint.a, tint.g * tint.a, tint.b * tint.a, tint.a);
    }
    glfn::BindBuffer(glc::kArrayBuffer, vbo_);
    glfn::BufferData(glc::kArrayBuffer, static_cast<GLsizeiptr>(sizeof(Vertex) * 6), vertices,
                     glc::kDynamicDraw);
    ::glBindTexture(GL_TEXTURE_2D, texture.id());
    ::glDrawArrays(GL_TRIANGLES, 0, 6);
    ::glBindTexture(GL_TEXTURE_2D, 0);
}

void GlRenderer::DrawTexture(const Texture& texture, const RECT& dest, float rotationDeg,
                            const Color& tint) {
    if (!texture.valid()) {
        return;
    }
    const float centerX = (static_cast<float>(dest.left) + static_cast<float>(dest.right)) * 0.5f;
    const float centerY = (static_cast<float>(dest.top) + static_cast<float>(dest.bottom)) * 0.5f;
    const float halfWidth = (static_cast<float>(dest.right) - static_cast<float>(dest.left)) * 0.5f;
    const float halfHeight = (static_cast<float>(dest.bottom) - static_cast<float>(dest.top)) * 0.5f;

    const float radians = rotationDeg * 3.14159265358979f / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);

    // 屏幕坐标系 y 轴向下，该矩阵在此坐标系下表现为顺时针旋转
    const auto transform = [&](float localX, float localY) -> Vertex {
        return Vertex{centerX + localX * cosine - localY * sine,
                      centerY + localX * sine + localY * cosine, 0.0f, 0.0f};
    };

    const Vertex topLeft = transform(-halfWidth, -halfHeight);
    const Vertex topRight = transform(halfWidth, -halfHeight);
    const Vertex bottomRight = transform(halfWidth, halfHeight);
    const Vertex bottomLeft = transform(-halfWidth, halfHeight);

    Vertex vertices[6];
    vertices[0] = topLeft;
    vertices[0].u = 0.0f;
    vertices[0].v = 0.0f;
    vertices[1] = topRight;
    vertices[1].u = 1.0f;
    vertices[1].v = 0.0f;
    vertices[2] = bottomRight;
    vertices[2].u = 1.0f;
    vertices[2].v = 1.0f;
    vertices[3] = topLeft;
    vertices[3].u = 0.0f;
    vertices[3].v = 0.0f;
    vertices[4] = bottomRight;
    vertices[4].u = 1.0f;
    vertices[4].v = 1.0f;
    vertices[5] = bottomLeft;
    vertices[5].u = 0.0f;
    vertices[5].v = 1.0f;

    DrawQuad(vertices, texture, tint);
}

void GlRenderer::DrawSolid(const RECT& dest, const Color& color, float rotationDeg) {
    DrawTexture(whiteTexture_, dest, rotationDeg, color);
}

} // namespace gfx
} // namespace vb
