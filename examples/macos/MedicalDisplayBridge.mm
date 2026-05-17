/**
 * @file MedicalDisplayBridge.mm
 * @brief macOS Objective-C++ 桥接层 - 连接 C SDK 到 Swift/Metal
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// C SDK 头文件引用
// ============================================================================
#include "sdk/display_engine/include/display_engine.h"
#include "sdk/ai_engine/include/ai_engine.h"
#include "sdk/surgical_video/include/surgical_video.h"

// ============================================================================
// Objective-C 类接口
// ============================================================================
@interface MedicalDisplayBridge : NSObject

+ (instancetype)sharedInstance;

// 显示引擎
- (BOOL)initializeDisplayEngine;
- (void)applyDisplayConfig:(Display_Config *)config;

// AI 引擎
- (BOOL)initializeAIEngine;
- (AI_RecognitionResult *)recognizeModality:(const uint8_t *)imageData
                                       width:(int)width
                                      height:(int)height
                                    channels:(int)channels;

// 术野增强
- (BOOL)initializeSurgicalEngine:(SurgicalPreset)preset;
- (void)processVideoFrame:(const uint8_t *)input
                    output:(uint8_t *)output
                    width:(int)width
                   height:(int)height
                   params:(const EnhancementParams *)params;

// 清理
- (void)cleanup;

@end

#ifdef __cplusplus
}
#endif
