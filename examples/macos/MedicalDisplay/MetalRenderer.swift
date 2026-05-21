import Metal
import MetalKit
import simd
import AppKit
import UIKit

// Pipeline params structure matching Metal shader
struct PipelineParams {
    var width: UInt32
    var height: UInt32
    var brightness: Float
    var contrast: Float
    var saturation: Float
    var enableGsdf: UInt32
    var enableBloodless: UInt32
    var bloodSuppress: Float
    var tissueEnhance: Float
    var enableSobel: UInt32
    var sobelThreshold: Float
    var mode: UInt32

    static var stride: Int { MemoryLayout<PipelineParams>.stride }
}

class MetalRenderer {
    let device: MTLDevice
    let commandQueue: MTLCommandQueue
    let pipelineState: MTLComputePipelineState

    // Frame rate tracking
    private var frameCount: Int = 0
    private var lastFpsUpdate: CFTimeInterval = 0
    var currentFps: Double = 0

    init?() {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else {
            return nil
        }
        self.device = device
        self.commandQueue = queue

        let shaderSource = """
        #include <metal_stdlib>
        using namespace metal;

        struct PipelineParams {
            uint width;
            uint height;
            float brightness;
            float contrast;
            float saturation;
            uint enableGsdf;
            uint enableBloodless;
            float bloodSuppress;
            float tissueEnhance;
            uint enableSobel;
            float sobelThreshold;
            uint mode;
        };

        float rgb2gray(float3 c) {
            return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
        }

        float gsdfTransform(float gray, uint enable) {
            if (enable == 0) return gray;
            float jnd = gray * 8.5;
            float luminance = pow(10.0, -0.6225 + 0.0820 * log(jnd) / log(10.0));
            return clamp(luminance / 40.0, 0.0, 1.0);
        }

        // HSV to RGB conversion
        float3 hsv2rgb(float3 c) {
            float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
            float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
            return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
        }

        // RGB to HSV conversion
        float3 rgb2hsv(float3 c) {
            float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
            float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
            float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
            float d = q.x - min(q.w, q.y);
            float e = 1.0e-10;
            return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
        }

        // Sobel edge detection
        float sobelEdge(texture2d<float, access::read> tex, uint2 gid, uint width, uint height, float threshold) {
            float gx = 0.0, gy = 0.0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    uint2 samplePos = uint2(clamp(int(gid.x) + dx, 0, int(width) - 1),
                                           clamp(int(gid.y) + dy, 0, int(height) - 1));
                    float gray = rgb2gray(tex.read(samplePos).rgb);

                    float3 kx = float3(-1, 0, 1);
                    float3 ky = float3(-1, 0, 1);

                    if (dx == -1) gx -= gray;
                    else if (dx == 1) gx += gray;
                    if (dy == -1) gy -= gray;
                    else if (dy == 1) gy += gray;
                }
            }
            float edge = sqrt(gx * gx + gy * gy);
            return step(threshold, edge);
        }

        // Blood suppression for surgical field
        float3 bloodlessEnhance(float3 rgb, float suppress, float enhance) {
            float3 hsv = rgb2hsv(rgb);
            // Blood color range: H = 0-30 degrees (0-0.083 in normalized)
            if (hsv.x <= 0.083 && hsv.y > 0.3) {
                // Suppress red saturation
                hsv.y *= (1.0 - suppress * 0.7);
                // Enhance tissue brightness
                hsv.z *= (1.0 + enhance * 0.2);
                rgb = hsv2rgb(hsv);
            }
            return rgb;
        }

        kernel void process_frame(texture2d<float, access::read> input [[texture(0)]],
                                  texture2d<float, access::write> output [[texture(1)]],
                                  constant PipelineParams& params [[buffer(0)]],
                                  uint2 gid [[thread_position_in_grid]]) {
            if (gid.x >= params.width || gid.y >= params.height) return;
            float4 pixel = input.read(gid);
            float3 rgb = pixel.rgb;

            // Brightness and contrast adjustment
            if (params.brightness != 0.0 || params.contrast != 1.0) {
                rgb = (rgb - 0.5) * params.contrast + 0.5 + params.brightness;
                rgb = clamp(rgb, 0.0, 1.0);
            }

            // Saturation adjustment
            if (params.saturation != 1.0) {
                float3 hsv = rgb2hsv(rgb);
                hsv.y *= params.saturation;
                rgb = hsv2rgb(hsv);
            }

            // GSDF calibration
            if (params.enableGsdf == 1) {
                float gray = rgb2gray(rgb);
                float gsdf_val = gsdfTransform(gray, 1);
                rgb *= (gsdf_val / max(gray, 0.001));
                rgb = clamp(rgb, 0.0, 1.0);
            }

            // Bloodless surgical field enhancement
            if (params.enableBloodless == 1) {
                rgb = bloodlessEnhance(rgb, params.bloodSuppress, params.tissueEnhance);
                rgb = clamp(rgb, 0.0, 1.0);
            }

            // Sobel edge detection
            if (params.enableSobel == 1) {
                float edge = sobelEdge(input, gid, params.width, params.height, params.sobelThreshold);
                rgb = mix(rgb, float3(edge), 0.5);
            }

            output.write(float4(rgb, 1.0), gid);
        }
        """

        guard let library = try? device.makeLibrary(source: shaderSource, options: nil),
              let function = library.makeFunction(name: "process_frame") else {
            return nil
        }

        do {
            self.pipelineState = try device.makeComputePipelineState(function: function)
        } catch {
            print("Shader compile error: \(error)")
            return nil
        }

        self.lastFpsUpdate = CACurrentMediaTime()
    }

    func render(appState: AppState, in view: MTKView) {
        guard let drawable = view.currentDrawable,
              let commandBuffer = commandQueue.makeCommandBuffer() else {
            return
        }

        let width = UInt32(view.drawableSize.width)
        let height = UInt32(view.drawableSize.height)

        // 创建纹理描述符
        let textureDescriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba32Float,
            width: Int(width),
            height: Int(height),
            mipmapped: false
        )
        textureDescriptor.usage = [.shaderRead, .shaderWrite]

        guard let inputTexture = device.makeTexture(descriptor: textureDescriptor),
              let outputTexture = device.makeTexture(descriptor: textureDescriptor) else {
            return
        }

        // 如果有图像数据，加载到输入纹理
        if let imagePath = appState.currentImagePath,
           let imageData = FileManager.default.contents(atPath: imagePath),
           let cgImage = UIImage(data: imageData)?.cgImage {
            loadImageToTexture(cgImage: cgImage, texture: inputTexture, width: Int(width), height: Int(height))
        } else {
            // 生成测试图案
            generateTestPattern(texture: inputTexture, width: Int(width), height: Int(height))
        }

        // 设置参数
        var params = PipelineParams(
            width: width,
            height: height,
            brightness: appState.brightness,
            contrast: appState.contrast,
            saturation: appState.saturation,
            enableGsdf: appState.enableGsdf ? 1 : 0,
            enableBloodless: appState.enableBloodless ? 1 : 0,
            bloodSuppress: 0.5,
            tissueEnhance: 0.3,
            enableSobel: 0,
            sobelThreshold: 0.3,
            mode: 0
        )

        // 创建计算命令编码器
        guard let encoder = commandBuffer.makeComputeCommandEncoder() else {
            return
        }

        encoder.setComputePipelineState(pipelineState)
        encoder.setTexture(inputTexture, index: 0)
        encoder.setTexture(outputTexture, index: 1)
        encoder.setBytes(&params, length: MemoryLayout<PipelineParams>.stride, index: 0)

        // 分发线程组
        let threadGroupSize = MTLSize(width: 16, height: 16, depth: 1)
        let threadGroups = MTLSize(
            width: (Int(width) + 15) / 16,
            height: (Int(height) + 15) / 16,
            depth: 1
        )
        encoder.dispatchThreadgroups(threadGroups, threadsPerThreadgroup: threadGroupSize)
        encoder.endEncoding()

        // 复制到 drawable
        let blitEncoder = commandBuffer.makeBlitCommandEncoder()
        blitEncoder?.copy(from: outputTexture,
                          sourceSlice: 0,
                          sourceLevel: 0,
                          sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
                          sourceSize: MTLSize(width: Int(width), height: Int(height), depth: 1),
                          to: drawable.texture,
                          destinationSlice: 0,
                          destinationLevel: 0,
                          destinationOrigin: MTLOrigin(x: 0, y: 0, z: 0))
        blitEncoder?.endEncoding()

        commandBuffer.present(drawable)
        commandBuffer.commit()

        // Update FPS counter
        updateFps()
    }

    private func loadImageToTexture(cgImage: CGImage, texture: MTLTexture, width: Int, height: Int) {
        // Create a buffer to hold the pixel data
        let bytesPerPixel = 16 // RGBA32Float = 4 * 4 bytes
        let bytesPerRow = width * bytesPerPixel
        var pixelData = [Float](repeating: 0, count: width * height * 4)

        // Create a CG context to extract pixel data
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: &pixelData,
                                       width: width,
                                       height: height,
                                       bitsPerComponent: 8,
                                       bytesPerRow: bytesPerRow,
                                       space: colorSpace,
                                       bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            return
        }

        // Draw the image into the context (scaled to fit)
        let drawRect = CGRect(x: 0, y: 0, width: width, height: height)
        context.draw(cgImage, in: drawRect)

        // Upload to Metal texture
        let region = MTLRegion(origin: MTLOrigin(x: 0, y: 0, z: 0),
                               size: MTLSize(width: width, height: height, depth: 1))
        texture.replace(region: region, mipmapLevel: 0, withBytes: pixelData, bytesPerRow: bytesPerRow)
    }

    private func generateTestPattern(texture: MTLTexture, width: Int, height: Int) {
        // Generate concentric circles pattern with Float values
        var pixels = [Float](repeating: 0, count: width * height * 4)
        let centerX = width / 2
        let centerY = height / 2
        let radius = min(width, height) / 3

        for y in 0..<height {
            for x in 0..<width {
                let dx = Float(x - centerX) / Float(radius)
                let dy = Float(y - centerY) / Float(radius)
                let dist = sqrt(dx * dx + dy * dy)
                let value: Float = max(0, min(1, 1.0 - dist))

                let idx = (y * width + x) * 4
                pixels[idx] = value     // R
                pixels[idx + 1] = value // G
                pixels[idx + 2] = value // B
                pixels[idx + 3] = 1.0   // A
            }
        }

        let region = MTLRegion(origin: MTLOrigin(x: 0, y: 0, z: 0),
                               size: MTLSize(width: width, height: height, depth: 1))
        texture.replace(region: region, mipmapLevel: 0, withBytes: pixels, bytesPerRow: width * 16)
    }

    private func updateFps() {
        frameCount += 1
        let currentTime = CACurrentMediaTime()
        let elapsed = currentTime - lastFpsUpdate

        if elapsed >= 1.0 {
            currentFps = Double(frameCount) / elapsed
            frameCount = 0
            lastFpsUpdate = currentTime
        }
    }
}