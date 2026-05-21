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

        kernel void process_frame(texture2d<float, access::read> input [[texture(0)]],
                                  texture2d<float, access::write> output [[texture(1)]],
                                  constant PipelineParams& params [[buffer(0)]],
                                  uint2 gid [[thread_position_in_grid]]) {
            if (gid.x >= params.width || gid.y >= params.height) return;
            float4 pixel = input.read(gid);
            float3 rgb = pixel.rgb;
            if (params.brightness != 0.0 || params.contrast != 1.0) {
                rgb = (rgb - 0.5) * params.contrast + 0.5 + params.brightness;
                rgb = clamp(rgb, 0.0, 1.0);
            }
            if (params.enableGsdf == 1) {
                float gray = rgb2gray(rgb);
                float gsdf_val = gsdfTransform(gray, 1);
                rgb *= (gsdf_val / max(gray, 0.001));
                rgb = clamp(rgb, 0.0, 1.0);
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
    }

    func render(appState: AppState, in view: MTKView) {
        guard let drawable = view.currentDrawable,
              let commandBuffer = commandQueue.makeCommandBuffer() else {
            return
        }

        let width = view.drawableSize.width
        let height = view.drawableSize.height

        // 创建纹理描述符
        let textureDescriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm,
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
            let region = MTLRegion(origin: MTLOrigin(x: 0, y: 0, z: 0),
                                   size: MTLSize(width: cgImage.width, height: cgImage.height, depth: 1))
            let bytesPerRow = cgImage.width * 4
            var pixelData = [UInt8](repeating: 0, count: bytesPerRow * cgImage.height)
            let colorSpace = CGColorSpaceCreateDeviceRGB()
            guard let context = CGContext(pixelData: &pixelData,
                                         width: cgImage.width,
                                         height: cgImage.height,
                                         bitsPerComponent: 8,
                                         bytesPerRow: bytesPerRow,
                                         space: colorSpace,
                                         bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
                return
            }
            context.draw(cgImage, in: CGRect(x: 0, y: 0, width: cgImage.width, height: cgImage.height))
            inputTexture.replace(region: MTLRegion(origin: MTLOrigin(x: 0, y: 0, z: 0),
                                                    size: MTLSize(width: min(cgImage.width, Int(width)),
                                                                 height: min(cgImage.height, Int(height)),
                                                                 depth: 1)),
                                mipmapLevel: 0,
                                withBytes: pixelData,
                                bytesPerRow: bytesPerRow)
        } else {
            // 生成测试图案
            generateTestPattern(texture: inputTexture, width: Int(width), height: Int(height), appState: appState)
        }

        // 设置参数
        var params = PipelineParams(
            width: UInt32(width),
            height: UInt32(height),
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
    }

    private func generateTestPattern(texture: MTLTexture, width: Int, height: Int, appState: AppState) {
        // 生成测试图案（同心圆）
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        let centerX = width / 2
        let centerY = height / 2
        let radius = min(width, height) / 3

        for y in 0..<height {
            for x in 0..<width {
                let dx = Float(x - centerX) / Float(radius)
                let dy = Float(y - centerY) / Float(radius)
                let dist = sqrt(dx * dx + dy * dy)
                let value = UInt8(max(0, min(255, Int((1.0 - dist) * 255))))
                let idx = (y * width + x) * 4
                pixels[idx] = value     // R
                pixels[idx + 1] = value // G
                pixels[idx + 2] = value // B
                pixels[idx + 3] = 255   // A
            }
        }

        let region = MTLRegion(origin: MTLOrigin(x: 0, y: 0, z: 0),
                               size: MTLSize(width: width, height: height, depth: 1))
        texture.replace(region: region, mipmapLevel: 0, withBytes: pixels, bytesPerRow: width * 4)
    }
}