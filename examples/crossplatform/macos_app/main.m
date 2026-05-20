/**
 * AI Medical Display - macOS GUI (Responsive Layout + Fixed Rendering)
 */
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <simd/simd.h>

static const char* shader = R"(
#include <metal_stdlib>
using namespace metal;

kernel void gen(texture2d<float, access::write> o [[texture(0)]],
                constant float4& p [[buffer(0)]],
                uint2 g [[thread_position_in_grid]]) {
    uint w = uint(p.x);
    uint h = uint(p.y);
    if (g.x >= w || g.y >= h) return;
    
    float x = (float)g.x / (float)w;
    float y = (float)g.y / (float)h;
    float d = sqrt((x - 0.5) * (x - 0.5) + (y - 0.5) * (y - 0.5));
    float mode = p.z;
    float gsdf = p.w;
    
    float3 rgb;
    
    if (mode < 0.5) {
        float c = clamp(1.0 - d * 2.5, 0.0, 1.0);
        rgb = float3(c * 0.9 + 0.1);
        float ring = sin(d * 30.0) * 0.1;
        rgb += float3(ring);
    } else if (mode < 1.5) {
        float a = atan2(y - 0.5, x - 0.5);
        float wave = sin(a * 8.0 + d * 20.0) * 0.3 + 0.5;
        rgb = float3(wave * 0.3, wave * 0.5, wave * 0.9);
    } else if (mode < 2.5) {
        float val = (d < 0.15) ? 0.95 : (d < 0.35) ? 0.6 - d : 0.08;
        rgb = float3(val);
    } else {
        float angle = atan2(y - 0.5, x - 0.3);
        float depth = d * 2.5;
        float echo = sin(angle * 12.0 - depth * 8.0) * 0.4 + 0.6;
        echo *= clamp(1.0 - depth * 0.5, 0.2, 1.0);
        rgb = float3(echo, echo * 0.95, echo * 0.9);
    }
    
    if (gsdf > 0.5) {
        float gray = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
        if (gray > 0.01) {
            float gv = pow(10.0, -0.6225 + 0.082 * log(gray * 8.5) / 2.302585) / 40.0;
            rgb *= (gv / gray);
            rgb = clamp(rgb, 0.0, 1.0);
        }
    }
    
    o.write(float4(rgb, 1.0), g);
}
)";

@interface MetalView : NSView {
    CAMetalLayer *_metalLayer;
}
@property (nonatomic, assign) int mode;
@property (nonatomic, assign) int gsdf;
@end

@implementation MetalView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _mode = 0;
        _gsdf = 1;
        
        [self setWantsLayer:YES];
        
        _metalLayer = [[CAMetalLayer alloc] init];
        _metalLayer.frame = self.bounds;
        _metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
        self.layer = _metalLayer;
    }
    return self;
}

- (void)layout {
    [super layout];
    _metalLayer.frame = self.bounds;
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    _metalLayer.frame = self.bounds;
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate> {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> pipeline;
    MetalView *metalView;
    NSView *sidebar;
    NSView *rightPanel;
    NSTextField *statusLabel;
    NSTextField *modalityLabel;
    NSTimer *renderTimer;
    int gMode;
    int gGSDF;
    NSWindow *mainWindow;
}
@end

@implementation AppDelegate

- (void)render {
    if (!pipeline) return;
    
    CAMetalLayer *layer = (CAMetalLayer*)metalView.layer;
    if (!layer) return;
    
    CGFloat scale = mainWindow.backingScaleFactor;
    CGSize size = metalView.bounds.size;
    CGSize drawableSize = CGSizeMake(size.width * scale, size.height * scale);
    
    if (drawableSize.width <= 0 || drawableSize.height <= 0) return;
    layer.drawableSize = drawableSize;
    
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) return;
    
    simd_float4 params = {drawableSize.width, drawableSize.height, (float)gMode, (float)gGSDF};
    
    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:pipeline];
    [enc setTexture:drawable.texture atIndex:0];
    [enc setBytes:&params length:sizeof(params) atIndex:0];
    
    MTLSize threadsPerGroup = MTLSizeMake(16, 16, 1);
    MTLSize numGroups = MTLSizeMake((drawableSize.width + 15) / 16,
                                     (drawableSize.height + 15) / 16, 1);
    [enc dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
}

- (void)selectMode:(id)sender {
    gMode = (int)[sender tag];
    [modalityLabel setStringValue:[NSString stringWithFormat:@"模态: %@", @[@"CT", @"MRI", @"X-Ray", @"超声"][gMode]]];
    metalView.mode = gMode;
}

- (void)gsdfChanged:(NSButton*)checkbox {
    gGSDF = (checkbox.state == 1) ? 1 : 0;
    metalView.gsdf = gGSDF;
}

- (NSButton*)makeWhiteButton:(NSString*)title tag:(int)tag action:(SEL)action frame:(NSRect)frame {
    NSButton *btn = [[NSButton alloc] initWithFrame:frame];
    [btn setTitle:title];
    [btn setTarget:self];
    [btn setAction:action];
    btn.tag = tag;
    btn.bezelStyle = NSBezelStyleRounded;
    
    NSAttributedString *whiteTitle = [[NSAttributedString alloc]
        initWithString:title
            attributes:@{
                NSForegroundColorAttributeName: [NSColor whiteColor],
                NSFontAttributeName: [NSFont systemFontOfSize:13 weight:NSFontWeightMedium]
            }];
    [btn setAttributedTitle:whiteTitle];
    return btn;
}

- (void)setupUI {
    mainWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 1200, 800)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered defer:NO];
    [mainWindow setTitle:@"AI Medical Display"];
    [mainWindow setMinSize:NSMakeSize(800, 600)];
    [mainWindow makeKeyAndOrderFront:nil];
    
    NSView *content = [mainWindow contentView];
    [content setWantsLayer:YES];
    [content.layer setBackgroundColor:[NSColor colorWithRed:0.12 green:0.12 blue:0.14 alpha:1.0].CGColor];
    
    // 左侧边栏 - 固定宽度 180
    sidebar = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 180, 800)];
    [sidebar setWantsLayer:YES];
    [sidebar.layer setBackgroundColor:[NSColor colorWithRed:0.08 green:0.08 blue:0.10 alpha:1.0].CGColor];
    [sidebar setAutoresizingMask:NSViewMaxXMargin | NSViewHeightSizable];
    [content addSubview:sidebar];
    
    NSTextField *title = [NSTextField labelWithString:@"📁 影像模态"];
    title.frame = NSMakeRect(15, 760, 150, 24);
    title.textColor = [NSColor whiteColor];
    title.font = [NSFont boldSystemFontOfSize:14];
    [sidebar addSubview:title];
    
    NSArray *names = @[@"CT 扫描", @"MRI 影像", @"X-Ray", @"超声"];
    for (int i = 0; i < 4; i++) {
        NSButton *btn = [self makeWhiteButton:names[i] tag:i action:@selector(selectMode:) frame:NSMakeRect(15, 720 - i * 38, 150, 32)];
        [sidebar addSubview:btn];
    }
    
    NSTextField *gsdfStatus = [NSTextField labelWithString:@"🟢 GSDF 已启用"];
    gsdfStatus.frame = NSMakeRect(15, 100, 150, 20);
    gsdfStatus.textColor = [NSColor colorWithRed:0.3 green:0.9 blue:0.4 alpha:1.0];
    gsdfStatus.font = [NSFont systemFontOfSize:11];
    [sidebar addSubview:gsdfStatus];
    
    // 右侧面板 - 固定宽度 200
    rightPanel = [[NSView alloc] initWithFrame:NSMakeRect(1000, 0, 200, 800)];
    [rightPanel setWantsLayer:YES];
    [rightPanel.layer setBackgroundColor:[NSColor colorWithRed:0.08 green:0.08 blue:0.10 alpha:1.0].CGColor];
    [rightPanel setAutoresizingMask:NSViewMinXMargin | NSViewHeightSizable];
    [content addSubview:rightPanel];
    
    NSTextField *aiTitle = [NSTextField labelWithString:@"🤖 AI 识别"];
    aiTitle.frame = NSMakeRect(15, 760, 170, 20);
    aiTitle.textColor = [NSColor whiteColor];
    aiTitle.font = [NSFont boldSystemFontOfSize:14];
    [rightPanel addSubview:aiTitle];
    
    modalityLabel = [NSTextField labelWithString:@"模态: CT"];
    modalityLabel.frame = NSMakeRect(15, 730, 170, 22);
    modalityLabel.textColor = [NSColor colorWithRed:0.3 green:0.7 blue:1.0 alpha:1.0];
    modalityLabel.font = [NSFont boldSystemFontOfSize:16];
    [rightPanel addSubview:modalityLabel];
    
    NSTextField *infoLabel = [NSTextField labelWithString:@"来源: 合成图像"];
    infoLabel.frame = NSMakeRect(15, 705, 170, 18);
    infoLabel.textColor = [NSColor colorWithRed:0.4 green:0.9 blue:0.4 alpha:1.0];
    infoLabel.font = [NSFont systemFontOfSize:12];
    [rightPanel addSubview:infoLabel];
    
    NSBox *sep = [[NSBox alloc] initWithFrame:NSMakeRect(15, 685, 170, 1)];
    sep.boxType = NSBoxSeparator;
    [rightPanel addSubview:sep];
    
    NSTextField *ctrlTitle = [NSTextField labelWithString:@"🖥️ 显示控制"];
    ctrlTitle.frame = NSMakeRect(15, 650, 170, 20);
    ctrlTitle.textColor = [NSColor whiteColor];
    ctrlTitle.font = [NSFont boldSystemFontOfSize:14];
    [rightPanel addSubview:ctrlTitle];
    
    NSTextField *brightLbl = [NSTextField labelWithString:@"亮度"];
    brightLbl.frame = NSMakeRect(15, 615, 50, 20);
    brightLbl.textColor = [NSColor lightGrayColor];
    brightLbl.font = [NSFont systemFontOfSize:11];
    [rightPanel addSubview:brightLbl];
    
    NSSlider *brightSlider = [[NSSlider alloc] initWithFrame:NSMakeRect(65, 615, 120, 20)];
    brightSlider.minValue = -0.5;
    brightSlider.maxValue = 0.5;
    [rightPanel addSubview:brightSlider];
    
    NSButton *gsdfCheck = [NSButton checkboxWithTitle:@"GSDF 校准" target:self action:@selector(gsdfChanged:)];
    gsdfCheck.frame = NSMakeRect(15, 580, 170, 20);
    gsdfCheck.state = 1;
    [rightPanel addSubview:gsdfCheck];
    
    statusLabel = [NSTextField labelWithString:@"✅ 就绪"];
    statusLabel.frame = NSMakeRect(15, 20, 170, 18);
    statusLabel.textColor = [NSColor lightGrayColor];
    statusLabel.font = [NSFont systemFontOfSize:10];
    [rightPanel addSubview:statusLabel];
    
    // Metal 视图 - 填充中间区域
    metalView = [[MetalView alloc] initWithFrame:NSMakeRect(180, 0, 820, 800)];
    [metalView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [content addSubview:metalView];
    
    // 配置 MetalLayer
    CAMetalLayer *layer = (CAMetalLayer*)metalView.layer;
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatRGBA8Unorm;
    layer.backgroundColor = [NSColor blackColor].CGColor;
    layer.contentsScale = mainWindow.backingScaleFactor;
}

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    fprintf(stderr, "=== Starting ===\n");
    gMode = 0;
    gGSDF = 1;
    
    device = MTLCreateSystemDefaultDevice();
    if (!device) { fprintf(stderr, "No Metal\n"); return; }
    fprintf(stderr, "Metal: %s\n", [[device name] UTF8String]);
    
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:@(shader) options:nil error:&err];
    if (err) { fprintf(stderr, "Shader: %s\n", [[err localizedDescription] UTF8String]); return; }
    
    id<MTLFunction> func = [lib newFunctionWithName:@"gen"];
    pipeline = [device newComputePipelineStateWithFunction:func error:&err];
    if (err) { fprintf(stderr, "Pipeline: %s\n", [[err localizedDescription] UTF8String]); return; }
    
    queue = [device newCommandQueue];
    fprintf(stderr, "Pipeline OK\n");
    
    [self setupUI];
    
    // 渲染定时器
    renderTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/30.0 target:self selector:@selector(render) userInfo:nil repeats:YES];
    fprintf(stderr, "Ready\n");
}

- (void)applicationWillTerminate:(NSNotification*)note {
    if (renderTimer) {
        [renderTimer invalidate];
        renderTimer = nil;
    }
}

@end

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp setDelegate:[[AppDelegate alloc] init]];
        [NSApp run];
    }
    return 0;
}
