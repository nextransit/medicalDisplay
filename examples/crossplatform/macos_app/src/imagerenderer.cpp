#include "imagerenderer.h"
#include <QTimer>
#include <QDebug>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <future>

// Metal 后端外部声明（链接 metal_backend.mm）
extern "C" {
    void* metal_init(int width, int height);
    void  metal_render(void* ctx, const struct RenderParamsC* params, void* out_pixels);
    void  metal_destroy(void* ctx);
}

struct RenderParamsC {
    unsigned int width, height;
    float brightness, contrast, saturation;
    int enableGsdf, enableBloodless;
    float bloodSuppress, tissueEnhance;
    int modality;
};

ImageRenderer::ImageRenderer()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
    // 尝试初始化 Metal GPU
    m_metalCtx = metal_init(800, 700);
    m_hasGPU = (m_metalCtx != nullptr);

    if (m_hasGPU) {
        qDebug() << "[ImageRenderer] Metal GPU 加速已启用";
    } else {
        qDebug() << "[ImageRenderer] 使用 CPU 渲染（多线程）";
    }

    // 首帧延迟生成
    QTimer::singleShot(100, this, [this]() {
        markDirty();
    });
}

ImageRenderer::~ImageRenderer()
{
    if (m_metalCtx) {
        metal_destroy(m_metalCtx);
        m_metalCtx = nullptr;
    }
}

QImage ImageRenderer::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id)
    QMutexLocker lock(&m_mutex);

    if (m_image.isNull()) {
        int w = requestedSize.width() > 0 ? requestedSize.width() : 800;
        int h = requestedSize.height() > 0 ? requestedSize.height() : 700;
        QImage placeholder(w, h, QImage::Format_ARGB32);
        placeholder.fill(QColor(10, 14, 23));
        if (size) *size = placeholder.size();
        return placeholder;
    }

    if (size) *size = m_image.size();
    return m_image;
}

// ---- 属性 Setter ----

void ImageRenderer::setModality(int m)
{
    if (m_modality.exchange(m) != m) {
        emit modalityChanged();
        markDirty();
    }
}

void ImageRenderer::setBrightness(float v)
{
    if (std::abs(m_brightness.exchange(v) - v) > 0.001f) {
        emit brightnessChanged();
        markDirty();
    }
}

void ImageRenderer::setContrast(float v)
{
    if (std::abs(m_contrast.exchange(v) - v) > 0.001f) {
        emit contrastChanged();
        markDirty();
    }
}

void ImageRenderer::setSaturation(float v)
{
    if (std::abs(m_saturation.exchange(v) - v) > 0.001f) {
        emit saturationChanged();
        markDirty();
    }
}

void ImageRenderer::setGsdfEnabled(bool v)
{
    if (m_gsdfEnabled.exchange(v) != v) {
        emit gsdfEnabledChanged();
        markDirty();
    }
}

void ImageRenderer::setViewportWidth(int w)
{
    if (w > 0 && m_viewW.exchange(w) != w) {
        emit viewportSizeChanged();
    }
}

void ImageRenderer::setViewportHeight(int h)
{
    if (h > 0 && m_viewH.exchange(h) != h) {
        emit viewportSizeChanged();
    }
}

// ---- 渲染调度 ----

void ImageRenderer::markDirty()
{
    if (!m_dirty.exchange(true)) {
        // 16ms 去抖：合并连续参数更新
        QTimer::singleShot(16, this, &ImageRenderer::processRenderQueue);
    }
}

void ImageRenderer::processRenderQueue()
{
    if (!m_dirty.load()) return;

    // 防止并发渲染：如果已有渲染在进行中，等待下一次
    bool expected = false;
    if (!m_rendering.compare_exchange_strong(expected, true)) {
        // 重新标记 dirty，下一轮重试
        m_dirty.store(true);
        return;
    }

    int w = m_viewW.load();
    int h = m_viewH.load();

    // 如果视口尺寸改变，重建 Metal 上下文以匹配新尺寸
    if (m_hasGPU && (w != m_metalW || h != m_metalH)) {
        recreateMetalContext(w, h);
    }

    // 显示 loading 状态
    if (!m_loading.exchange(true)) {
        emit loadingChanged();
    }

    // 清理 dirty 标志（在派发渲染前，防止重复触发）
    m_dirty.store(false);

    // 异步生成图像
    QFuture<void> future = QtConcurrent::run([this, w, h]() {
        generateImage(w, h);
        m_rendering.store(false);
    });
    Q_UNUSED(future)
}

void ImageRenderer::generateImage(int width, int height)
{
    // 尝试 GPU 路径
    bool gpuOk = false;
    if (m_hasGPU) {
        gpuOk = generateOnGPU(width, height);
    }

    if (!gpuOk) {
        generateOnCPU(width, height);
    }

    m_dirty.store(false);
    m_loading.store(false);
    emit loadingChanged();
    emit imageUpdated();
}

// ---- GPU 渲染路径 (Metal) ----

bool ImageRenderer::generateOnGPU(int width, int height)
{
    if (!m_metalCtx) return false;

    // 使用实际视口尺寸（Metal 上下文应已匹配）
    RenderParamsC params;
    params.width = static_cast<unsigned int>(width);
    params.height = static_cast<unsigned int>(height);
    params.brightness = m_brightness.load();
    params.contrast = m_contrast.load();
    params.saturation = m_saturation.load();
    params.enableGsdf = m_gsdfEnabled.load() ? 1 : 0;
    params.enableBloodless = 0;
    params.bloodSuppress = 0.5f;
    params.tissueEnhance = 0.3f;
    params.modality = m_modality.load();

    // 分配输出缓冲
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);

    metal_render(m_metalCtx, &params, pixels.data());

    // Metal 输出是 BGRA8Unorm → 转为 QImage ARGB32
    QImage img(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        auto *src = pixels.data() + y * width * 4;
        auto *dst = img.scanLine(y);
        for (int x = 0; x < width; ++x) {
            int si = x * 4;
            int di = x * 4;
            dst[di + 2] = src[si + 0];   // R (from B)
            dst[di + 1] = src[si + 1];   // G
            dst[di + 0] = src[si + 2];   // B (from R)
            dst[di + 3] = 255;           // A
        }
    }

    // 注意：Metal 上下文已匹配视口尺寸，无需缩放
    {
        QMutexLocker lock(&m_mutex);
        m_image = img;
    }
    return true;
}

// ---- CPU 渲染路径（多线程加速） ----

void ImageRenderer::generateOnCPU(int width, int height)
{
    int m = m_modality.load();
    float b = m_brightness.load();
    float c = m_contrast.load();
    float s = m_saturation.load();
    bool gsdf = m_gsdfEnabled.load();

    QImage img(width, height, QImage::Format_ARGB32);
    const float cx = width * 0.5f;
    const float cy = height * 0.5f;
    const float maxR = std::min(width, height) * 0.42f;

    // 并行处理：每个线程处理 N 行
    const int numThreads = std::max(1, QThread::idealThreadCount());
    const int rowsPerThread = (height + numThreads - 1) / numThreads;

    std::vector<std::future<void>> futures;
    futures.reserve(numThreads);  // 预分配避免运行时重新分配

    for (int t = 0; t < numThreads; ++t) {
        int yStart = t * rowsPerThread;
        int yEnd = std::min(yStart + rowsPerThread, height);

        if (yStart >= height) break;

        futures.push_back(std::async(std::launch::async, [&, yStart, yEnd]() {
            for (int y = yStart; y < yEnd; ++y) {
                auto *line = reinterpret_cast<uint32_t*>(img.scanLine(y));
                for (int x = 0; x < width; ++x) {
                    float dx = (x - cx) / maxR;
                    float dy = (y - cy) / maxR;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    float angle = std::atan2(dy, dx);

                    float r = 0, g = 0, bVal = 0;

                    switch (m) {
                    case 0: { // CT
                        float skull = smoothstep(0.72f, 0.82f, dist) - smoothstep(1.02f, 1.08f, dist);
                        float brain = smoothstep(0.05f, 0.68f, dist) * (1.0f - smoothstep(0.68f, 0.78f, dist));
                        float ventricle = std::exp(-dist * dist * 8.0f) * 0.6f;
                        float tissue = std::sin(x / width * 30.0f) * std::cos(y / height * 40.0f) * 0.06f * (1.0f - skull);
                        r = g = bVal = clamp(skull * 0.92f + brain * 0.48f + ventricle * 0.22f + tissue, 0.0f, 1.0f);
                        break;
                    }
                    case 1: { // MRI
                        float gm = smoothstep(0.12f, 0.52f, dist) - smoothstep(0.58f, 0.68f, dist);
                        float wm = smoothstep(0.08f, 0.38f, dist) * (1.0f - smoothstep(0.38f, 0.48f, dist));
                        float csf = std::exp(-dist * dist * 12.0f) * 0.65f;
                        float folding = std::sin(angle * 10.0f + dist * 22.0f) * 0.05f;
                        r = g = bVal = clamp(gm * 0.52f + wm * 0.72f + csf * 0.2f + folding, 0.0f, 1.0f);
                        break;
                    }
                    case 2: { // X-Ray
                        float lungL = std::exp(-((dx + 0.25f) * (dx + 0.25f) * 6.0f + dy * dy * 5.0f));
                        float lungR = std::exp(-((dx - 0.25f) * (dx - 0.25f) * 6.0f + dy * dy * 5.0f));
                        float spine = std::exp(-dx * dx * 18.0f) * std::exp(-dy * 2.5f);
                        r = g = bVal = clamp(0.82f - lungL * 0.45f - lungR * 0.45f + spine * 0.18f, 0.0f, 1.0f);
                        break;
                    }
                    case 3: { // 超声
                        float sector = (std::abs(angle) < 0.65f) ? (1.0f - dist * 1.3f) : 0.0f;
                        float speckle = (std::sin(x * 0.08f + y * 0.095f) * std::sin(y * 0.073f - x * 0.067f) * 0.5f + 0.5f) * 0.2f;
                        float tissue = std::max(0.0f, sector) * (0.25f + speckle);
                        float vessel = std::exp(-((dx + 0.1f) * (dx + 0.1f) + (dy - 0.1f) * (dy - 0.1f)) * 25.0f) * 0.35f;
                        r = g = bVal = clamp(tissue + vessel + 0.05f, 0.0f, 1.0f);
                        break;
                    }
                    default:
                        r = g = bVal = 0.25f;
                        break;
                    }

                    // 亮度 + 对比度
                    if (b != 0.0f || c != 1.0f) {
                        r = (r - 0.5f) * c + 0.5f + b;
                        g = (g - 0.5f) * c + 0.5f + b;
                        bVal = (bVal - 0.5f) * c + 0.5f + b;
                    }

                    // 饱和度
                    if (s != 1.0f) {
                        float gray = 0.299f * r + 0.587f * g + 0.114f * bVal;
                        r = gray + (r - gray) * s;
                        g = gray + (g - gray) * s;
                        bVal = gray + (bVal - gray) * s;
                    }

                    // GSDF
                    if (gsdf) {
                        float lum = 0.299f * r + 0.587f * g + 0.114f * bVal;
                        float perceptual = lum < 0.5f
                            ? 2.0f * lum * lum
                            : 1.0f - std::pow(-2.0f * lum + 2.0f, 2.0f) / 2.0f;
                        float scale = perceptual / std::max(lum, 0.001f);
                        r *= scale; g *= scale; bVal *= scale;
                    }

                    int ir = static_cast<int>(clamp(r, 0.0f, 1.0f) * 255.0f);
                    int ig = static_cast<int>(clamp(g, 0.0f, 1.0f) * 255.0f);
                    int ib = static_cast<int>(clamp(bVal, 0.0f, 1.0f) * 255.0f);
                    line[x] = 0xFF000000 | (ir << 16) | (ig << 8) | ib;
                }
            }
        }));
    }

    // 等待所有线程完成
    for (auto &f : futures) {
        f.get();
    }

    {
        QMutexLocker lock(&m_mutex);
        m_image = img;
    }
}

// ---- Metal 上下文动态重建 ----

void ImageRenderer::recreateMetalContext(int w, int h)
{
    if (!m_hasGPU) return;

    // 销毁旧上下文
    if (m_metalCtx) {
        metal_destroy(m_metalCtx);
        m_metalCtx = nullptr;
    }

    // 使用新尺寸重建
    m_metalCtx = metal_init(w, h);
    m_hasGPU = (m_metalCtx != nullptr);

    if (m_hasGPU) {
        m_metalW = w;
        m_metalH = h;
        qDebug() << "[ImageRenderer] Metal 上下文已重建:" << w << "x" << h;
    } else {
        qWarning() << "[ImageRenderer] Metal 上下文重建失败，回退到 CPU 渲染";
        m_metalW = 0;
        m_metalH = 0;
    }
}

// ---- 工具函数 ----

float ImageRenderer::smoothstep(float edge0, float edge1, float x)
{
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float ImageRenderer::clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
