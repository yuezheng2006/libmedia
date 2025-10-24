#!/bin/bash

# 确保脚本中的错误会导致脚本终止
set -e

# 加载Emscripten环境变量
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/setup_env.sh"

# 获取项目根目录（相对于当前脚本的位置）
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../../../" && pwd)"

# 确保player/public/wasm目录存在
mkdir -p "$SCRIPT_DIR/../../../public/wasm"

# 定义WASM输出目录（绝对路径）
PLAYER_WASM_DIR="$(cd "$SCRIPT_DIR/../../../public/wasm" && pwd)"
WEBPLAYER_VUE_JS_DIR="$(cd "$PROJECT_ROOT/packages/webplayer-vue/public/js" && mkdir -p . && pwd)"

# 定义player/public/js目录（绝对路径）
PLAYER_JS_DIR="$(cd "$SCRIPT_DIR/../../../public/js" && pwd)"

echo "WASM输出目录:"
echo "- Player: $PLAYER_WASM_DIR"
echo "- Webplayer-Vue: $WEBPLAYER_VUE_JS_DIR"
echo "- Player JS目录: $PLAYER_JS_DIR"

# 创建构建目录
BUILD_DIR="build"
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# 检查环境变量是否正确设置
if [ -z "$EMSDK" ]; then
  echo "Error: Emscripten SDK环境变量未设置，脚本setup_env.sh可能未正确执行"
  exit 1
fi

echo "使用Emscripten SDK路径: $EMSDK"

# 使用CMake配置项目
echo "正在配置项目..."
emcmake cmake ..

# 编译项目
echo "正在编译项目..."
emmake make

# 返回上级目录
cd ..

# 确认文件存在
if [ ! -f "$PLAYER_WASM_DIR/thunder_module.js" ]; then
  echo "错误: 编译后的JS文件不存在: $PLAYER_WASM_DIR/thunder_module.js"
  exit 1
fi

if [ ! -f "$PLAYER_WASM_DIR/thunder_module.wasm" ]; then
  echo "警告: 编译后的WASM文件不存在: $PLAYER_WASM_DIR/thunder_module.wasm"
  echo "可能是WASM文件已被内嵌到JS文件中"
fi

# Demo项目已删除，跳过复制

# 复制WASM文件到webplayer-vue项目中
echo "正在将文件复制到Webplayer-Vue项目..."
cp -f "$PLAYER_WASM_DIR/thunder_module.js" "$WEBPLAYER_VUE_JS_DIR/"

# 如果存在WASM文件，也复制它
if [ -f "$PLAYER_WASM_DIR/thunder_module.wasm" ]; then
  cp -f "$PLAYER_WASM_DIR/thunder_module.wasm" "$WEBPLAYER_VUE_JS_DIR/"
  echo "已复制WASM文件到Webplayer-Vue项目"
fi

# 复制JS文件到webplayer-vue项目中
echo "正在将所有JS文件复制到Webplayer-Vue项目..."

# 要复制的JS文件列表
JS_FILES=(
  "common.js"
  "decoder.js"
  "downloader.js"
  "http_bridge.js"
  "pcm-player.js"
  "player.js"
  "webgl.js"
)

# 遍历并复制所有JS文件
for js_file in "${JS_FILES[@]}"; do
  echo "复制 $js_file 到 Webplayer-Vue项目..."
  cp -f "$PLAYER_JS_DIR/$js_file" "$WEBPLAYER_VUE_JS_DIR/"
  
  # 验证复制结果
  if [ -f "$WEBPLAYER_VUE_JS_DIR/$js_file" ]; then
    echo "- $js_file 复制成功!"
  else
    echo "错误: $js_file 复制失败!"
    exit 1
  fi
done

# 验证Webplayer-Vue复制结果

if [ -f "$WEBPLAYER_VUE_JS_DIR/thunder_module.js" ]; then
  echo "JS文件复制到Webplayer-Vue项目成功!"
else
  echo "错误: JS文件复制到Webplayer-Vue项目失败!"
  exit 1
fi

if [ -f "$PLAYER_WASM_DIR/thunder_module.wasm" ] && [ ! -f "$WEBPLAYER_VUE_JS_DIR/thunder_module.wasm" ]; then
  echo "错误: WASM文件复制到Webplayer-Vue项目失败!"
  exit 1
fi

echo "构建完成！WebAssembly文件位于以下目录:"
echo "- Player: $PLAYER_WASM_DIR"
echo "- Webplayer-Vue: $WEBPLAYER_VUE_JS_DIR"
echo "所有JS文件已成功复制到: $WEBPLAYER_VUE_JS_DIR" 