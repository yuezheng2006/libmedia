#!/bin/bash

# 简化的构建脚本 - 只编译，不复制
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 创建构建目录
BUILD_DIR="build"
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# 使用CMake配置
echo "正在配置项目..."
emcmake cmake ..

# 编译
echo "正在编译项目..."
emmake make

cd ..

echo "✅ 编译完成！WASM文件位于: $SCRIPT_DIR"
ls -lh thunder_module.*
