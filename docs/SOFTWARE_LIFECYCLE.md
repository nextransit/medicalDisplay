# AI 自适应医疗显示系统 - 软件生命周期文档 (IEC 62304)

**版本**: v1.0  
**日期**: 2026-05-17  
**标准**: IEC 62304:2006+A1 (Medical device software - Software life cycle processes)

---

## 1. 软件安全分类

### 1.1 分类依据

根据 IEC 62304 第 4.3 条，基于软件对患者安全的潜在影响：

| 软件项 | 安全分类 | 理由 |
|--------|----------|------|
| AI模态识别 | Class B | 可能导致误诊，但不直接造成伤害 |
| GSDF显示校准 | Class B | 显示参数偏差可能导致漏诊 |
| 手术视频增强 | Class C | 直接用于手术视野，潜在安全影响 |

### 1.2 确认的安全分类

```
软件安全等级: Class B (依据 IEC 62304 Table 3)
- 不会造成不可接受的伤害风险
- 软件故障可能导致可恢复的伤害或误诊
```

---

## 2. 软件开发计划

### 2.1 开发过程

```
┌─────────────────────────────────────────────────────────────┐
│                    软件开发计划 (SWDP)                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  输入:                                                      │
│  ├── 器械描述                                               │
│  ├── 预期用途声明                                           │
│  ├── 风险分析                                               │
│  └── 现有技术文档                                            │
│                                                              │
│  过程:                                                      │
│  ├── SW-DP-1: 分配软件需求                                  │
│  ├── SW-DP-2: 软件架构设计                                  │
│  ├── SW-DP-3: 软件详细设计                                  │
│  ├── SW-DP-4: 软件单元实现                                   │
│  ├── SW-DP-5: 软件单元验证                                  │
│  ├── SW-DP-6: 软件集成与测试                                │
│  ├── SW-DP-7: 软件系统测试                                  │
│  └── SW-DP-8: 软件发布                                      │
│                                                              │
│  输出:                                                      │
│  ├── 软件架构文档 (SAD)                                      │
│  ├── 软件详细设计文档 (SDD)                                   │
│  ├── 源代码                                                  │
│  ├── 测试计划和报告                                           │
│  └── 软件发布记录                                             │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 软件需求分配

| 需求ID | 描述 | 来源 | 验证方法 |
|--------|------|------|----------|
| SRS-001 | 支持CT/MRI/XRay等模态识别 | 用户需求 | 单元测试 |
| SRS-002 | DICOM GSDF校准精度ΔE<3 | IEC 60601-2-44 | 性能测试 |
| SRS-003 | 视频处理延迟<20ms | 实时性需求 | 集成测试 |
| SRS-004 | 多显示器色彩一致性 | 临床需求 | 系统测试 |
| SRS-005 | OTA安全更新 | 安全需求 | 安全测试 |

---

## 3. 软件架构设计

### 3.1 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                 AI Adaptive Medical Display System                │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────┐   │
│  │ AI Engine   │  │ Display Eng │  │ Surgical Video Eng   │   │
│  │ (Class B)  │  │ (Class B)   │  │ (Class C)            │   │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬───────────┘   │
│         │                │                      │                │
│  ┌──────┴────────────────┴──────────────────────┴───────────┐   │
│  │                   Platform Abstraction Layer              │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐     │   │
│  │  │  Linux    │  │ Android   │  │    macOS/Windows  │     │   │
│  │  │ (DRM/KMS) │  │ (NDK)    │  │    (Metal/DX)    │     │   │
│  │  └──────────┘  └──────────┘  └──────────────────┘     │   │
│  └───────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 软件项列表

| 软件项 | 类型 | 安全分类 | 依赖关系 |
|--------|------|----------|----------|
| AI Engine | 可执行 | Class B | Platform Layer |
| Display Engine | 可执行 | Class B | Platform Layer |
| Surgical Video Engine | 可执行 | Class C | AI Engine, Platform |
| Cloud Agent | 可执行 | Class B | AI Engine |
| Federated Learning | 可执行 | Class B | Cloud Agent |

### 3.3 接口定义

```c
// AI Engine 接口
typedef struct AIEngine AIEngine;
AIEngine* ai_engine_create(const AIEngineConfig* config);
int ai_engine_recognize(AIEngine* engine, const AI_ImageBuffer* image, 
                        AI_RecognitionResult* result);
void ai_engine_destroy(AIEngine* engine);

// Display Engine 接口
typedef struct DisplayEngine DisplayEngine;
DisplayEngine* display_open(const char* device_path);
int display_apply_config(DisplayEngine* engine, const Display_Config* config);
void display_close(DisplayEngine* engine);

// Surgical Video Engine 接口
typedef struct SurgicalVideoEngine SurgicalVideoEngine;
SurgicalVideoEngine* surgical_engine_create(const SurgicalConfig* config);
int surgical_engine_process_frame(SurgicalVideoEngine* engine, 
                                 const uint8_t* input, uint8_t* output);
void surgical_engine_destroy(SurgicalVideoEngine* engine);
```

---

## 4. 软件详细设计

### 4.1 AI Engine 模块设计

```c
// 4.1.1 模态识别策略
typedef struct {
    const char* modality_name;     // "CT", "MRI", etc.
    float brightness_min;
    float brightness_max;
    float contrast_range;
    float saturation_range;
    int recommended_window_center;
    int recommended_window_width;
} DisplayStrategy;

// 4.1.2 AI识别结果
typedef struct {
    ModalityType modality;
    float confidence;
    DisplayStrategy strategy;
    char metadata[512];
} AI_RecognitionResult;

// 4.1.3 识别流程
int ai_engine_recognize_internal(AIEngine* engine, 
                                 const uint8_t* image_data,
                                 int width, int height,
                                 AI_RecognitionResult* result) {
    // Step 1: 特征提取
    float features[128];
    extract_features(image_data, width, height, features);
    
    // Step 2: ONNX推理 (或规则引擎fallback)
    float scores[MODALITY_COUNT];
    if (engine->use_onnx && engine->onnx_backend) {
        engine->onnx_backend->Run(features, scores, MODALITY_COUNT);
    } else {
        rule_based_classification(features, scores);
    }
    
    // Step 3: 选择最高分
    int best_idx = argmax(scores, MODALITY_COUNT);
    
    // Step 4: 填充结果
    result->modality = best_idx;
    result->confidence = scores[best_idx];
    result->strategy = DEFAULT_STRATEGIES[best_idx];
    
    return 0;
}
```

### 4.2 GSDF 计算模块设计

```c
// 4.2.1 GSDF常数 (DICOM Part 14 Table 6.2-1)
#define GSDF_JND_MIN    0.1f
#define GSDF_JND_MAX    850.0f
#define GSDF_L_MIN      0.05f
#define GSDF_L_MAX      4000.0f

// Eq.7-1 系数
#define GSDF_EQ71_A  -0.6225f
#define GSDF_EQ71_B   0.0820f
#define GSDF_EQ71_C   0.3698f
// ... (完整系数见 dicom_gsdf.h)

// 4.2.2 JND到亮度转换
float gsdf_jnd_to_luminance(float jnd) {
    float j = clamp(jnd, GSDF_JND_MIN, GSDF_JND_MAX);
    float y = logf(j);
    float y2 = y * y, y3 = y2 * y, y4 = y3 * y, y5 = y4 * y;
    
    float num = GSDF_EQ71_A + GSDF_EQ71_C*y + GSDF_EQ71_E*y2 
              + GSDF_EQ71_G*y3 + GSDF_EQ71_M*y4;
    float den = 1.0f + GSDF_EQ71_B*y + GSDF_EQ71_D*y2 
              + GSDF_EQ71_F*y3 + GSDF_EQ71_H*y4 + GSDF_EQ71_K*y5;
    
    return powf(10.0f, num / (den + 1e-12f));
}

// 4.2.3 LUT生成
int gsdf_generate_lut(uint8_t* lut, GsdfStandard standard) {
    for (int i = 0; i < 256; i++) {
        float jnd = (i / 255.0f) * (GSDF_JND_MAX - GSDF_JND_MIN) 
                  + GSDF_JND_MIN;
        float luminance = gsdf_jnd_to_luminance(jnd);
        // 归一化到8位
        lut[i] = (uint8_t)(luminance / GSDF_L_MAX * 255.0f);
    }
    return 0;
}
```

### 4.3 手术视频增强设计

```c
// 4.3.1 无血术野增强算法
typedef struct {
    float suppress_level;    // 血色抑制强度 (0.0-1.0)
    float tissue_enhance;   // 组织对比度增强
    float edge_preserve;     // 边缘保留度
} BloodlessConfig;

int bloodless_enhance(const uint8_t* input, uint8_t* output,
                      int width, int height,
                      const BloodlessConfig* config) {
    // Step 1: RGB到HSV转换
    for (int i = 0; i < width * height; i++) {
        float r = input[i*3] / 255.0f;
        float g = input[i*3+1] / 255.0f;
        float b = input[i*3+2] / 255.0f;
        
        float h, s, v;
        rgb_to_hsv(r, g, b, &h, &s, &v);
        
        // Step 2: 血色检测 (H在0-30度范围)
        if (h >= 0.0f && h <= 30.0f/180.0f && s > 0.3f) {
            // 抑制红色，增强绿色
            s *= (1.0f - config->suppress_level * 0.7f);
            v *= (1.0f + config->tissue_enhance * 0.2f);
        }
        
        // Step 3: HSV转回RGB
        hsv_to_rgb(h, s, v, &r, &g, &b);
        output[i*3] = (uint8_t)(r * 255.0f);
        output[i*3+1] = (uint8_t)(g * 255.0f);
        output[i*3+2] = (uint8_t)(b * 255.0f);
    }
    
    return 0;
}
```

---

## 5. 软件单元验证

### 5.1 单元测试覆盖要求

| 模块 | 覆盖目标 | 测试用例数 |
|------|----------|------------|
| AI Engine | 100% 路径覆盖 | 50+ |
| Display Engine | 100% 路径覆盖 | 30+ |
| GSDF | 100% 分支覆盖 | 40+ |
| Bloodless | 100% 路径覆盖 | 25+ |
| V4L2 | 100% 路径覆盖 | 20+ |

### 5.2 测试用例示例

```cpp
// GSDF JND-Luminance 转换测试
TEST(GSDFTest, JndToLuminance_At500Jnd) {
    float luminance = gsdf_jnd_to_luminance(500.0f);
    EXPECT_NEAR(luminance, 1035.0f, 1.0f);  // DICOM标准值
}

TEST(GSDFTest, LuminanceRoundTrip) {
    for (float jnd = 50.0f; jnd < 800.0f; jnd += 10.0f) {
        float lum = gsdf_jnd_to_luminance(jnd);
        float jnd_back = gsdf_luminance_to_jnd(lum);
        EXPECT_NEAR(jnd, jnd_back, 0.1f);
    }
}

// AI模态识别测试
TEST(AIEngineTest, CTRecognition) {
    AIEngine* engine = ai_engine_create(nullptr);
    uint8_t ct_image[512*512*3];  // 模拟CT图像
    
    AI_RecognitionResult result;
    ai_engine_recognize(engine, ct_image, 512, 512, 3, &result);
    
    EXPECT_EQ(result.modality, MODALITY_CT);
    EXPECT_GT(result.confidence, 0.8f);
    
    ai_engine_destroy(engine);
}

// 血色抑制测试
TEST(BloodlessTest, SuppressRedDominated) {
    uint8_t red_dominated[100*100*3];
    // 填充红色主导图像
    memset(red_dominated, 200, 100*100);  // R=200
    memset(red_dominated+1, 50, 100*100);  // G=50
    memset(red_dominated+2, 50, 100*100);  // B=50
    
    uint8_t output[100*100*3];
    BloodlessConfig config = {0.8f, 0.3f, 0.5f};
    
    bloodless_enhance(red_dominated, output, 100, 100, &config);
    
    // 验证红色被抑制
    EXPECT_LT(output[0], red_dominated[0]);
    EXPECT_GT(output[1], red_dominated[1]);
}
```

---

## 6. 软件集成与测试

### 6.1 集成测试策略

```
Level 1: 单元集成
├── AI Engine + Display Engine
├── GSDF + Display Pipeline
└── Bloodless + Video Pipeline

Level 2: 子系统集成
├── AI Recognition → Display Strategy → GSDF
├── V4L2 Capture → Bloodless → Display
└── Cloud Agent → OTA → Local Storage

Level 3: 系统集成
├── All subsystems
├── Full pipeline
└── End-to-end workflow
```

### 6.2 集成测试用例

```cpp
// 集成测试: AI识别 → 自动校准
TEST(IntegrationTest, AIRecognitionToAutoCalibration) {
    // Setup
    AIEngine* ai = ai_engine_create(nullptr);
    DisplayEngine* display = display_open("/dev/dri/card0");
    
    // Load CT图像
    uint8_t ct_image[512*512*3];
    load_test_image("ct_512.raw", ct_image);
    
    // 执行识别
    AI_RecognitionResult result;
    ai_engine_recognize(ai, ct_image, 512, 512, 3, &result);
    ASSERT_EQ(result.modality, MODALITY_CT);
    
    // 应用推荐参数
    Display_Config config = {
        .window_center = result.strategy.recommended_window_center,
        .window_width = result.strategy.recommended_window_width,
        .enable_gsdf = true
    };
    display_apply_config(display, &config);
    
    // 验证显示质量
    float delta_e = measure_delta_e(display);
    EXPECT_LT(delta_e, 3.0f);  // DICOM要求
    
    // Cleanup
    ai_engine_destroy(ai);
    display_close(display);
}
```

---

## 7. 软件发布管理

### 7.1 发布版本规则

| 版本格式 | 规则 | 示例 |
|----------|------|------|
| Major.Minor.Patch | 重大功能/功能/修复 | 2.0.1 |
| Build | 每日构建 | 20260517 |
| Release | 正式发布 | RC1, GA |

### 7.2 发布检查清单

```
□ 代码审查完成
□ 所有单元测试通过
□ 集成测试通过
□ 性能测试达标
□ 安全扫描通过
□ 文档更新完成
□ 版本号已更新
□ 变更日志已记录
□ 发布批准签字
□ 构建产物验证
□ 兼容性测试通过
```

---

## 8. 维护过程

### 8.1 问题分级

| 级别 | 定义 | 响应时间 | 解决时间 |
|------|------|----------|----------|
| Critical | 系统崩溃/数据丢失 | 4小时 | 24小时 |
| High | 核心功能失效 | 24小时 | 7天 |
| Medium | 功能异常 | 5天 | 30天 |
| Low | 界面/文档问题 | 30天 | 90天 |

### 8.2 维护活动

```yaml
维护过程:
  输入:
    - 问题报告
    - 变更请求
    - 安全公告
    
  过程:
    SW-MP-1: 问题分析
    SW-MP-2: 影响分析
    SW-MP-3: 修改实现
    SW-MP-4: 回归测试
    SW-MP-5: 发布更新
    
  输出:
    - 问题修复
    - 测试报告
    - 发布记录
```

---

## 9. 参考文档

| 文档ID | 标题 | 版本 |
|--------|------|------|
| DOC-001 | 风险管理文档 (ISO 14971) | v1.0 |
| DOC-002 | 临床评估报告 | v1.0 |
| DOC-003 | 技术文档 (EU MDR Annex II) | v1.0 |
| DOC-004 | 质量手册 (ISO 13485) | v1.0 |
| DOC-005 | 510(k) 申报文档 | v1.0 |
