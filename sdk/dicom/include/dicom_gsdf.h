#ifndef DICOM_GSDF_H
#define DICOM_GSDF_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DICOM GSDF (Grayscale Standard Display Function) — NEMA PS 3.14 §7
//
// JND→L (Eq.7-1): rational function of ln(j)
//   log10(L) = (a + c·ln(j) + e·ln²(j) + g·ln³(j) + m·ln⁴(j))
//            / (1 + b·ln(j) + d·ln²(j) + f·ln³(j) + h·ln⁴(j) + k·ln⁵(j))
//
// L→JND (Eq.7-2): direct degree-8 polynomial of log10(L)
//   j(L) = A + B·log10(L) + C·log10²(L) + … + I·log10⁸(L)
// ============================================================================

/* Equation 7-1 coefficients: JND → Luminance */
#define GSDF_EQ71_A   -1.3011877
#define GSDF_EQ71_B   -2.5840191E-2
#define GSDF_EQ71_C    8.0242636E-2
#define GSDF_EQ71_D   -1.0320229E-1
#define GSDF_EQ71_E    1.3646699E-1
#define GSDF_EQ71_F    2.8745620E-2
#define GSDF_EQ71_G   -2.5468404E-2
#define GSDF_EQ71_H   -3.1978977E-3
#define GSDF_EQ71_K    1.2992634E-4
#define GSDF_EQ71_M    1.3635334E-3

/* Equation 7-2 coefficients: Luminance → JND (polynomial in log10(L)) */
#define GSDF_EQ72_A   71.498068
#define GSDF_EQ72_B   94.593053
#define GSDF_EQ72_C   41.912053
#define GSDF_EQ72_D    9.8247004
#define GSDF_EQ72_E    0.28175407
#define GSDF_EQ72_F   -1.1878455
#define GSDF_EQ72_G   -0.18014349
#define GSDF_EQ72_H    0.14710899
#define GSDF_EQ72_I   -0.017046845

/* Luminance & JND range */
#define GSDF_L_MIN             0.05
#define GSDF_L_MAX          4000.0
#define GSDF_JND_MIN           1.0
#define GSDF_JND_MAX        1023.0

// ============================================================================
// 函数声明
// ============================================================================

/**
 * 计算给定亮度对应的Jnd值
 * @param luminance 亮度 (cd/m²)
 * @return Jnd值
 */
float gsdf_luminance_to_jnd(float luminance);

/**
 * 计算给定Jnd值对应的亮度
 * @param jnd Jnd值
 * @return 亮度 (cd/m²)
 */
float gsdf_jnd_to_luminance(float jnd);

/**
 * 计算P-Value (归一化显示值)
 * @param luminance 亮度 (cd/m²)
 * @param ambient_luminance 环境光亮度 (cd/m²)
 * @return P-Value (0.0 - 1.0)
 */
float gsdf_calculate_pvalue(float luminance, float ambient_luminance);

/**
 * P-Value转回亮度
 * @param pvalue P-Value
 * @param ambient_luminance 环境光亮度
 * @return 亮度 (cd/m²)
 */
float gsdf_pvalue_to_luminance(float pvalue, float ambient_luminance);

/**
 * 生成GSDF LUT (查找表)
 * @param lut 输出缓冲区 (需预分配)
 * @param lut_size LUT大小
 * @param bit_depth 输出位深 (8, 10, 12)
 * @param ambient_luminance 环境光亮度
 * @param max_luminance 最大亮度
 * @return 0成功
 */
int gsdf_generate_lut(float* lut, int lut_size, int bit_depth,
                      float ambient_luminance, float max_luminance);

/**
 * 应用GSDF到像素值
 * @param pixel_value 输入像素值 (归一化)
 * @param lut GSDF LUT
 * @param lut_size LUT大小
 * @return GSDF校正后的值
 */
float gsdf_apply_lut(float pixel_value, const float* lut, int lut_size);

/**
 * DICOM模态LUT处理 (HU -> P-Value)
 * @param hu_value CT值 (Hounsfield Unit)
 * @param rescale_slope Rescale Slope
 * @param rescale_intercept Rescale Intercept
 * @paramvoi_lut VOI LUT (可为NULL)
 * @param voi_lut_size VOI LUT大小 (0表示无)
 * @param lut_data VOI LUT数据
 * @param lut_bits VOI LUT位深
 * @param gsdf_lut GSDF LUT
 * @param gsdf_lut_size GSDF LUT大小
 * @param ambient 环境光亮度
 * @return 最终显示值 (归一化)
 */
float dicom_apply_modality_and_voi_lut(float hu_value,
                                        float rescale_slope,
                                        float rescale_intercept,
                                        const uint16_t* voi_lut,
                                        int voi_lut_size,
                                        int voi_lut_bits,
                                        const float* gsdf_lut,
                                        int gsdf_lut_size,
                                        float ambient);

/**
 * 批量应用GSDF (SIMD优化)
 * @param input 输入像素数组 (归一化)
 * @param output 输出像素数组
 * @param count 像素数量
 * @param lut GSDF LUT
 * @param lut_size LUT大小
 */
void gsdf_apply_lut_batch(const float* input, float* output, int count,
                          const float* lut, int lut_size);

// ============================================================================
// GSDF验证
// ============================================================================

/**
 * 验证GSDF LUT正确性
 * @param lut GSDF LUT
 * @param lut_size LUT大小
 * @param tolerance 允许的误差
 * @return true通过
 */
bool gsdf_validate_lut(const float* lut, int lut_size, float tolerance);

/**
 * 测量Jnd精度
 * @param measured_luminance 实测亮度
 * @param target_jnd 目标Jnd
 * @param ambient 环境光
 * @return 测量到的Jnd
 */
float gsdf_measure_jnd_accuracy(float measured_luminance, float target_jnd, float ambient);

/**
 * 计算Delta-Jnd误差
 * @param lut GSDF LUT
 * @param lut_size LUT大小
 * @return 最大误差
 */
float gsdf_calculate_delta_jnd(const float* lut, int lut_size);

#ifdef __cplusplus
}
#endif

#endif // DICOM_GSDF_H
