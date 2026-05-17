#include "dicomloader.h"
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <cstring>

DicomLoader::DicomLoader()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage DicomLoader::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(id)
    if (m_image.isNull()) {
        if (size) *size = QSize(1, 1);
        QImage placeholder(1, 1, QImage::Format_ARGB32);
        placeholder.fill(Qt::transparent);
        return placeholder;
    }
    if (size) *size = m_image.size();
    return m_image;
}

void DicomLoader::loadFile(const QString &path)
{
    m_filePath = path;
    m_image = QImage();
    m_tags.clear();
    m_rawData.clear();
    m_floatPixels.clear();
    m_patientName.clear();
    m_patientId.clear();
    m_modality.clear();
    m_studyDesc.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("无法打开文件: %1").arg(path));
        return;
    }

    QByteArray raw = file.readAll();
    file.close();

    if (raw.size() < 132) {
        emit errorOccurred(QStringLiteral("文件太小，不是有效的 DICOM 文件"));
        return;
    }

    // 检查 DICOM 魔数 (跳过 128 字节 preamble)
    if (std::memcmp(raw.data() + 128, "DICM", 4) != 0) {
        emit errorOccurred(QStringLiteral("无效的 DICOM 文件（缺少 DICM 魔数）"));
        return;
    }

    std::vector<uint8_t> buffer(raw.size());
    std::memcpy(buffer.data(), raw.data(), raw.size());

    if (!parseFile(buffer)) {
        emit errorOccurred(QStringLiteral("DICOM 解析失败"));
        return;
    }

    // 检查是否成功提取了像素
    if (m_image.isNull()) {
        emit errorOccurred(QStringLiteral("无法提取像素数据"));
        return;
    }

    qDebug() << "[DICOM] 加载成功:" << m_modality << m_patientName
             << m_cols << "x" << m_rows;
    emit fileLoaded();
}

void DicomLoader::loadUrl(const QUrl &url)
{
    loadFile(url.toLocalFile());
}

bool DicomLoader::parseFile(const std::vector<uint8_t> &buffer)
{
    const uint8_t *meta = buffer.data() + 132;  // 跳过 preamble + "DICM"

    // 读取 (0002,0010) Transfer Syntax UID
    m_explicitVR = true;
    m_bigEndianTransfer = false;

    // 先读取 File Meta Information (Group 0x0002) 获取 Transfer Syntax
    const uint8_t *ptr = meta;
    const uint8_t *end = buffer.data() + buffer.size();

    // 第一遍：读取 transfer syntax 和其他元数据
    while (ptr + 4 <= end) {
        uint16_t group = (ptr[0] << 8) | ptr[1];  // Meta 信息通常是大端
        uint16_t elem  = (ptr[2] << 8) | ptr[3];

        if (group == 0x0002 && elem == 0x0010) {
            // Transfer Syntax UID
            // 先用显式 VR 读取 (0002 组总是显式 VR)
            if (ptr + 8 > end) break;
            char vr[3] = {(char)ptr[4], (char)ptr[5], 0};
            uint16_t length;
            const uint8_t *valPtr;

            if (vr[0] == 'O' && (vr[1] == 'B' || vr[1] == 'F' || vr[1] == 'D')) {
                // OB/OF/OD: 保留 2 字节，然后 4 字节长度
                if (ptr + 12 > end) break;
                ptr += 2;
                length = (ptr[6] << 24) | (ptr[7] << 16) | (ptr[8] << 8) | ptr[9];
                valPtr = ptr + 4;
                ptr = valPtr + length;
            } else {
                length = (ptr[6] << 8) | ptr[7];
                valPtr = ptr + 8;
                ptr = valPtr + length;
            }

            // 检查 transfer syntax
            QString ts = QString::fromLatin1((const char*)valPtr, length);
            if (ts.contains("1.2.840.10008.1.2.2")) {
                m_explicitVR = true; m_bigEndianTransfer = true;
            } else if (ts.contains("1.2.840.10008.1.2")) {
                m_explicitVR = false; m_bigEndianTransfer = false;
            }
            break;
        }

        // 跳过此标签
        if (ptr + 6 > end) break;
        uint16_t length = (ptr[4] << 8) | ptr[5];
        ptr += 4 + length + 2;  // group/elem + length + value (VR 在 0002 组是 2 字节)
    }

    // 第二遍：从 meta 信息之后开始读取所有标签
    ptr = meta;
    // 先跳到 dataset 开始 (跳过 0002 组)
    while (ptr + 4 <= end) {
        uint16_t group = (ptr[0] << 8) | ptr[1];
        if (group != 0x0002) break;

        if (ptr + 6 > end) break;
        uint16_t length = (ptr[4] << 8) | ptr[5];
        ptr += 4 + length + 2;
    }

    // 读取所有 data set 标签
    while (ptr + 4 <= end) {
        DicomTag tag;
        if (!readTag(ptr, end, m_explicitVR, m_bigEndianTransfer, tag)) break;
        m_tags.push_back(tag);
    }

    // 提取关键图像参数
    m_rows = readUint16Tag(0x0028, 0x0010);
    m_cols = readUint16Tag(0x0028, 0x0011);
    m_bitsAllocated = readUint16Tag(0x0028, 0x0100);
    m_bitsStored = readUint16Tag(0x0028, 0x0101);
    m_highBit = readUint16Tag(0x0028, 0x0102);
    m_pixelRep = readUint16Tag(0x0028, 0x0103);
    m_samplesPerPixel = readUint16Tag(0x0028, 0x0002);
    if (m_samplesPerPixel == 0) m_samplesPerPixel = 1;
    m_photoInterp = readStringTag(0x0028, 0x0004);
    m_rescaleSlope = readFloatTag(0x0028, 0x1053);
    if (m_rescaleSlope == 0.0f) m_rescaleSlope = 1.0f;
    m_rescaleIntercept = readFloatTag(0x0028, 0x1052);

    // Window/Level
    m_wlCenter = readFloatTag(0x0028, 0x1050);
    m_wlWidth  = readFloatTag(0x0028, 0x1051);

    // 元数据
    m_modality = readStringTag(0x0008, 0x0060);
    m_patientName = readStringTag(0x0010, 0x0010);
    m_patientId = readStringTag(0x0010, 0x0020);
    m_studyDesc = readStringTag(0x0008, 0x1030);

    if (m_rows == 0 || m_cols == 0) {
        qWarning() << "[DICOM] 未找到图像尺寸标签";
        return false;
    }

    // 解码像素数据
    for (auto &t : m_tags) {
        if ((t.group == 0x7FE0 && t.element == 0x0010) && t.length > 0) {
            decodePixels(t);
            break;
        }
    }

    return !m_image.isNull();
}

bool DicomLoader::readTag(const uint8_t *&ptr, const uint8_t *end,
                          bool explicitVR, bool bigEndian, DicomTag &tag)
{
    if (ptr + 4 > end) return false;

    auto read16 = [bigEndian](const uint8_t *p) -> uint16_t {
        return bigEndian ? (p[0] << 8) | p[1] : (p[1] << 8) | p[0];
    };
    auto read32 = [bigEndian](const uint8_t *p) -> uint32_t {
        return bigEndian
            ? (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]
            : (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
    };

    tag.group = read16(ptr);
    tag.element = read16(ptr + 2);
    ptr += 4;

    if (tag.group == 0xFFFE) {
        // Sequence delimiters
        tag.length = 0;
        tag.data = nullptr;
        return true;
    }

    if (explicitVR && ptr + 2 <= end) {
        char vr[3] = {(char)ptr[0], (char)ptr[1], 0};

        if (vr[0] == 'O' && (vr[1] == 'B' || vr[1] == 'F' || vr[1] == 'D' || vr[1] == 'W')) {
            // OB/OF/OD/OW: reserved 2 bytes + 4-byte length
            if (ptr + 4 > end) return false;
            ptr += 2; // skip reserved
            tag.length = read32(ptr);
            ptr += 4;
        } else if (vr[0] == 'S' && vr[1] == 'Q') {
            // Sequence: 2 reserved + 4-byte length
            if (ptr + 4 > end) return false;
            ptr += 2;
            tag.length = read32(ptr);
            ptr += 4;
        } else {
            ptr += 2;
            if (ptr + 2 > end) return false;
            tag.length = read16(ptr);
            ptr += 2;
        }
    } else {
        // Implicit VR: 4-byte length
        if (ptr + 4 > end) return false;
        tag.length = read32(ptr);
        ptr += 4;
    }

    if (tag.length == 0xFFFFFFFF) {
        tag.length = 0;
        tag.data = nullptr;
    } else if (ptr + tag.length <= end) {
        tag.data = ptr;
        ptr += tag.length;
    } else {
        tag.length = 0;
        tag.data = nullptr;
    }

    return true;
}

void DicomLoader::decodePixels(const DicomTag &pixelTag)
{
    uint32_t pixelCount = m_rows * m_cols;
    if (pixelCount == 0 || pixelTag.data == nullptr) return;

    m_floatPixels.resize(pixelCount * m_samplesPerPixel, 0.0f);

    if (m_bitsAllocated <= 8) {
        const uint8_t *src = pixelTag.data;
        for (uint32_t i = 0; i < pixelCount * m_samplesPerPixel && src < pixelTag.data + pixelTag.length; ++i, ++src) {
            float val = static_cast<float>(*src);
            if (m_pixelRep && (val >= (1u << (m_bitsStored - 1)))) {
                val -= static_cast<float>(1u << m_bitsStored);  // 有符号
            }
            m_floatPixels[i] = val;
        }
    } else if (m_bitsAllocated == 16) {
        const uint16_t *src = reinterpret_cast<const uint16_t*>(pixelTag.data);
        size_t maxSrc = pixelTag.length / 2;
        for (uint32_t i = 0; i < pixelCount * m_samplesPerPixel && i < maxSrc; ++i) {
            float val = static_cast<float>(src[i]);
            if (m_pixelRep && (val >= (1u << (m_bitsStored - 1)))) {
                val -= static_cast<float>(1u << m_bitsStored);
            }
            m_floatPixels[i] = val;
        }
    } else if (m_bitsAllocated == 32) {
        const uint32_t *src = reinterpret_cast<const uint32_t*>(pixelTag.data);
        size_t maxSrc = pixelTag.length / 4;
        for (uint32_t i = 0; i < pixelCount * m_samplesPerPixel && i < maxSrc; ++i) {
            m_floatPixels[i] = static_cast<float>(src[i]);
        }
    }

    applyRescaleSlope();
    buildDisplayImage();
}

void DicomLoader::applyRescaleSlope()
{
    if (m_rescaleSlope == 1.0f && m_rescaleIntercept == 0.0f) return;

    for (auto &v : m_floatPixels) {
        v = v * m_rescaleSlope + m_rescaleIntercept;
    }
}

void DicomLoader::buildDisplayImage()
{
    if (m_floatPixels.empty()) return;

    uint32_t pixelCount = m_rows * m_cols;
    m_image = QImage(m_cols, m_rows, QImage::Format_Grayscale8);

    // 确定数值范围
    float minVal = m_floatPixels[0], maxVal = m_floatPixels[0];
    for (uint32_t i = 0; i < pixelCount; ++i) {
        float v = m_floatPixels[i];
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }

    // 应用窗宽窗位（如果存在）
    float wlCenter = m_wlCenter;
    float wlWidth = m_wlWidth;

    if (wlWidth <= 0.0f) {
        // 自动窗宽窗位
        wlCenter = (minVal + maxVal) / 2.0f;
        wlWidth = maxVal - minVal;
        if (wlWidth <= 0.0f) wlWidth = 1.0f;
        m_wlCenter = wlCenter;
        m_wlWidth = wlWidth;
    }

    float wlMin = wlCenter - wlWidth / 2.0f;
    float wlRange = wlWidth;

    for (uint32_t y = 0; y < m_rows; ++y) {
        uchar *line = m_image.scanLine(y);
        for (uint32_t x = 0; x < m_cols; ++x) {
            uint32_t idx = y * m_cols + x;
            float val = m_floatPixels[idx];

            // 窗口映射到 [0, 255]
            float normalized = (val - wlMin) / wlRange;
            int pixel = static_cast<int>(normalized * 255.0f);
            line[x] = static_cast<uchar>(std::max(0, std::min(255, pixel)));
        }
    }
}

void DicomLoader::applyWindowLevel(float center, float width)
{
    if (center == m_wlCenter && width == m_wlWidth) return;
    m_wlCenter = center;
    m_wlWidth = width;
    buildDisplayImage();
    emit fileLoaded();
}

// ---- 多帧导航 ----

void DicomLoader::nextFrame() {
    if (m_frameCount <= 1) return;
    int next = m_currentFrame + 1;
    if (next >= m_frameCount) next = 0;
    setFrame(next);
}

void DicomLoader::prevFrame() {
    if (m_frameCount <= 1) return;
    int prev = m_currentFrame - 1;
    if (prev < 0) prev = m_frameCount - 1;
    setFrame(prev);
}

void DicomLoader::setFrame(int frame) {
    if (frame < 0 || frame >= m_frameCount) return;
    if (frame == m_currentFrame) return;
    m_currentFrame = frame;
    if (!m_frameData.empty() && static_cast<size_t>(frame) < m_frameData.size()) {
        auto &fi = m_frameData[frame];
        decodePixels({0x7FE0, 0x0010, fi.length, fi.data});
    }
    buildDisplayImage();
    emit frameChanged();
    emit fileLoaded();
}

// ---- 标签读取辅助 ----

QString DicomLoader::readStringTag(uint16_t group, uint16_t element) const
{
    for (auto &t : m_tags) {
        if (t.group == group && t.element == element && t.data) {
            // 去掉尾部空格
            int len = t.length;
            while (len > 0 && t.data[len - 1] == ' ') --len;
            return QString::fromLatin1((const char*)t.data, len);
        }
    }
    return {};
}

uint16_t DicomLoader::readUint16Tag(uint16_t group, uint16_t element) const
{
    for (auto &t : m_tags) {
        if (t.group == group && t.element == element && t.length >= 2 && t.data) {
            // DICOM 显式 VR Little Endian 中 16-bit 数值为小端字节序
            // 大端传输语法则使用大端
            if (m_bigEndianTransfer) {
                return (t.data[0] << 8) | t.data[1];
            } else {
                return (t.data[1] << 8) | t.data[0];
            }
        }
    }
    return 0;
}

float DicomLoader::readFloatTag(uint16_t group, uint16_t element) const
{
    QString s = readStringTag(group, element);
    if (s.isEmpty()) return 0.0f;

    // DICOM DS (Decimal String) 可能包含多个值（用 \ 分隔），取第一个
    int slashPos = s.indexOf('\\');
    if (slashPos >= 0) {
        s = s.left(slashPos);
    }

    bool ok = false;
    float val = s.toFloat(&ok);
    return ok ? val : 0.0f;
}
