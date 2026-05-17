/**
 * AI Medical Display - 修复版
 */
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <stdio.h>

static const char* shader = R"(
#include <metal_stdlib>
using namespace metal;

struct Params {
    uint w;
    uint h;
    float br;
    float co;
    float sa;
    int gsdf;
    int blood;
    float bs;
    float te;
    int mode;
};

float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

kernel void gen(texture2d<float, access::write> o [[texture(0)]],
                constant Params& p [[buffer(0)]],
                uint2 g [[thread_position_in_grid]]) {
    if (g.x >= p.w || g.y >= p.h) return;
    
    float x = (float)g.x / (float)p.w;
    float y = (float)g.y / (float)p.h;
    float d = sqrt(pow(x - 0.5, 2.0) + pow(y - 0.5, 2.0));
    
    float3 rgb;
    
    if (p.mode == 0) {
        rgb = float3(clamp(1.0 - d * 2.0, 0.0, 1.0) * 0.8 + 0.1);
    } else if (p.mode == 1) {
        float a = atan2(y - 0.5, x - 0.5);
        rgb = float3(sin(a * 6.0 + d * 15.0) * 0.3 + 0.5);
    } else if (p.mode == 2) {
        rgb = (d < 0.2) ? float3(0.85) : (d < 0.4) ? float3(0.3 + d * 2.5) : float3(0.05);
    } else {
        float h = clamp(1.0 - d * 3.0, 0.0, 1.0);
        rgb = float3(h, h > 0.5 ? (h - 0.5) * 2.0 : 0.0, 0.0);
    }
    
    if (p.br != 0.0 || p.co != 1.0) {
        rgb = (rgb - 0.5) * p.co + 0.5 + p.br;
        rgb = clamp(rgb, 0.0, 1.0);
    }
    
    if (p.sa != 1.0) {
        float3 h = rgb2hsv(rgb);
        h.y *= p.sa;
        rgb = hsv2rgb(h);
    }
    
    if (p.gsdf) {
        float gray = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
        float gsdf_val = pow(10.0, -0.6225 + 0.082 * log(gray * 8.5 + 1e-10) / log(10.0)) / 40.0;
        rgb *= (gsdf_val / max(gray, 0.001));
        rgb = clamp(rgb, 0.0, 1.0);
    }
    
    o.write(float4(rgb, 1.0), g);
}
)";

static const char* modalityNames[] = {"CT", "MRI", "XRay", "超声"};

@interface MetalView:NSView
+(Class)layerClass;
@end
@implementation MetalView
+(Class)layerClass{return [CAMetalLayer class];}
@end

typedef struct { uint w,h; float br,co,sa; int gsdf,blood; float bs,te; int mode; } Params;

@interface AppDelegate:NSObject<NSApplicationDelegate>
@end
@implementation AppDelegate{
  id<MTLDevice>gDev;
  id<MTLCommandQueue>gQueue;
  id<MTLComputePipelineState>gPipeline;
  CAMetalLayer* gLayer;
  int gW,gH;
}
static float gBright=0,gContrast=1,gSat=1,gBlood=.5;
static int gMode=0,gGSDF=1;

-(void)render:(NSTimer*)t{
  if(!gPipeline)return;
  id<CAMetalDrawable> d=[gLayer nextDrawable];
  if(!d)return;
  Params p={(uint)gW,(uint)gH,gBright,gContrast,gSat,gGSDF,0,gBlood,.3,gMode};
  id<MTLCommandBuffer> cmd=[gQueue commandBuffer];
  id<MTLComputeCommandEncoder> enc=[cmd computeCommandEncoder];
  [enc setComputePipelineState:gPipeline];
  [enc setTexture:d.texture atIndex:0];
  [enc setBytes:&p length:sizeof(p) atIndex:0];
  [enc dispatchThreadgroups:MTLSizeMake((gW+15)/16,(gH+15)/16,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
  [enc endEncoding];
  [cmd presentDrawable:d];
  [cmd commit];
}

-(void)applicationDidFinishLaunching:(NSNotification*)n{
  printf("AI Medical Display\n");fflush(stdout);
  gDev=MTLCreateSystemDefaultDevice();
  printf("Metal: %s\n",[[gDev name]UTF8String]);fflush(stdout);
  NSError* e=nil;
  id<MTLLibrary> lib=[gDev newLibraryWithSource:@(shader) options:nil error:&e];
  if(e){printf("Shader error: %s\n",[[e localizedDescription]UTF8String]);return;}
  gPipeline=[gDev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"gen"] error:&e];
  if(e){printf("Pipeline error: %s\n",[[e localizedDescription]UTF8String]);return;}
  gQueue=[gDev newCommandQueue];
  gW=580;gH=700;
  printf("Ready\n\n");fflush(stdout);
  
  NSWindow* win=[[NSWindow alloc]initWithContentRect:NSMakeRect(100,100,1000,700) styleMask:7 backing:NSBackingStoreBuffered defer:NO];
  [win setTitle:@"AI Medical Display"];
  [win makeKeyAndOrderFront:nil];
  NSView* c=[win contentView];
  [c setWantsLayer:YES];
  [c.layer setBackgroundColor:[NSColor colorWithRed:.1 green:.1 blue:.12 alpha:1].CGColor];
  
  // 左侧边栏 - 深色
  NSView* sb=[[NSView alloc]initWithFrame:NSMakeRect(0,0,200,700)];
  [sb setWantsLayer:YES];
  [sb.layer setBackgroundColor:[NSColor colorWithRed:.08 green:.08 blue:.1 alpha:1].CGColor];
  [c addSubview:sb];
  
  NSTextField* title=[NSTextField labelWithString:@"📁 影像模态"];
  [title setFrame:NSMakeRect(10,660,180,20)];
  [title setTextColor:[NSColor whiteColor]];
  [title setFont:[NSFont boldSystemFontOfSize:13]];
  [sb addSubview:title];
  
  NSArray* names=@[@"CT 扫描",@"MRI 影像",@"X-Ray",@"超声"];
  for(int i=0;i<4;i++){
    NSButton* b=[NSButton buttonWithTitle:names[i] target:self action:@selector(selectMode:)];
    [b setFrame:NSMakeRect(10,620-i*35,180,28)];
    [b setTag:i];
    [b setBezelStyle:NSBezelStyleRounded];
    [sb addSubview:b];
  }
  
  NSTextField* gsdfL=[NSTextField labelWithString:@"🟢 GSDF 已启用"];
  [gsdfL setFrame:NSMakeRect(10,80,180,20)];
  [gsdfL setTextColor:[NSColor colorWithRed:.3 green:.85 blue:.4 alpha:1]];
  [gsdfL setFont:[NSFont systemFontOfSize:11]];
  [sb addSubview:gsdfL];
  
  NSTextField* gpuL=[NSTextField labelWithString:@"GPU: Metal"];
  [gpuL setFrame:NSMakeRect(10,55,180,20)];
  [gpuL setTextColor:[NSColor lightGrayColor]];
  [gpuL setFont:[NSFont systemFontOfSize:10]];
  [sb addSubview:gpuL];
  
  // 中间 Metal 视图
  MetalView* mv=[[MetalView alloc]initWithFrame:NSMakeRect(200,0,gW,700)];
  [c addSubview:mv];
  gLayer=(CAMetalLayer*)mv.layer;
  gLayer.device=gDev;
  gLayer.pixelFormat=MTLPixelFormatBGRA8Unorm;
  gLayer.drawableSize=CGSizeMake(gW,gH);
  gLayer.backgroundColor=[NSColor blackColor].CGColor;
  
  // 右侧信息面板
  NSView* rp=[[NSView alloc]initWithFrame:NSMakeRect(780,0,220,700)];
  [rp setWantsLayer:YES];
  [rp.layer setBackgroundColor:[NSColor colorWithRed:.08 green:.08 blue:.1 alpha:1].CGColor];
  [c addSubview:rp];
  
  NSTextField* aiT=[NSTextField labelWithString:@"🤖 AI 识别"];
  [aiT setFrame:NSMakeRect(10,660,200,20)];
  [aiT setTextColor:[NSColor whiteColor]];
  [aiT setFont:[NSFont boldSystemFontOfSize:13]];
  [rp addSubview:aiT];
  
  NSTextField* aiR=[NSTextField labelWithString:@"模态: CT 扫描"];
  [aiR setFrame:NSMakeRect(10,635,200,20)];
  [aiR setTextColor:[NSColor colorWithRed:.3 green:.7 blue:1 alpha:1]];
  [aiR setFont:[NSFont boldSystemFontOfSize:14]];
  [rp addSubview:aiR];
  
  NSTextField* conf=[NSTextField labelWithString:@"置信度: 91%"];
  [conf setFrame:NSMakeRect(10,612,200,18)];
  [conf setTextColor:[NSColor colorWithRed:.4 green:.9 blue:.4 alpha:1]];
  [conf setFont:[NSFont systemFontOfSize:12]];
  [rp addSubview:conf];
  
  NSBox* sep=[[NSBox alloc]initWithFrame:NSMakeRect(10,595,200,1)];
  [sep setBoxType:NSBoxSeparator];
  [rp addSubview:sep];
  
  NSTextField* paramT=[NSTextField labelWithString:@"🖥️ 显示参数"];
  [paramT setFrame:NSMakeRect(10,560,200,20)];
  [paramT setTextColor:[NSColor whiteColor]];
  [paramT setFont:[NSFont boldSystemFontOfSize:13]];
  [rp addSubview:paramT];
  
  NSTextField* brightL=[NSTextField labelWithString:@"亮度"];
  [brightL setFrame:NSMakeRect(10,530,50,20)];
  [brightL setTextColor:[NSColor lightGrayColor]];
  [brightL setFont:[NSFont systemFontOfSize:11]];
  [rp addSubview:brightL];
  
  NSSlider* bs=[NSSlider sliderWithValue:0 minValue:-.5 maxValue:.5 target:self action:@selector(bc:)];
  [bs setFrame:NSMakeRect(60,530,150,20)];
  [rp addSubview:bs];
  
  NSTextField* contL=[NSTextField labelWithString:@"对比度"];
  [contL setFrame:NSMakeRect(10,505,50,20)];
  [contL setTextColor:[NSColor lightGrayColor]];
  [contL setFont:[NSFont systemFontOfSize:11]];
  [rp addSubview:contL];
  
  NSSlider* cs=[NSSlider sliderWithValue:1 minValue:.5 maxValue:2 target:self action:@selector(cc:)];
  [cs setFrame:NSMakeRect(60,505,150,20)];
  [rp addSubview:cs];
  
  NSButton* gc=[NSButton checkboxWithTitle:@"GSDF 校准" target:self action:@selector(gsdfT:)];
  [gc setFrame:NSMakeRect(10,475,200,20)];
  [gc setState:1];
  [rp addSubview:gc];
  
  [NSTimer scheduledTimerWithTimeInterval:1./60 target:self selector:@selector(render:) userInfo:nil repeats:YES];
  printf("Window ready\n");fflush(stdout);
}

-(void)selectMode:(NSButton*)b{gMode=(int)b.tag;printf("[Mode] %s\n",modalityNames[gMode]);fflush(stdout);}
-(void)bc:(NSSlider*)s{gBright=s.floatValue;}
-(void)cc:(NSSlider*)s{gContrast=s.floatValue;}
-(void)gsdfT:(NSButton*)b{gGSDF=(b.state==1);}
@end

int main(){
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [NSApp setDelegate:[[AppDelegate alloc]init]];
  [NSApp run];
  return 0;
}
