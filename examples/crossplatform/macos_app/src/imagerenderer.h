#pragma once
#include <QObject>
#include <QImage>
#include <QQuickImageProvider>
#include <QMutex>
#include <QThread>
#include <atomic>
#include <cmath>

/**
 * 医学影像生成器 — 多线程 + GPU 加速
 *
 * 架构:
 *   - 后台线程异步生成图像（不阻塞 UI）
 *   - macOS: Metal GPU Compute Shader 加速（可选）
 *   - 其他平台: CPU SIMD 优化的 C++ 渲染
 *   - 自适应视口分辨率
 *   - 16ms 参数去抖动合并连续更新
 */
class ImageRenderer : public QQuickImageProvider {
    Q_OBJECT
    Q_PROPERTY(int modality READ modality WRITE setModality NOTIFY modalityChanged)
    Q_PROPERTY(float brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(float contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(float saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(bool gsdfEnabled READ gsdfEnabled WRITE setGsdfEnabled NOTIFY gsdfEnabledChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int viewportWidth READ viewportWidth WRITE setViewportWidth NOTIFY viewportSizeChanged)
    Q_PROPERTY(int viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY viewportSizeChanged)

public:
    explicit ImageRenderer();
    ~ImageRenderer() override;

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    int modality() const { return m_modality; }
    float brightness() const { return m_brightness; }
    float contrast() const { return m_contrast; }
    float saturation() const { return m_saturation; }
    bool gsdfEnabled() const { return m_gsdfEnabled; }
    bool loading() const { return m_loading; }
    int viewportWidth() const { return m_viewW; }
    int viewportHeight() const { return m_viewH; }

    void setModality(int m);
    void setBrightness(float v);
    void setContrast(float v);
    void setSaturation(float v);
    void setGsdfEnabled(bool v);
    void setViewportWidth(int w);
    void setViewportHeight(int h);

signals:
    void modalityChanged();
    void brightnessChanged();
    void contrastChanged();
    void saturationChanged();
    void gsdfEnabledChanged();
    void loadingChanged();
    void viewportSizeChanged();
    void imageUpdated();

private slots:
    void processRenderQueue();

private:
    void markDirty();
    void generateImage(int width, int height);

    // 图像生成核心算法
    void generateOnCPU(int width, int height);
    bool generateOnGPU(int width, int height);  // Metal 路径
    void recreateMetalContext(int w, int h);    // 动态重建 Metal 纹理

    static float smoothstep(float edge0, float edge1, float x);
    static float clamp(float v, float lo, float hi);

    // 参数 (原子操作，线程安全)
    std::atomic<int> m_modality{0};
    std::atomic<float> m_brightness{0.0f};
    std::atomic<float> m_contrast{1.0f};
    std::atomic<float> m_saturation{1.0f};
    std::atomic<bool> m_gsdfEnabled{true};
    std::atomic<bool> m_dirty{true};
    std::atomic<bool> m_rendering{false};  // 防止并发渲染
    std::atomic<bool> m_loading{false};
    std::atomic<int> m_viewW{800};
    std::atomic<int> m_viewH{700};

    // 渲染结果
    QImage m_image;
    QMutex m_mutex;

    // Metal GPU 上下文
    void *m_metalCtx = nullptr;
    bool m_hasGPU = false;
    int m_metalW = 800;
    int m_metalH = 700;
};
