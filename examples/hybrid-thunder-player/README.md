# 混合播放器示例 | Thunder鉴权 + ThunderStone解密 + libmedia硬解码

## 🎯 设计目标

最大化复用ThunderWebPlayer的成功经验，结合libmedia的硬解码能力：

```
ThunderWebPlayer优势          +     libmedia优势
─────────────────────────────────────────────────
✅ 成熟的鉴权机制(initSDK)    +    ✅ WebCodecs硬解码
✅ ThunderStone WASM解密      +    ✅ 完善的解封装(avformat)
✅ 稳定的数据流控制          +    ✅ 优秀的渲染管线
```

## 📁 目录结构

```
hybrid-thunder-player/
├── README.md                          # 本文档
├── index.html                         # 主页面：UI + 集成逻辑
├── ThunderAuthAdapter.js              # Thunder鉴权适配器
├── HybridThunderStoneIOLoader.js      # 混合IOLoader（核心）
└── test-urls.json                     # 测试URL配置
```

## 🔧 核心组件

### 1. ThunderAuthAdapter（鉴权适配器）
- 复用Thunder SDK的initSDK鉴权机制
- 提供统一的鉴权接口
- 管理鉴权状态

### 2. HybridThunderStoneIOLoader（核心IOLoader）
- 继承`AVPlayer.IOLoader.CustomIOLoader`
- 集成ThunderStone WASM解密能力
- 实现透明解密：网络数据 → 解密 → libmedia

### 3. AVPlayer播放器
- 使用libmedia的AVPlayer
- 启用WebCodecs硬解码
- 自动选择最优WASM解码器

## 🚀 使用流程

```javascript
// 1. 初始化鉴权
const authAdapter = new ThunderAuthAdapter()
await authAdapter.init({
  appid: 'xxx',
  uid: 'xxx', 
  sdk_sn: 'xxx'
})

// 2. 创建混合IOLoader
const ioLoader = new HybridThunderStoneIOLoader({
  url: 'https://example.com/encrypted.ts',
  thunderModule: window.ThunderModule,
  authAdapter: authAdapter
})

// 3. 使用libmedia播放器
const player = new AVPlayer({
  container: document.getElementById('container')
})

await player.load(ioLoader)
await player.play()
```

## 📊 数据流向

```
网络请求 → ThunderStone解密 → libmedia解封装 → WebCodecs硬解码 → 渲染
   ↑            ↑                    ↑                ↑            ↑
 Fetch      WASM模块           avformat模块      WebCodecs API   Canvas
```

## ✨ 技术亮点

1. **零侵入集成**：不修改libmedia和Thunder SDK源码
2. **最优性能**：硬解码 + WASM解密并行
3. **完整复用**：Thunder鉴权 + 解密能力完全保留
4. **易于扩展**：符合libmedia的CustomIOLoader规范

## 🧪 测试

```bash
# 启动本地服务
pnpm run server

# 访问测试页面
http://localhost:9527/examples/hybrid-thunder-player/
```
