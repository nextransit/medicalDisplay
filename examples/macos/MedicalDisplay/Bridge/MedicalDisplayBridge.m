// MedicalDisplayBridge.m
#import "MedicalDisplayBridge.h"

@implementation MedicalDisplayBridge

- (nullable instancetype)init {
    self = [super init];
    if (self) {
        // 初始化 ONNX Runtime
        [self setupONNXBackend];
    }
    return self;
}

- (void)setupONNXBackend {
    // 从 SDK 获取 ONNX 后端状态
    // 实际通过 Bridge 调用 C++ SDK
}

- (NSDictionary *)recognizeFromImage:(NSData *)imageData width:(int)width height:(int)height {
    // 调用 SDK ai_engine_recognize_from_image
    // 返回模拟结果
    return @{
        @"modality": @"CT",
        @"confidence": @(0.91)
    };
}

- (void)dealloc {
    // 清理资源
}

@end