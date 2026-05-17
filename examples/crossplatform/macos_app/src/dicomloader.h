#pragma once
#include <QObject>
#include <QImage>
#include <QQuickImageProvider>
#include <QString>
#include <QUrl>
#include <vector>
#include <cstdint>

/**
 * DICOM 压缩类型枚举
 */
enum class CompressionType {
    Uncompressed,
    RLE_Lossless,
    JPEG_Baseline,
    JPEG_Extended,
    JPEG_Lossless,
    JPEG_Lossless_SV1,
    JPEG_LS_Lossless,
    JPEG_LS_Lossy,
    JPEG2000_Lossless,
    JPEG2000_Lossy,
    Unknown
};

/**
 * DICOM 文件加载器 + ImageProvider
 *
 * 支持:
 *   - DICOM Part 10 文件格式解析
 *   - 显式/隐式 VR 传输语法
 *   - 小端/大端字节序
 *   - 无压缩像素数据 (Raw/Monochrome/RGB)
 *   - RLE 无损压缩解码
 *   - JPEG 基线压缩解码 (macOS ImageIO)
 *   - JPEG 2000 / JPEG-LS 占位符（返回错误）
 *   - 多帧 DICOM 浏览
 *   - image://dicom/current 供 QML Image 使用
 */
class DicomLoader : public QQuickImageProvider {
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath WRITE loadFile NOTIFY fileLoaded)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY fileLoaded)
    Q_PROPERTY(QString patientName READ patientName NOTIFY fileLoaded)
    Q_PROPERTY(QString patientId READ patientId NOTIFY fileLoaded)
    Q_PROPERTY(QString modality READ modality NOTIFY fileLoaded)
    Q_PROPERTY(QString studyDesc READ studyDesc NOTIFY fileLoaded)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY fileLoaded)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY fileLoaded)
    Q_PROPERTY(float windowCenter READ windowCenter NOTIFY fileLoaded)
    Q_PROPERTY(float windowWidth READ windowWidth NOTIFY fileLoaded)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY fileLoaded)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY frameChanged)

public:
    explicit DicomLoader();

    // QQuickImageProvider
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    QString filePath() const { return m_filePath; }
    bool hasImage() const { return !m_image.isNull(); }
    QString patientName() const { return m_patientName; }
    QString patientId() const { return m_patientId; }
    QString modality() const { return m_modality; }
    QString studyDesc() const { return m_studyDesc; }
    int imageWidth() const { return m_image.width(); }
    int imageHeight() const { return m_image.height(); }
    float windowCenter() const { return m_wlCenter; }
    float windowWidth() const { return m_wlWidth; }
    int frameCount() const { return m_frameCount; }
    int currentFrame() const { return m_currentFrame; }

    QImage image() const { return m_image; }

public slots:
    void loadFile(const QString &path);
    void loadUrl(const QUrl &url);
    void applyWindowLevel(float center, float width);
    void nextFrame();
    void prevFrame();
    void setFrame(int frame);

signals:
    void fileLoaded();
    void errorOccurred(const QString &message);
    void frameChanged();

private:
    struct DicomTag {
        uint16_t group;
        uint16_t element;
        uint32_t length;
        const uint8_t *data;
    };

    struct FrameInfo {
        const uint8_t *data;   // 指向帧数据（可能已压缩）
        uint32_t length;       // 帧数据长度
    };

    bool parseFile(const std::vector<uint8_t> &buffer);
    bool readTag(const uint8_t *&ptr, const uint8_t *end,
                 bool explicitVR, bool bigEndian, DicomTag &tag);
    void parseFrameData();
    void decodePixels(const DicomTag &pixelTag);
    bool decodeUncompressed(const uint8_t *data, uint32_t dataLen);
    bool decodeRLE(const uint8_t *data, uint32_t dataLen);
    bool decodeJPEG(const uint8_t *data, uint32_t dataLen);
    void applyRescaleSlope();
    void buildDisplayImage();

    QString readStringTag(uint16_t group, uint16_t element) const;
    uint16_t readUint16Tag(uint16_t group, uint16_t element) const;
    float readFloatTag(uint16_t group, uint16_t element) const;

    static CompressionType transferSyntaxToCompression(const QString &ts);

    QString m_filePath;
    QImage m_image;
    std::vector<uint8_t> m_rawData;      // 完整文件缓冲区
    std::vector<float> m_floatPixels;
    std::vector<DicomTag> m_tags;

    // 图像参数
    uint32_t m_rows = 0;
    uint32_t m_cols = 0;
    uint32_t m_bitsAllocated = 0;
    uint32_t m_bitsStored = 0;
    uint32_t m_highBit = 0;
    uint32_t m_pixelRep = 0;    // 0=unsigned, 1=signed
    uint32_t m_samplesPerPixel = 1;
    QString m_photoInterp;
    bool m_bigEndianTransfer = false;
    bool m_explicitVR = true;

    // 传输语法与压缩
    QString m_transferSyntax;
    CompressionType m_compression = CompressionType::Uncompressed;
    bool m_isEncapsulated = false;

    // 多帧相关
    int m_frameCount = 1;
    int m_currentFrame = 0;
    std::vector<FrameInfo> m_frameData;  // 每帧数据的指针和长度

    // 像素数据原始引用
    const uint8_t *m_pixelDataPtr = nullptr;
    uint32_t m_pixelDataLen = 0;

    // Rescale
    float m_rescaleSlope = 1.0f;
    float m_rescaleIntercept = 0.0f;

    // Window/Level
    float m_wlCenter = 0.0f;
    float m_wlWidth = 0.0f;

    // 元数据
    QString m_patientName;
    QString m_patientId;
    QString m_modality;
    QString m_studyDesc;
};
