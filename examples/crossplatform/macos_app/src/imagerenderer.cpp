#include "imagerenderer.h"
#include <QTimer>
#include <QDebug>
#include <algorithm>

ImageRenderer::ImageRenderer()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
    // 延迟初始化：首帧在事件循环启动后生成
    QTimer::singleShot(100, this, [this]() {
        generateImage(800, 700);
    });
}

QImage ImageRenderer::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id)
    QMutexLocker lock(&m_mutex);

    if (m_image.isNull()) {
        // 返回占位图
        QImage placeholder(requestedSize.width() > 0 ? requestedSize.width() : 800,
                          requestedSize.height() > 0 ? requestedSize.height() : 700,
                          QImage::Format_ARGB32);
        placeholder.fill(QColor(10, 14, 23));
        if (size) *size = placeholder.size();
        return placeholder;
    }

    if (size) *size = m_image.size();
    return m_image;
}

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

void ImageRenderer::markDirty()
{
    if (!m_dirty.exchange(true)) {
        emit dirtyChanged();
        // 延迟生成，合并连续更新
        QTimer::singleShot(16, this, [this]() {
            generateImage(800, 700);
        });
    }
}

// ---- 图像生成 ----

void ImageRenderer::generateImage(int width, int height)
{
    int m = m_modality.load();
    float b = m_brightness.load();
    float c = m_contrast.load();
    float s = m_saturation.load();
    bool gsdf = m_gsdfEnabled.load();

    QImage img(width, height, QImage::Format_ARGB32);
    uchar *pixels = img.bits();
    int stride = img.bytesPerLine();

    float cx = width * 0.5f;
    float cy = height * 0.5f;
    float maxR = std::min(width, height) * 0.42f;

    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<uint32_t*>(pixels + y * stride);
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
                float ctVal = skull * 0.92f + brain * 0.48f + ventricle * 0.22f + tissue;
                r = g = bVal = clamp(ctVal, 0.0f, 1.0f);
                break;
            }
            case 1: { // MRI
                float gm = smoothstep(0.12f, 0.52f, dist) - smoothstep(0.58f, 0.68f, dist);
                float wm = smoothstep(0.08f, 0.38f, dist) * (1.0f - smoothstep(0.38f, 0.48f, dist));
                float csf = std::exp(-dist * dist * 12.0f) * 0.65f;
                float folding = std::sin(angle * 10.0f + dist * 22.0f) * 0.05f;
                float mriVal = gm * 0.52f + wm * 0.72f + csf * 0.2f + folding;
                r = g = bVal = clamp(mriVal, 0.0f, 1.0f);
                break;
            }
            case 2: { // X-Ray
                float lungL = std::exp(-((dx + 0.25f) * (dx + 0.25f) * 6.0f + dy * dy * 5.0f));
                float lungR = std::exp(-((dx - 0.25f) * (dx - 0.25f) * 6.0f + dy * dy * 5.0f));
                float spine = std::exp(-dx * dx * 18.0f) * std::exp(-dy * 2.5f);
                float xrVal = 0.82f - lungL * 0.45f - lungR * 0.45f + spine * 0.18f;
                r = g = bVal = clamp(xrVal, 0.0f, 1.0f);
                break;
            }
            case 3: { // 超声
                float sector = (std::abs(angle) < 0.65f) ? (1.0f - dist * 1.3f) : 0.0f;
                float speckle = (std::sin(x * 0.08f + y * 0.095f) * std::sin(y * 0.073f - x * 0.067f) * 0.5f + 0.5f) * 0.2f;
                float tissue = std::max(0.0f, sector) * (0.25f + speckle);
                float vessel = std::exp(-((dx + 0.1f) * (dx + 0.1f) + (dy - 0.1f) * (dy - 0.1f)) * 25.0f) * 0.35f;
                float usVal = tissue + vessel + 0.05f;
                r = g = bVal = clamp(usVal, 0.0f, 1.0f);
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

    {
        QMutexLocker lock(&m_mutex);
        m_image = img;
    }

    m_dirty.store(false);
    emit imageUpdated();
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

