#!/bin/bash

# Thunder WASM模块重新编译和部署脚本

set -e  # 遇到错误立即退出

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
WASM_OUTPUT_DIR="/Users/vincentyang/Documents/Github/libmedia/public/wasm"
EXAMPLE_DIR="/Users/vincentyang/Documents/Github/libmedia/libmedia/examples/hybrid-thunder-player"

echo "🔧 开始重新编译Thunder WASM模块..."

# 1. 进入构建目录
cd "${BUILD_DIR}"

# 2. 清理旧的构建（可选）
# make clean

# 3. 重新编译
echo "📦 编译WASM模块..."
make -j4

# 4. 检查编译是否成功
if [ ! -f "${WASM_OUTPUT_DIR}/thunder_module.wasm" ]; then
    echo "❌ 编译失败：找不到thunder_module.wasm"
    exit 1
fi

echo "✅ 编译成功！"

# 5. 复制到示例目录
echo "📋 复制文件到示例目录..."
cp -f "${WASM_OUTPUT_DIR}/thunder_module.wasm" "${EXAMPLE_DIR}/"
cp -f "${WASM_OUTPUT_DIR}/thunder_module.js" "${EXAMPLE_DIR}/"

# 6. 显示文件信息
echo ""
echo "✅ 部署完成！文件信息："
ls -lh "${EXAMPLE_DIR}/thunder_module.wasm"
ls -lh "${EXAMPLE_DIR}/thunder_module.js"

echo ""
echo "🎉 全部完成！现在可以刷新浏览器测试了。"
