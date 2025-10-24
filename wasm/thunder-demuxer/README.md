# Thunder Player WebAssembly 模块

这个目录包含了Thunder Player的WebAssembly本地模块代码。

## 目录结构

- `thunder_module.c` - 主模块C代码
- `http_client.c` - HTTP客户端实现
- `http_client.h` - HTTP客户端接口定义
- `CMakeLists.txt` - CMake构建配置文件
- `build.sh` - 构建脚本

## 编译前的准备

编译WebAssembly模块需要安装Emscripten工具链：

1. 安装Emscripten: https://emscripten.org/docs/getting_started/downloads.html
2. 设置Emscripten环境变量:

```bash
source /path/to/emsdk/emsdk_env.sh
```

## 编译方法

### 手动编译

在当前目录下运行:

```bash
./build.sh
```

这个脚本会自动完成以下工作:
1. 创建必要的输出目录
2. 配置CMake项目
3. 编译WebAssembly模块
4. 将编译好的文件复制到正确的位置

### 通过npm脚本编译

项目提供了几个npm脚本来简化编译过程:

- 在根目录运行: `npm run build:wasm`
- 在player包目录运行: `npm run build:wasm`

## 输出文件

WebAssembly模块会被编译为:
- JavaScript包装器: `thunder_module.js`
- WebAssembly二进制文件: 内嵌在JS文件中的base64编码

这些文件会被放置在以下位置:
- Player包: `packages/player/public/wasm/`
- Demo包: `packages/demo/public/wasm/`

## 使用方法

在JavaScript代码中，通过以下路径引用WebAssembly模块:

```javascript
import { DEFAULT_MODULE_PATH } from '@thunderwebplayer/player/src/wasm';

// 加载WASM模块
const loader = WasmLoader.getInstance();
loader.loadModule({
  wasmPath: DEFAULT_MODULE_PATH,
  onLoaded: () => {
    // WASM模块加载完成后的回调
  }
});
```

## 导出的函数

WebAssembly模块导出以下函数:

- `_js_get_version()` - 获取模块版本号
- `_js_http_post_request()` - 发送HTTP POST请求
- `_js_init_auth()` - 初始化授权

这些函数通过`WasmApi`类封装，可以通过以下方式使用:

```javascript
import { wasmApi } from '@thunderwebplayer/player/src/wasm';

// 获取版本号
const version = wasmApi.getVersion();

// 发送HTTP请求
const response = await wasmApi.httpPost(url, data);

// 初始化授权
const authResult = await wasmApi.initAuth(params);
``` 