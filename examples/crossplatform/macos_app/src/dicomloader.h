#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <QUrl>
#include <vector>
#include <cstdint>

/**
 * DICOM 文件加载器
 *
 * 支持:
 *   - DICOM Part 10 文件格式解析
 *   - 显式/隐式 VR 传输语法
 *   - 小端/大端字节序
 *   - 无压缩像素数据 (Raw/Monochrome/RGB)
 *   - 窗口/窗位应用
 *   - 元数据提取 (Patient, Study, Modality 等)
 */
class DicomLoader : public QObject {
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

public:
    explicit DicomLoader(QObject *parent = nullptr);

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

    QImage image() const { return m_image; }

public slots:
    void loadFile(const QString &path);
    void loadUrl(const QUrl &url);
    void applyWindowLevel(float center, float width);

signals:
    void fileLoaded();
    void errorOccurred(const QString &message);

private:
    struct DicomTag {
        uint16_t group;
        uint16_t element;
        uint32_t length;
        const uint8_t *data;
    };

    bool parseFile(const std::vector<uint8_t> &buffer);
    bool readTag(const uint8_t *&ptr, const uint8_t *end,
                 bool explicitVR, bool bigEndian, DicomTag &tag);
    void decodePixels(const DicomTag &pixelTag);
    void applyRescaleSlope();
    void buildDisplayImage();

    QString readStringTag(uint16_t group, uint16_t element) const;
    uint16_t readUint16Tag(uint16_t group, uint16_t element) const;
    float readFloatTag(uint16_t group, uint16_t element) const;

    QString m_filePath;
    QImage m_image;
    std::vector<uint8_t> m_rawData;
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
