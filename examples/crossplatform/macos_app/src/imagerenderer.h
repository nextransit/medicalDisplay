#pragma once
#include <QObject>
#include <QImage>
#include <QQuickImageProvider>
#include <QMutex>
#include <atomic>
#include <cmath>

/**
 * 医学影像生成器 — C++ 高性能渲染
 *
 * 在后台线程生成医学测试图案，通过 QQuickImageProvider 传给 QML 显示。
 * 避免 QML Canvas JavaScript 在主线程阻塞导致的 UI 卡死。
 */
class ImageRenderer : public QQuickImageProvider {
    Q_OBJECT
    Q_PROPERTY(int modality READ modality WRITE setModality NOTIFY modalityChanged)
    Q_PROPERTY(float brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(float contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(float saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(bool gsdfEnabled READ gsdfEnabled WRITE setGsdfEnabled NOTIFY gsdfEnabledChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    explicit ImageRenderer();

    // QQuickImageProvider
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // 属性访问器
    int modality() const { return m_modality; }
    float brightness() const { return m_brightness; }
    float contrast() const { return m_contrast; }
    float saturation() const { return m_saturation; }
    bool gsdfEnabled() const { return m_gsdfEnabled; }
    bool dirty() const { return m_dirty; }

    void setModality(int m);
    void setBrightness(float v);
    void setContrast(float v);
    void setSaturation(float v);
    void setGsdfEnabled(bool v);

signals:
    void modalityChanged();
    void brightnessChanged();
    void contrastChanged();
    void saturationChanged();
    void gsdfEnabledChanged();
    void dirtyChanged();
    void imageUpdated();

private:
    void markDirty();
    void generateImage(int width, int height);

    static float smoothstep(float edge0, float edge1, float x);
    static float clamp(float v, float lo, float hi);
    static void applyProcessing(uchar *pixels, int len, float brightness, float contrast, float saturation, bool gsdf);

    std::atomic<int> m_modality{0};
    std::atomic<float> m_brightness{0.0f};
    std::atomic<float> m_contrast{1.0f};
    std::atomic<float> m_saturation{1.0f};
    std::atomic<bool> m_gsdfEnabled{true};
    std::atomic<bool> m_dirty{true};

    QImage m_image;
    QMutex m_mutex;
};
