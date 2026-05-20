import Metal
import MetalKit
import simd

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
        // Rendering logic placeholder
    }
}