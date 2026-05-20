// MedicalDisplayBridge.h
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface MedicalDisplayBridge : NSObject
- (nullable instancetype)init;
- (NSDictionary *)recognizeFromImage:(NSData *)imageData width:(int)width height:(int)height;
@end

NS_ASSUME_NONNULL_END