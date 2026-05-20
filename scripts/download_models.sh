#!/bin/bash
# scripts/download_models.sh

set -e

MODEL_DIR="$(cd "$(dirname "$0")/../sdk/ai_engine/models" && pwd)"
mkdir -p "$MODEL_DIR"

echo "下载 MonoViT 模型..."
# 使用 Monai 模型仓库 (示例 URL，需替换为实际可用 URL)
curl -L -o "$MODEL_DIR/monovit.onnx" \
    "https://github.com/Project-MONAI/model-zoo/releases/download/monovit.onnx"

echo "下载 MedCLIP 模型..."
curl -L -o "$MODEL_DIR/medclip.onnx" \
    "https://huggingface.co/medclip/medclip.onnx"

echo "模型下载完成: $MODEL_DIR"
ls -la "$MODEL_DIR"