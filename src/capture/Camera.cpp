#include "capture/Camera.h"

#include "util/Log.h"
#include "util/Strings.h"

#include <mferror.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strmif.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>

namespace vb {
namespace capture {
namespace {

using Microsoft::WRL::ComPtr;

constexpr DWORD kDeviceLostThreshold = 5; // 连续读取失败次数达到后判定设备丢失
constexpr int64_t kOpenTimeoutMs = 8000;  // 打开设备等待上限
constexpr int64_t kThreadStopGraceMs = 300;     // 采集线程自行退出宽限时间
constexpr int64_t kThreadStopTimeoutMs = 4000;  // 采集线程退出等待上限
// 第一路视频流（MF_SOURCE_READER_FIRST_VIDEO_STREAM 为有符号枚举常量，此处显式转换）
constexpr DWORD kFirstVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool CollectCameraActivates(std::vector<ComPtr<IMFActivate>>& activates,
                            std::vector<CameraInfo>* infos) {
    activates.clear();
    if (infos != nullptr) {
        infos->clear();
    }

    ComPtr<IMFAttributes> attributes;
    if (FAILED(::MFCreateAttributes(attributes.GetAddressOf(), 1))) {
        return false;
    }
    if (FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
        return false;
    }

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    const HRESULT hr = ::MFEnumDeviceSources(attributes.Get(), &devices, &count);
    if (FAILED(hr)) {
        VB_WARN("枚举摄像头设备失败: %ls", HresultToWide(hr).c_str());
        return false;
    }

    for (UINT32 i = 0; i < count; ++i) {
        if (devices[i] == nullptr) {
            continue;
        }
        ComPtr<IMFActivate> activate;
        activate.Attach(devices[i]);

        CameraInfo info;
        WCHAR* buffer = nullptr;
        UINT32 length = 0;
        if (SUCCEEDED(activate->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &buffer, &length))) {
            info.id.assign(buffer, length);
            ::CoTaskMemFree(buffer);
            buffer = nullptr;
        }
        if (SUCCEEDED(activate->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                                   &buffer, &length))) {
            info.name.assign(buffer, length);
            ::CoTaskMemFree(buffer);
            buffer = nullptr;
        }
        if (info.id.empty()) {
            continue;
        }
        if (info.name.empty()) {
            info.name = info.id;
        }
        activates.push_back(std::move(activate));
        if (infos != nullptr) {
            infos->push_back(std::move(info));
        }
    }

    if (devices != nullptr) {
        ::CoTaskMemFree(devices);
    }
    return true;
}

// 自动曝光设置：设备支持时生效，失败不影响采集
void ApplyExposure(IMFMediaSource* source, bool autoExposure) {
    if (source == nullptr) {
        return;
    }
    ComPtr<IAMCameraControl> control;
    if (FAILED(source->QueryInterface(IID_PPV_ARGS(control.GetAddressOf())))) {
        VB_INFO("设备未暴露 IAMCameraControl，跳过曝光设置");
        return;
    }
    long value = 0;
    long flags = 0;
    if (FAILED(control->Get(CameraControl_Exposure, &value, &flags))) {
        VB_INFO("设备不支持曝光控制，跳过曝光设置");
        return;
    }
    const long targetFlags =
        autoExposure ? CameraControl_Flags_Auto : CameraControl_Flags_Manual;
    const HRESULT hr = control->Set(CameraControl_Exposure, value, targetFlags);
    if (FAILED(hr)) {
        VB_WARN("设置曝光模式失败: %ls", HresultToWide(hr).c_str());
    } else {
        VB_INFO("曝光模式已设置为%s", autoExposure ? "自动" : "手动");
    }
}

ComPtr<IMFMediaType> MakeRgb32Type(int width, int height, int fps) {
    ComPtr<IMFMediaType> type;
    if (FAILED(::MFCreateMediaType(type.GetAddressOf()))) {
        return nullptr;
    }
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    ::MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(width),
                         static_cast<UINT32>(height));
    ::MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1);
    ::MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    // 正步长表示自上而下布局
    type->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width) * 4u);
    return type;
}

} // namespace

std::vector<CameraInfo> EnumerateCameras() {
    std::vector<CameraInfo> infos;
    std::vector<ComPtr<IMFActivate>> activates;
    CollectCameraActivates(activates, &infos);
    return infos;
}

CameraCapture::~CameraCapture() {
    Close();
}

bool CameraCapture::Open(const std::wstring& deviceId, int width, int height, int fps,
                         bool autoExposure) {
    Close();

    if (threadBlocked_) {
        // 上一次采集线程卡在设备驱动调用中，重复启动会破坏状态，直接放弃
        VB_ERROR("摄像头驱动无响应，需重新启动程序后再试");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(openMutex_);
        openResult_ = OpenResult::Pending;
        request_.deviceId = deviceId;
        request_.width = width > 0 ? width : 1280;
        request_.height = height > 0 ? height : 720;
        request_.fps = fps > 0 ? fps : 30;
        request_.autoExposure = autoExposure;
    }

    deviceId_ = deviceId;
    deviceName_.clear();
    fps_.store(fps > 0 ? fps : 30);
    active_.store(true);
    running_.store(true);
    threadFinished_.store(false);
    thread_ = std::thread(&CameraCapture::ThreadMain, this);

    const int64_t deadline = NowMs() + kOpenTimeoutMs;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(openMutex_);
            if (openResult_ == OpenResult::Ok) {
                open_.store(true);
                return true;
            }
            if (openResult_ == OpenResult::Failed) {
                break;
            }
        }
        if (NowMs() >= deadline) {
            VB_WARN("打开摄像头超时");
            break;
        }
        ::Sleep(10);
    }

    Close();
    return false;
}

void CameraCapture::Close() {
    running_.store(false);
    if (thread_.joinable()) {
        // 给采集线程一个自行退出的机会
        const int64_t graceDeadline = NowMs() + kThreadStopGraceMs;
        while (!threadFinished_.load() && NowMs() < graceDeadline) {
            ::Sleep(5);
        }
        if (!threadFinished_.load()) {
            // 采集线程通常阻塞在 ReadSample 上，关闭媒体源可让读取立刻返回
            IMFMediaSource* source = nullptr;
            {
                std::lock_guard<std::mutex> lock(sourceMutex_);
                if (source_ != nullptr) {
                    source = source_;
                    source->AddRef();
                }
            }
            if (source != nullptr) {
                VB_WARN("采集线程未退出，关闭媒体源以取消阻塞的读取");
                source->Shutdown();
                source->Release();
            }
        }

        // 限时等待线程退出，避免卡住界面线程
        const int64_t deadline = NowMs() + kThreadStopTimeoutMs;
        while (!threadFinished_.load() && NowMs() < deadline) {
            ::Sleep(5);
        }
        if (threadFinished_.load()) {
            thread_.join();
        } else {
            VB_WARN("采集线程未能在超时前退出，已分离该线程");
            thread_.detach();
            threadBlocked_ = true;
        }
    }
    open_.store(false);

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latest_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(openMutex_);
        openResult_ = OpenResult::Pending;
    }
}

FramePtr CameraCapture::TakeLatest() {
    std::lock_guard<std::mutex> lock(frameMutex_);
    FramePtr frame = std::move(latest_);
    latest_.reset();
    return frame;
}

void CameraCapture::ThreadMain() {
    // 采集线程独立初始化 COM：SourceReader 的创建与读取均在本线程完成，
    // 避免跨线程单元（STA/MTA）问题
    const HRESULT comHr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    OpenRequest request;
    {
        std::lock_guard<std::mutex> lock(openMutex_);
        request = request_;
    }

    const bool opened = OpenOnCaptureThread(request);
    {
        std::lock_guard<std::mutex> lock(openMutex_);
        openResult_ = opened ? OpenResult::Ok : OpenResult::Failed;
    }

    if (opened) {
        CaptureLoop();
    }

    if (reader_ != nullptr) {
        reader_->Release();
        reader_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(sourceMutex_);
        if (source_ != nullptr) {
            source_->Shutdown();
            source_->Release();
            source_ = nullptr;
        }
    }
    open_.store(false);
    threadFinished_.store(true);
    if (SUCCEEDED(comHr)) {
        ::CoUninitialize();
    }
}

bool CameraCapture::OpenOnCaptureThread(const OpenRequest& request) {
    std::vector<ComPtr<IMFActivate>> activates;
    std::vector<CameraInfo> infos;
    if (!CollectCameraActivates(activates, &infos) || activates.empty()) {
        VB_WARN("未检测到可用的摄像头设备");
        return false;
    }

    size_t selected = 0;
    bool matched = false;
    if (!request.deviceId.empty()) {
        for (size_t i = 0; i < infos.size(); ++i) {
            if (EqualsIgnoreCase(infos[i].id, request.deviceId)) {
                selected = i;
                matched = true;
                break;
            }
        }
    }
    if (!matched) {
        VB_WARN("默认摄像头不可用，改用设备列表中的第 1 个设备: %ls",
                infos[0].name.c_str());
        selected = 0;
        if (!request.deviceId.empty()) {
            deviceId_ = infos[0].id;
        }
    }
    deviceName_ = infos[selected].name;

    IMFMediaSource* activated = nullptr;
    HRESULT hr = activates[selected]->ActivateObject(IID_PPV_ARGS(&activated));
    if (FAILED(hr)) {
        VB_ERROR("激活摄像头设备失败: %ls, %ls", deviceName_.c_str(),
                 HresultToWide(hr).c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(sourceMutex_);
        source_ = activated;
    }

    ComPtr<IMFAttributes> attributes;
    hr = ::MFCreateAttributes(attributes.GetAddressOf(), 3);
    if (FAILED(hr)) {
        return false;
    }
    // 由 SourceReader 内置视频处理器完成 MJPEG 解码、缩放与色彩转换
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attributes->SetUINT32(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE);

    hr = ::MFCreateSourceReaderFromMediaSource(source_, attributes.Get(), &reader_);
    if (FAILED(hr)) {
        VB_ERROR("创建 SourceReader 失败: %ls", HresultToWide(hr).c_str());
        return false;
    }

    ComPtr<IMFMediaType> type = MakeRgb32Type(request.width, request.height, request.fps);
    hr = reader_->SetCurrentMediaType(kFirstVideoStream, nullptr,
                                      type.Get());
    if (FAILED(hr)) {
        // 目标分辨率不被支持时回退到设备原生分辨率
        ComPtr<IMFMediaType> native;
        if (SUCCEEDED(reader_->GetNativeMediaType(kFirstVideoStream, 0,
                                                 native.GetAddressOf()))) {
            UINT32 nativeWidth = 0;
            UINT32 nativeHeight = 0;
            if (SUCCEEDED(::MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &nativeWidth,
                                              &nativeHeight))) {
                VB_WARN("采集分辨率 %dx%d 不被支持，回退到 %ux%u", request.width,
                        request.height, nativeWidth, nativeHeight);
                type = MakeRgb32Type(static_cast<int>(nativeWidth),
                                     static_cast<int>(nativeHeight), request.fps);
                hr = reader_->SetCurrentMediaType(kFirstVideoStream,
                                                  nullptr, type.Get());
            }
        }
    }
    if (FAILED(hr)) {
        VB_ERROR("设置采集输出格式失败: %ls", HresultToWide(hr).c_str());
        return false;
    }

    ApplyExposure(source_, request.autoExposure);

    // 记录实际协商到的采集分辨率，供采集循环打包帧时使用
    UINT32 actualWidth = 0;
    UINT32 actualHeight = 0;
    {
        ComPtr<IMFMediaType> current;
        if (SUCCEEDED(reader_->GetCurrentMediaType(kFirstVideoStream, current.GetAddressOf()))) {
            ::MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &actualWidth, &actualHeight);
        }
    }
    frameWidth_ = actualWidth > 0 ? static_cast<int>(actualWidth) : request.width;
    frameHeight_ = actualHeight > 0 ? static_cast<int>(actualHeight) : request.height;
    VB_INFO("摄像头已打开: %ls (%ux%u@%d, 请求 %dx%d)", deviceName_.c_str(), actualWidth,
            actualHeight, request.fps, request.width, request.height);
    return true;
}

void CameraCapture::CaptureLoop() {
    const int fps = fps_.load() > 0 ? fps_.load() : 30;
    const int64_t frameIntervalMs = 1000 / fps;
    int64_t lastEmitMs = 0;
    DWORD failureCount = 0;

    while (running_.load()) {
        if (!active_.load()) {
            ::Sleep(5);
            continue;
        }

        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;
        const HRESULT hr = reader_->ReadSample(kFirstVideoStream,
                                              0, &streamIndex, &flags, &timestamp, &sample);
        if (FAILED(hr)) {
            if (sample != nullptr) {
                sample->Release();
                sample = nullptr;
            }
            ++failureCount;
            VB_WARN("读取视频帧失败(%u): %ls", failureCount, HresultToWide(hr).c_str());
            if (failureCount >= kDeviceLostThreshold) {
                VB_WARN("摄像头设备已丢失，停止采集");
                break;
            }
            ::Sleep(200);
            continue;
        }
        failureCount = 0;

        if (sample == nullptr) {
            // 流结束或暂时无数据
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
                VB_WARN("采集流已结束");
                break;
            }
            continue;
        }

        if ((flags & MF_SOURCE_READERF_STREAMTICK) != 0) {
            sample->Release();
            continue;
        }

        // 按配置刷新率丢弃多余帧
        const int64_t now = NowMs();
        if (frameIntervalMs > 0 && now - lastEmitMs < frameIntervalMs - 1) {
            sample->Release();
            continue;
        }
        lastEmitMs = now;

        ComPtr<IMFMediaBuffer> buffer;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) {
            // 帧尺寸取自打开时协商的媒体类型，Lock2D 的步长仅用于逐行寻址
            const int frameWidth = frameWidth_;
            const int frameHeight = frameHeight_;

            bool copied = false;
            if (frameWidth > 0 && frameHeight > 0) {
                ComPtr<IMF2DBuffer> buffer2d;
                if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(buffer2d.GetAddressOf())))) {
                    BYTE* scan0 = nullptr;
                    LONG pitch = 0;
                    if (SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch))) {
                        // Lock2D 返回指向图像首行（顶行）的指针，步长为负表示自下而上，
                        // 按行寻址可自动兼容两种布局
                        Publish(scan0, static_cast<ptrdiff_t>(pitch), frameWidth, frameHeight);
                        copied = true;
                        buffer2d->Unlock2D();
                    }
                }
                if (!copied) {
                    BYTE* data = nullptr;
                    DWORD length = 0;
                    if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)) &&
                        length >= static_cast<DWORD>(frameWidth) * 4u *
                                      static_cast<DWORD>(frameHeight)) {
                        Publish(data, static_cast<ptrdiff_t>(frameWidth) * 4, frameWidth,
                                frameHeight);
                    }
                    if (data != nullptr) {
                        buffer->Unlock();
                    }
                }
            }
        }
        sample->Release();
    }
}

void CameraCapture::Publish(const uint8_t* scan0, ptrdiff_t pitch, int width, int height) {
    if (scan0 == nullptr || width <= 0 || height <= 0) {
        return;
    }
    auto frame = std::make_shared<Frame>();
    frame->width = width;
    frame->height = height;
    frame->stride = width * 4;
    frame->index = ++frameIndex_;
    frame->timestampMs = NowMs();
    frame->pixels.resize(static_cast<size_t>(frame->stride) * static_cast<size_t>(height));

    for (int y = 0; y < height; ++y) {
        const uint8_t* src = scan0 + static_cast<ptrdiff_t>(y) * pitch;
        uint8_t* dst = frame->pixels.data() + static_cast<size_t>(y) * frame->stride;
        std::memcpy(dst, src, static_cast<size_t>(width) * 4u);
        // MFVideoFormat_RGB32 的 X 通道未定义，强制为不透明（BGRA 小端下即最高字节）
        uint32_t* pixels = reinterpret_cast<uint32_t*>(dst);
        for (int x = 0; x < width; ++x) {
            pixels[x] |= 0xFF000000u;
        }
    }

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latest_ = frame;
    }
}

} // namespace capture
} // namespace vb
