#!/bin/bash
# 重新编译Thunder WASM decoder并自动复制到示例目录

set -e

SCRIPT_DIR="$(dirname "$0")"
cd "$SCRIPT_DIR/build"

echo "🔨 开始编译WASM模块..."
cmake --build . --target thunder_module

echo "✅ 编译完成！"
echo "📦 输出文件: thunder_module.js 和 thunder_module.wasm"

# 自动复制到examples目录
# 直接使用绝对路径
SOURCE_DIR="/Users/vincentyang/Documents/Github/libmedia/public/wasm"
EXAMPLE_DIR="/Users/vincentyang/Documents/Github/libmedia/libmedia/examples/hybrid-thunder-player"

echo ""
echo "📋 自动复制文件..."
echo "   源目录: $SOURCE_DIR"
echo "   目标目录: $EXAMPLE_DIR"

if [ -d "$EXAMPLE_DIR" ] && [ -d "$SOURCE_DIR" ]; then
    cp -f "$SOURCE_DIR/thunder_module.js" "$EXAMPLE_DIR/"
    cp -f "$SOURCE_DIR/thunder_module.wasm" "$EXAMPLE_DIR/"
    echo "✅ 文件复制完成！"
    echo ""
    echo "🎯 现在可以刷新浏览器测试了"
else
    echo "⚠️  找不到目录"
    echo "   SOURCE_DIR exists: $([ -d "$SOURCE_DIR" ] && echo YES || echo NO)"
    echo "   EXAMPLE_DIR exists: $([ -d "$EXAMPLE_DIR" ] && echo YES || echo NO)"
fi
