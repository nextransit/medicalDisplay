/**
 * 原生文件对话框 — macOS NSOpenPanel 封装
 *
 * 通过 extern "C" 暴露给 C++ 调用，绕过 Qt6 QFileDialog（需要 QApplication）。
 */

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <cstdio>

extern "C" {

/**
 * 打开原生文件选择对话框
 * @param outPath 输出缓冲区（调用者分配，至少 1024 字节）
 * @param title 对话框标题
 * @param extensions 文件扩展名过滤（分号分隔，如 "dcm;dicom"），传 NULL 为所有文件
 * @return 0=成功, -1=取消, -2=错误
 */
int native_open_file_dialog(char *outPath, const char *title, const char *extensions) {
    if (!outPath) return -2;
    outPath[0] = '\0';

    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        panel.title = title ? @(title) : @"选择文件";
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;

        if (extensions && extensions[0]) {
            NSString *extStr = @(extensions);
            NSArray *extArray = [extStr componentsSeparatedByString:@";"];
            NSMutableArray *types = [NSMutableArray array];
            for (NSString *ext in extArray) {
                NSString *trimmed = [ext stringByTrimmingCharactersInSet:
                                     [NSCharacterSet whitespaceCharacterSet]];
                if (trimmed.length > 0) {
                    [types addObject:trimmed];
                }
            }
            if (types.count > 0) {
                NSMutableArray *contentTypes = [NSMutableArray array];
                for (NSString *ext in types) {
                    UTType *type = [UTType typeWithFilenameExtension:ext];
                    if (type) [contentTypes addObject:type];
                }
                if (contentTypes.count > 0) {
                    panel.allowedContentTypes = contentTypes;
                }
            }
        }

        NSInteger result = [panel runModal];
        if (result == NSModalResponseOK && panel.URL) {
            const char *path = [[panel.URL path] UTF8String];
            if (path) {
                strncpy(outPath, path, 1023);
                outPath[1023] = '\0';
                return 0;
            }
        }
        return (result == NSModalResponseCancel) ? -1 : -2;
    }
}

}  // extern "C"
