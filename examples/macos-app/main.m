/**
 * AI Medical Display - 跨平台 macOS 应用
 * 编译: clang -fobjc-arc -o CrossPlatformApp main.m -framework Metal -framework MetalKit -framework Foundation -framework AppKit
 */
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <stdio.h>

static const char* computeShader = R"(
#include <metal_stdlib>
using namespace metal;
struct Params { uint w,h; float br,co,sa; int gsdf,blood; float bs,te,zoom,px,py; int mode; };
float3 rgb2hsv(float3 c){float4 K=float4(0,-1/3.,2/3.,-1);float4 p=mix(float4(c.bg,K.wz),float4(c.gb,K.xy),step(c.b,c.g));float4 q=mix(float4(p.xyw,c.r),float4(c.r,p.yzx),step(p.x,c.r));float d=q.x-min(q.w,q.y);return float3(abs(q.z+(q.w-q.y)/(6*d+1e-10)),d/(q.x+1e-10),q.x);}
float3 hsv2rgb(float3 c){float4 K=float4(1,2/3.,1/3.,3);float3 p=abs(fract(c.xxx+K.xyz)*6-K.www);return c.z*mix(K.xxx,clamp(p-K.xxx,0,1),c.y);}
kernel void process(texture2d<float,access::read>in[[0]],texture2d<float,access::write>out[[1]],constant Params&p[[0]],uint2 g[[thread_position_in_grid]]){
  float sx=(float(g.x)/p.zoom)+p.px,sy=(float(g.y)/p.zoom)+p.py;
  if(sx<0||sx>=p.w||sy<0||sy>=p.h){out.write(float4(.05,.05,.08,1),g);return;}
  float4 pix=in.read(uint2(uint(sx),uint(sy)));
  float3 rgb=pix.rgb;
  if(p.br!=0||p.co!=1){rgb=(rgb-.5)*p.co+.5+p.br;rgb=clamp(rgb,0,1);}
  if(p.sa!=1){float3 h=rgb2hsv(rgb);h.y*=p.sa;rgb=hsv2rgb(h);}
  if(p.gsdf){float g=.299*rgb.r+.587*rgb.g+.114*rgb.b;rgb*=(pow(10,-.6225+.082*log(g*8.5)/log(10))/40./max(g,.001);rgb=clamp(rgb,0,1);}
  if(p.blood){float3 h=rgb2hsv(rgb);if(h.x<=.083&&h.y>.3){h.y*=1-p.bs*.7;h.z*=1+p.te*.2;}rgb=hsv2rgb(h);rgb=clamp(rgb,0,1);}
  out.write(float4(rgb,1),g);
}
kernel void gen(texture2d<float,access::write>o[[0]],constant Params&p[[0]],uint2 g[[thread_position_in_grid]]){
  if(g.x>=p.w||g.y>=p.h)return;
  float x=(float)g.x/p.w,y=(float)g.y/p.h,d=sqrt(pow(x-.5,2)+pow(y-.5,2));
  float3 rgb;if(p.mode==0)rgb=float3(clamp(1-d*2,0.,1.)*.8+.1);else if(p.mode==1){float a=atan2(y-.5,x-.5);rgb=float3(sin(a*6+d*15)*.3+.5);}else if(p.mode==2)rgb=(d<.2)?float3(.85):(d<.4)?float3(.3+d*2.5):float3(.05);else if(p.mode==3)rgb=float3(.3+fract(sin(dot(float2(x,y),float2(12.9898,78.233)))*43758.5)*.2);else{float h=clamp(1-d*3,0.,1.);rgb=float3(h,h>.5?(h-.5)*2:0,h>.8?(h-.8)*5:0);}
  o.write(float4(rgb,1),g);
})";

static id<MTLDevice> dev;
static id<MTLCommandQueue> q;
static id<MTLComputePipelineState> pp,gp;
static id<MTLTexture> inTex,outTex;
static int W=800,H=600;
static float br=0,co=1,sa=1,bs=.5,te=.3,zm=1,px=0,py=0;
static int gsdf=1,bld=0,mode=0;
static const char* mnames[]={"CT","MRI","XRay","超声","PET"};

static BOOL initM(){
  dev=MTLCreateSystemDefaultDevice();if(!dev)return NO;
  q=[dev newCommandQueue];if(!q)return NO;
  NSError*e=nil;
  id<MTLLib> lib=[dev newLibraryWithSource:@(computeShader)options:nil error:&e];
  if(e){fprintf(stderr,"Shader:%s\n",[[e localizedDescription]UTF8String]);return NO;}
  pp=[dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"process"]error:&e];
  gp=[dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"gen"]error:&e];
  printf("Metal:%s\n",[[dev name]UTF8String]);
  return YES;
}

static void mkTex(){
  MTLTD*d=[MTLTD texture2DDescriptorWithPixelFormat:MTLPF_RGBA8Unorm width:W height:H mipmapped:NO];
  d.usage=MTLTUShaderRead|MTLTUShaderWrite;
  inTex=[dev newTextureWithDescriptor:d];
  outTex=[dev newTextureWithDescriptor:d];
}

static void genP(){
  struct P{int w,h;float br,co,sa;int gsdf,bld;float bs,te,zm,px,py;int mode;}p={W,H,br,co,sa,gsdf,bld,bs,te,zm,px,py,mode};
  id<MTLCB>cmd=[q commandBuffer];
  id<MTLCE>enc=[cmd computeCommandEncoder];
  [enc setComputePipelineState:gp];
  [enc setTexture:inTex atIndex:0];
  [enc setBytes:&p length:sizeof(p) atIndex:0];
  [enc dispatchThreadgroups:MTLSizeMake((W+15)/16,(H+15)/16,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
  [enc endEncoding];
  [cmd commit];[cmd waitUntilCompleted];
}

static void proc(){
  struct P{int w,h;float br,co,sa;int gsdf,bld;float bs,te,zm,px,py;int mode;}p={W,H,br,co,sa,gsdf,bld,bs,te,zm,px,py,mode};
  id<MTLCB>cmd=[q commandBuffer];
  id<MTLCE>enc=[cmd computeCommandEncoder];
  [enc setComputePipelineState:pp];
  [enc setTexture:inTex atIndex:0];
  [enc setTexture:outTex atIndex:1];
  [enc setBytes:&p length:sizeof(p) atIndex:0];
  [enc dispatchThreadgroups:MTLSizeMake((W+15)/16,(H+15)/16,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
  [enc endEncoding];
  [cmd commit];[cmd waitUntilCompleted];
}

@interface R:NSObject<MTKVD>
@end
@implementation R
-(void)mtkView:(MTKView*)v drawableSizeWillChange:(CGSize)s{}
-(void)drawInMTKView:(MTKView*)v{
  id<CAMD>d=v.currentDrawable;if(!d)return;
  genP();proc();
  id<MTLCB>cmd=[q commandBuffer];
  id<MTLBCE>blit=[cmd blitCommandEncoder];
  [blit copyFromTexture:outTex toTexture:d.texture];
  [blit endEncoding];
  [cmd presentDrawable:d];[cmd commit];
}
@end

@interface A:NSObject<NSAD>
@end
@implementation A
-(void)applicationDidFinishLaunching:(NSNotification*)n{
  printf("\n==== AI Medical Display (Cross Platform) ====\n\n");
  if(!initM()){[[NSAlert alertWithMessageText:@"Metal?" defaultButton:@"OK" alternateButton:nil informativeTextWithFormat:@"Metal required"]runModal];[NSApp terminate:nil];return;}
  mkTex();
  
  NSWindow*win=[[NSWindow alloc]initWithContentRect:NSMakeRect(100,100,1280,800) styleMask:7 backing:NSBackingStoreBuffered defer:NO];
  [win setTitle:@"AI Medical Display - Cross Platform (Metal)"];
  [win makeKeyAndOrderFront:nil];
  NSView*c=[win contentView];
  
  // Sidebar
  NSView*sb=[[NSView alloc]initWithFrame:NSMakeRect(0,0,220,740)];
  [sb setWantsLayer:YES];
  [sb.layer setBackgroundColor:[NSColor colorWithRed:.12 green:.12 blue:.15 alpha:1].CGColor];
  [c addSubview:sb];
  NSTextField*L=[NSTextField labelWithString:@"📁 影像模态"];
  [L setFrame:NSMakeRect(10,700,200,20)];
  [L setTextColor:[NSColor whiteColor]];
  [L setFont:[NSFont boldSystemFontOfSize:14]];
  [sb addSubview:L];
  NSArray*names=@[@"CT 扫描",@"MRI 影像",@"X-Ray",@"超声",@"PET"];
  for(int i=0;i<5;i++){NSButton*b=[NSButton buttonWithTitle:names[i] target:self action:@selector(selM:)];
    [b setFrame:NSMakeRect(10,660-i*35,200,28)];
    [b setTag:i];
    [sb addSubview:b];}
  
  // Metal View
  MTKView*mv=[[MTKView alloc]initWithFrame:NSMakeRect(220,0,780,740)];
  [mv setDevice:dev];
  [c addSubview:mv];
  [mv setDelegate:[[R alloc]init]];
  mv.preferredFramesPerSecond=60;
  
  // Inspector
  NSView*ins=[[NSView alloc]initWithFrame:NSMakeRect(1000,0,280,740)];
  [ins setWantsLayer:YES];
  [ins.layer setBackgroundColor:[NSColor colorWithRed:.1 green:.1 blue:.12 alpha:1].CGColor];
  [c addSubview:ins];
  
  L=[NSTextField labelWithString:@"🤖 AI 识别结果"];
  [L setFrame:NSMakeRect(10,700,260,20)];
  [L setTextColor:[NSColor whiteColor]];
  [L setFont:[NSFont boldSystemFontOfSize:14]];
  [ins addSubview:L];
  
  NSTextField*ml=[NSTextField labelWithString:@"模态: CT"];
  [ml setFrame:NSMakeRect(10,670,260,20)];
  [ml setTextColor:[NSColor colorWithRed:.3 green:.8 blue:1 alpha:1]];
  [ml setFont:[NSFont boldSystemFontOfSize:16]];
  [ins addSubview:ml];
  
  // Sliders
  NSTextField*sl=[NSTextField labelWithString:@"亮度"];
  [sl setFrame:NSMakeRect(10,620,50,20)];
  [sl setTextColor:[NSColor lightGrayColor]];
  [ins addSubview:sl];
  
  NSSlider*bsl=[NSSlider sliderWithValue:0 minValue:-.5 maxValue:.5 target:self action:@selector(brChg:)];
  [bsl setFrame:NSMakeRect(60,620,210,20)];
  [ins addSubview:bsl];
  
  sl=[NSTextField labelWithString:@"对比度"];
  [sl setFrame:NSMakeRect(10,590,50,20)];
  [sl setTextColor:[NSColor lightGrayColor]];
  [ins addSubview:sl];
  
  NSSlider*csl=[NSSlider sliderWithValue:1 minValue:.5 maxValue:2 target:self action:@selector(coChg:)];
  [csl setFrame:NSMakeRect(60,590,210,20)];
  [ins addSubview:csl];
  
  NSButton*gc=[NSButton checkboxWithTitle:@"GSDF 校准" target:self action:@selector(gsdfTgl:)];
  [gc setFrame:NSMakeRect(10,550,260,20)];
  [gc setState:1];
  [ins addSubview:gc];
  
  NSButton*bc=[NSButton checkboxWithTitle:@"无血术野增强" target:self action:@selector(bldTgl:)];
  [bc setFrame:NSMakeRect(10,525,260,20)];
  [ins addSubview:bc];
  
  sl=[NSTextField labelWithString:@"抑制级别"];
  [sl setFrame:NSMakeRect(10,490,60,20)];
  [sl setTextColor:[NSColor lightGrayColor]];
  [ins addSubview:sl];
  
  NSSlider*ssl=[NSSlider sliderWithValue:.5 minValue:0 maxValue:1 target:self action:@selector(supChg:)];
  [ssl setFrame:NSMakeRect(70,490,200,20)];
  [ins addSubview:ssl];
  
  // Status bar
  NSView*sb2=[[NSView alloc]initWithFrame:NSMakeRect(0,0,1280,55)];
  [sb2 setWantsLayer:YES];
  [sb2.layer setBackgroundColor:[NSColor colorWithRed:.08 green:.08 blue:.1 alpha:1].CGColor];
  [c addSubview:sb2];
  
  NSTextField*st=[NSTextField labelWithString:@"🟢 Metal GPU | 800x600"];
  [st setFrame:NSMakeRect(10,15,1000,25)];
  [st setTextColor:[NSColor lightGrayColor]];
  [sb2 addSubview:st];
  
  printf("✓ 应用已启动\n\n");
}
-(void)selM:(NSButton*)b{mode=b.tag;printf("[模态] %s\n",mnames[mode]);}
-(void)brChg:(NSSlider*)s{br=s.floatValue;}
-(void)coChg:(NSSlider*)s{co=s.floatValue;}
-(void)gsdfTgl:(NSButton*)b{gsdf=b.state==1;}
-(void)bldTgl:(NSButton*)b{bld=b.state==1;}
-(void)supChg:(NSSlider*)s{bs=s.floatValue;}
@end

int main(){
  @autoreleasepool{
    A*d=[[A alloc]init];
    [NSApp setActivationPolicy:0];
    [NSApp setDelegate:d];
    [NSApp run];
  }
  return 0;
}
