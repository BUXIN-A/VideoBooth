#pragma once

#include <windows.h>

#include <GL/gl.h>

#include <cstdint>
#include <vector>

#include "render/GlContext.h"

namespace vb {
namespace gfx {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float red, float green, float blue, float alpha = 1.0f)
        : r(red), g(green), b(blue), a(alpha) {}

    // 无色偏（保持原纹理颜色）
    static constexpr Color White() { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
};

// 32 位预乘 BGRA 纹理，支持复用同一 GL 纹理对象反复上传
class Texture {
public:
    ~Texture();

    bool Upload(const uint8_t* bgra, int width, int height, int stride);
    // 局部更新（仅在尺寸一致时生效，否则退化为整图上传）
    bool UploadRegion(const uint8_t* bgra, int width, int height, int stride, const RECT& region);
    void Destroy();

    GLuint id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return id_ != 0 && width_ > 0 && height_ > 0; }

private:
    GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> packed_; // 行不连续时用于紧凑拷贝
};

// 基于着色器的纹理 / 纯色四边形渲染器（像素坐标，原点左上）
class GlRenderer {
public:
    ~GlRenderer();

    bool Init();
    void Shutdown();

    void BeginFrame(int width, int height, const Color& clearColor);
    void DrawTexture(const Texture& texture, const RECT& dest, float rotationDeg = 0.0f,
                     const Color& tint = Color::White());
    void DrawSolid(const RECT& dest, const Color& color, float rotationDeg = 0.0f);

private:
    struct Vertex {
        float x;
        float y;
        float u;
        float v;
    };

    void DrawQuad(const Vertex vertices[6], const Texture& texture, const Color& tint);

    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLint uniformViewport_ = -1;
    GLint uniformTexture_ = -1;
    GLint uniformTint_ = -1;
    GLint attribPosition_ = -1;
    GLint attribTexCoord_ = -1;
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    Texture whiteTexture_;
};

} // namespace gfx
} // namespace vb
