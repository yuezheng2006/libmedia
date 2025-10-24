# ThunderStone解密播放故障排查指南

## 🔍 常见问题诊断

### 问题1：页面加载后没有继续

**症状**：
- 日志显示"加载文件: xxx"后停止
- 没有错误提示
- 播放按钮保持禁用

**可能原因**：

1. **CORS问题**
   - 加密视频URL可能不允许跨域访问
   - 检查浏览器控制台是否有CORS错误

2. **WASM解码器缺失**
   - H.264/AAC解码器WASM文件不存在
   - 检查 `dist/decode/h264.wasm` 是否存在

3. **AVPlayer初始化失败**
   - customLoader参数类型不匹配
   - container元素不存在

### 解决步骤

#### 步骤1：检查浏览器控制台

打开浏览器开发者工具（F12），查看Console标签页：

```
可能的错误信息：
- CORS policy blocked...
- Failed to fetch...
- Cannot read property...
- WASM module not found...
```

#### 步骤2：验证文件存在

```bash
# 检查WASM解码器
ls -la dist/decode/h264.wasm
ls -la dist/decode/aac.wasm

# 如果不存在，需要构建
pnpm run build-avplayer
```

#### 步骤3：测试简单URL

尝试使用公开的测试视频：

```
https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8
```

如果这个可以播放，说明解密逻辑有问题。
如果这个也不能播放，说明AVPlayer配置有问题。

#### 步骤4：检查ThunderStone解密

在浏览器控制台手动测试：

```javascript
// 检查WASM模块
console.log(typeof Module)
console.log(Module._tsInitDecrypt)

// 测试初始化
const handle = Module._tsInitDecrypt()
console.log('Handle:', handle)

// 测试头部检查
const buffer = new Uint8Array(512)
const ptr = Module._malloc(512)
Module.HEAPU8.set(buffer, ptr)
const result = Module._tsCheckDecrypt(handle, ptr, 512)
console.log('Check result:', result) // -3表示加密，0表示明文
Module._free(ptr)
Module._tsDeinitDecrypt(handle)
```

### 问题2：CORS错误

**错误信息**：
```
Access to fetch at 'https://qnktv.ktvdaren.com/...' from origin 
'http://localhost:57742' has been blocked by CORS policy
```

**解决方案**：

1. **使用代理服务器**
   ```javascript
   const proxyUrl = 'https://cors-anywhere.herokuapp.com/'
   const url = proxyUrl + originalUrl
   ```

2. **配置服务器CORS**
   - 需要目标服务器支持
   - 添加 `Access-Control-Allow-Origin` 响应头

3. **使用本地文件**
   - 下载加密文件到本地
   - 使用相对路径访问

### 问题3：解密失败

**症状**：
- 日志显示"流类型 = 加密"
- 但播放器报错或画面花屏

**检查点**：

1. **验证解密算法**
   ```javascript
   // 在read()方法中添加日志
   console.log('Decrypting block:', {
     offset: Number(this.currentPos),
     blockSize,
     result
   })
   ```

2. **检查内存管理**
   - malloc/free是否配对
   - bufferPtr是否正确释放

3. **验证数据完整性**
   ```javascript
   // 解密前后对比
   const before = new Uint8Array(buffer).slice(0, 16)
   // ... 解密 ...
   const after = new Uint8Array(buffer).slice(0, 16)
   console.log('Before:', before)
   console.log('After:', after)
   ```

### 问题4：AVPlayer事件未触发

**症状**：
- load()调用成功
- 但'loaded'事件never触发

**原因**：
- 可能customLoader接口不完整
- 可能数据格式错误

**调试**：

```javascript
// 添加所有事件监听
const events = ['loadstart', 'loaded', 'canplay', 'playing', 'error', 
                'timeupdate', 'seeking', 'seeked', 'ended']

events.forEach(event => {
  this.player.on(event, (...args) => {
    console.log(`Event: ${event}`, args)
    this.log(`事件: ${event}`, 'info')
  })
})
```

### 问题5：视频播放但无画面

**症状**：
- 日志显示播放中
- container是黑屏

**检查**：

1. **container元素**
   ```javascript
   const container = document.getElementById('videoContainer')
   console.log('Container:', container)
   console.log('Container size:', {
     width: container.offsetWidth,
     height: container.offsetHeight
   })
   ```

2. **视频尺寸**
   ```css
   #videoContainer {
     width: 100%;
     height: 100%;
     background: #000;
   }
   ```

3. **渲染模式**
   ```javascript
   // 尝试不同渲染模式
   enableWebGPU: false,
   enableWebCodecs: true
   ```

## 🛠️ 调试技巧

### 启用详细日志

```javascript
// 在IntegratedPlayer中添加
AVPlayer.setLogLevel(1) // 0=ERROR, 1=WARN, 2=INFO, 3=DEBUG
```

### 监控数据流

```javascript
// 在SimpleHTTPLoader.read()中
async read(buffer) {
  const result = await this.reader.read()
  console.log('HTTP read:', {
    done: result.done,
    size: result.value?.length,
    pos: this.pos
  })
  // ... 其余代码
}
```

### 检查解密状态

```javascript
// 在ThunderStoneIOLoader.read()中
async read(buffer) {
  const bytesRead = await this.baseLoader.read(buffer)
  
  console.log('Decrypt read:', {
    isEncrypted: this.isEncrypted,
    bytesRead,
    currentPos: Number(this.currentPos)
  })
  
  // ... 解密逻辑
}
```

## 📞 获取帮助

如果以上方法都无法解决问题，请提供以下信息：

1. **浏览器控制台完整错误信息**
2. **运行日志（右侧日志面板）**
3. **测试的视频URL**
4. **浏览器版本和操作系统**
5. **Network标签页的请求详情**

---

*更新时间: 2025-10-24*
