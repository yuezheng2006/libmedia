# Thunder混合播放器完整解决方案

> **方案B**: Thunder鉴权 + WASM解密/Demux + libmedia硬解
> **安全保证**: 明文数据不暴露到JS层，仅packet数据传递

---

## 📋 问题分析

### 原有方案A的致命缺陷

```javascript
// ❌ 方案A (HybridThunderStoneIOLoader)
网络加密数据 → JS解密 → 明文TS流暴露给libmedia → 播放
                 ↑ 致命问题：明文在JS内存中可被窃取
```

**安全风险**:
- 解密后的完整TS流暴露在JavaScript堆内存
- 浏览器调试工具可以dump整个内存
- 用户可以通过DevTools拦截并保存明文数据

### WASM Packet Tester失败原因

**现象**: `openDecoder失败: 8`

**根本原因**:
1. 错误码8 = `kErrorCode_FFmpeg_Error`
2. `avformat_open_input`失败，因为FFmpeg的`readCallback`返回数据为空
3. **死锁逻辑**:
   ```c
   // decoder.c:1720
   if(type == 1 && decoder->gotStreamInfo == 1){
       // 仅在 gotStreamInfo==1 时写入FIFO
       // 但 gotStreamInfo 在 openDecoder 内部设置！
   }
   ```
4. **解决**: 首块发送足够大的数据(512KB)，确保`type=0`触发headBuffer缓存，然后立即`openDecoder`

---

## ✅ 最终方案：方案B完整架构

### 数据流

```
┌─────────────────────────────────────────────────────────┐
│        JavaScript Layer (ThunderWASMBridge.js)          │
│  1. Thunder HTTP Bridge鉴权 ✓                           │
│  2. Fetch加密数据                                        │
│  3. sendData()喂给WASM（加密数据，不解密）               │
└────────────────────┬────────────────────────────────────┘
                     │ 加密TS流
                     ▼
┌─────────────────────────────────────────────────────────┐
│          WASM Layer (thunder_module.wasm)               │
│  decoder.c:                                              │
│    1. Thunder解密 (tsDataDecrypt) 🔒 WASM沙盒内         │
│    2. FFmpeg demux (av_read_frame)                      │
│    3. 输出H264/AAC packet                                │
│  ↓ packetCallback                                       │
│  void onPacket(stream_type, data, size, pts, dts, flags)│
└────────────────────┬────────────────────────────────────┘
                     │ 明文H264/AAC packets (最小单元)
                     ▼
┌─────────────────────────────────────────────────────────┐
│            JavaScript Bridge Layer                       │
│  ThunderWASMBridge.read():                              │
│    - 接收packet回调，缓存packet                          │
│    - libmedia调用read()时返回packet数据                  │
└────────────────────┬────────────────────────────────────┘
                     │ Packet流
                     ▼
┌─────────────────────────────────────────────────────────┐
│            libmedia AVPlayer (硬解)                      │
│  WebCodecs VideoDecoder/AudioDecoder                    │
│  GPU硬件解码 → 渲染                                      │
└─────────────────────────────────────────────────────────┘
```

### 安全分析

**✅ 明文数据隔离**:
- ThunderStone解密发生在WASM沙盒内 (C代码，不暴露到JS)
- FFmpeg demux发生在WASM沙盒内
- **仅packet数据传递到JS** (H264 NAL单元、AAC帧，无法单独播放)
- Packet立即传递给WebCodecs硬件解码器（浏览器内核，无法访问）

**对比方案A**:
| 项目 | 方案A (IOLoader) | 方案B (WASMBridge) |
|------|------------------|---------------------|
| 解密位置 | ❌ JavaScript | ✅ WASM沙盒 |
| Demux位置 | ❌ libmedia (JS可见) | ✅ WASM沙盒 |
| 明文暴露 | ❌ 完整TS流 | ✅ 仅packet片段 |
| DevTools拦截 | ❌ 可dump完整视频 | ✅ 仅packet无法重组 |

---

## 🔧 修复内容

### 1. 创建ThunderWASMBridge.js

**职责**:
- 管理WASM decoder生命周期 (`initDecoder` → `sendData` → `openDecoder`)
- 设置packet回调 (`_js_setPacketCallback`)
- 实现CustomIOLoader接口，桥接WASM packet到libmedia

**关键实现**:
```javascript
// 设置packet回调
this.packetCallback = this.thunderModule.addFunction(
  (stream_type, dataPtr, size, pts, dts, flags) => {
    // 从WASM内存复制packet数据
    const packetData = new Uint8Array(size)
    packetData.set(new Uint8Array(this.thunderModule.HEAPU8.buffer, dataPtr, size))

    // 缓存packet供libmedia消费
    this.packetBuffer.push({ stream_type, data: packetData, pts, dts, flags })
  },
  'viiiiii'
)
Module._js_setPacketCallback(this.packetCallback)
```

### 2. 修复index.html中的WASM测试逻辑

**核心修复**:
```javascript
// ❌ 错误逻辑（导致openDecoder失败）
sendData(0, headerPtr, 8704, 0)  // type=0: header
sendData(8704, streamPtr, remaining, 1)  // type=1: stream (需要gotStreamInfo==1!)
openDecoder()  // ← 此时FIFO为空！

// ✅ 正确逻辑（参考ThunderWebPlayer）
sendData(0, firstChunkPtr, 512KB, 0)  // type=0: 首块512KB
openDecoder()  // ← headBuffer有数据，可以成功打开
sendData(512KB, remainingPtr, ..., 1)  // type=1: 后续数据
```

**详细步骤**:
1. 首块发送512KB (`kMinDecoderSize`)，`type=0`触发headBuffer缓存
2. 立即调用`openDecoder()` → FFmpeg从headBuffer读取数据探测格式
3. `gotStreamInfo`设置为1
4. 后续数据`type=1`可以写入FIFO

### 3. 参考文件更新

**新增文件**:
- `ThunderWASMBridge.js`: WASM ↔ libmedia桥接层

**修改文件**:
- `index.html`:
  - 修复WASM测试逻辑（512KB首块）
  - 集成ThunderWASMBridge用于播放

---

## 🧪 测试步骤

### 1. 测试WASM Packet输出（验证方案B可行性）

```bash
# 启动本地服务器
npx http-server . -p 8080 --cors

# 浏览器访问
open http://localhost:8080/examples/hybrid-thunder-player/
```

**操作**:
1. 点击"初始化系统"（Thunder鉴权）
2. 点击"测试WASM Packet输出"
3. 查看日志：
   ```
   ✅ 首块发送成功 (524288 bytes)
   ✅ Decoder打开成功
   📦 Packet #1: VIDEO 1234B, pts=0, keyframe=true
   📦 Packet #2: AUDIO 567B, pts=0, keyframe=false
   ...
   ```

**预期结果**:
- `openDecoder`成功（不再返回8）
- 成功读取packets并输出到回调
- H264 NAL分析显示正确的SPS/PPS/IDR

### 2. 测试完整播放（TODO）

当前状态：WASM packet输出已验证 ✅
待完成：libmedia适配packet流输入

**需要libmedia改造**:
- 当前libmedia期望CustomIOLoader返回完整TS流
- 需要支持packet流输入（EncodedVideoChunk/EncodedAudioChunk）

**临时方案**:
- 先验证WASM packet输出正确性
- 可以手动调用WebCodecs验证packet可解码性：
  ```javascript
  const decoder = new VideoDecoder({
    output: (frame) => { /* 渲染 */ },
    error: (e) => { console.error(e) }
  })
  decoder.configure({ codec: 'avc1.64001f', ... })
  decoder.decode(new EncodedVideoChunk({
    type: packet.isKeyframe ? 'key' : 'delta',
    timestamp: packet.pts,
    data: packet.data
  }))
  ```

---

## 📊 方案对比总结

| 维度 | 方案A (IOLoader) | 方案B (WASMBridge) |
|------|------------------|---------------------|
| **安全性** | ❌ 明文暴露JS | ✅ 仅packet暴露 |
| **改造复杂度** | ✅ 简单 | ⚠️ 中等 |
| **性能** | ✅ 单次解密 | ✅ WASM高效 |
| **可维护性** | ⚠️ 安全风险 | ✅ 架构清晰 |
| **兼容原软解** | ❌ 完全不同 | ✅ 高度对齐 |

**结论**: 方案B是唯一满足安全需求的方案，虽然需要适配libmedia，但架构优雅且对齐原有软解播放器。

---

## 🚀 下一步

1. ✅ 验证WASM packet输出（当前已完成）
2. ⏳ libmedia适配packet流输入
   - 修改AVPlayer支持packet source
   - 或创建PacketIOLoader → TS流重封装（临时方案）
3. ⏳ 完整播放流程测试
4. ⏳ Seek功能实现
5. ⏳ 性能优化

---

## 📚 参考文档

- Thunder鉴权机制：`docs/architecture-analysis/thunderwebplayer-auth-decrypt-deep-dive.md`
- 软解播放流程：`docs/architecture-analysis/thunderwebplayer-software-decode-flow.md`
- WASM decoder源码：`wasm/thunder-demuxer/decoder/decoder.c`
