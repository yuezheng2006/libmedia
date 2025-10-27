# Chrome DevTools 测试指南

## 🚀 快速开始

### 1. 启动本地服务器

```bash
cd /Users/vincentyang/Documents/Github/libmedia/libmedia

# 方法1: 使用http-server (推荐)
npx http-server . -p 8080 --cors

# 方法2: 使用Python
python3 -m http.server 8080

# 方法3: 使用Node.js serve
npx serve -p 8080 --cors
```

### 2. 浏览器访问

打开Chrome浏览器，访问：
```
http://localhost:9527/examples/hybrid-thunder-player/
```

---

## 🧪 测试步骤

### 阶段1: Thunder鉴权测试

1. **打开DevTools**
   - 按 `F12` 或 `Cmd+Option+I` (Mac)
   - 切换到 **Console** 标签

2. **点击"初始化系统"按钮**

3. **观察日志输出**
   ```
   预期输出：
   ✅ Thunder HTTP Bridge已加载
   🚀 开始初始化混合播放器系统...
   📦 检查依赖加载状态...
     ✓ ThunderStone WASM
     ✓ Thunder Player SDK
     ✓ libmedia AVPlayer
   🔐 初始化Thunder鉴权...
   🌐 [HTTP Bridge] POST请求: /api/wauth/init/v2
   ✅ [HTTP Bridge] 请求成功: 200, 耗时: XXXms
   ✅ Thunder WASM鉴权成功
   ✅ 系统初始化完成！
   ```

4. **检查状态面板**
   - 鉴权状态: `已鉴权` (绿色)
   - WASM模块: `已加载` (绿色)
   - 播放器: `就绪` (绿色)

**如果鉴权失败**，检查：
- 网络请求是否成功（Network标签）
- 鉴权服务器是否可访问
- appid/sdk_sn是否正确

---

### 阶段2: WASM Packet输出测试（核心验证）

1. **点击"测试WASM Packet输出"按钮**

2. **观察WASM测试日志**（在卡片的黑色日志区域）

   **预期输出** (完整流程):
   ```
   🔬 开始WASM Packet测试...
   0️⃣ 检查Thunder鉴权状态...
      ✅ Thunder鉴权成功
   1️⃣ 设置packet回调...
      ✅ 回调设置成功
   2️⃣ 初始化decoder...
      ✅ Decoder初始化成功
   3️⃣ 下载视频数据 (前1.0MB)...
      ✅ 下载完成: 1048576 bytes
   4️⃣ 发送数据到WASM...
      ✅ 首块发送成功 (524288 bytes)
   5️⃣ 打开decoder (FFmpeg demux)...
      ✅ Decoder打开成功   ← 🎯 关键：不再报错8！
   6️⃣ 发送剩余数据...
      ✅ 剩余数据发送成功 (524288 bytes)
   7️⃣ 获取stream metadata...
      Video Stream: 0, Audio Stream: 1
      Video: CodecID=27, 1920x1080   ← H.264编码
      Audio: CodecID=86018, 48000Hz, 2ch   ← MP2编码
   8️⃣ 读取前10个packets...
      📦 Packet #1: VIDEO 45123B, pts=0, keyframe=true
         数据: 00 00 00 01 67 64 00 1f ac d9 40 50 05 bb 01 6a
         H264 NAL: 7 (SPS)   ← 序列参数集
      📦 Packet #2: VIDEO 156B, pts=0, keyframe=true
         数据: 00 00 00 01 68 ee 3c 80
         H264 NAL: 8 (PPS)   ← 图像参数集
      📦 Packet #3: VIDEO 89234B, pts=0, keyframe=true
         数据: 00 00 00 01 65 88 84 00
         H264 NAL: 5 (IDR)   ← I帧
      📦 Packet #4: AUDIO 1152B, pts=0, keyframe=false
      📦 Packet #5: VIDEO 12345B, pts=3600, keyframe=false
         H264 NAL: 1 (Non-IDR)   ← P帧
      ...
   ✅ 测试完成！成功读取 10 个packets
   📊 总共输出 10 个packets到回调
   ```

3. **在Chrome DevTools Console中验证**

   打开Console标签，应该看到详细日志：
   ```javascript
   [ThunderModule] avformat_open_input success.
   [ThunderModule] Open video codec context success
   [ThunderModule] Open audio codec context success
   ```

---

### 阶段3: 安全性验证（验证明文不泄露）

**目标**：证明解密后的TS流不暴露到JavaScript

#### 测试A: 内存抓取测试

1. **打开DevTools Memory标签**

2. **在"测试WASM Packet输出"完成后，点击"Take heap snapshot"**

3. **搜索关键内容**：
   - 搜索 `0x47` (TS同步字节)
   - 搜索 `THUNDERCRYP` (加密头标识)

4. **预期结果**：
   - ❌ 找不到连续的TS包结构 (188字节对齐)
   - ✅ 只能找到零散的packet片段
   - ✅ 证明完整明文流不在JS堆内存

#### 测试B: Network抓包测试

1. **打开DevTools Network标签**

2. **刷新页面，点击"测试WASM Packet输出"**

3. **检查请求内容**：
   - 找到视频URL的请求
   - 右键 → "Copy → Copy response"
   - 粘贴到文本编辑器

4. **预期结果**：
   - ✅ 响应数据是加密的（不是0x47开头）
   - ✅ 可以看到 "THUNDERCRYP" 加密标识

#### 测试C: Sources断点测试

1. **打开DevTools Sources标签**

2. **在index.html中搜索 `packetCallback`**

3. **在回调函数设置断点**：
   ```javascript
   this.packetCallback = this.thunderModule.addFunction(
     (stream_type, dataPtr, size, pts, dts, flags) => {
       debugger;  // ← 在这里设置断点
       ...
     }
   )
   ```

4. **点击"测试WASM Packet输出"**

5. **断点触发时检查变量**：
   - `stream_type`: 0 (video) 或 1 (audio)
   - `size`: packet大小 (几百到几万字节)
   - `data`: Uint8Array (packet数据)

6. **检查packet数据**：
   ```javascript
   // 在Console中执行
   const packetData = new Uint8Array(Module.HEAPU8.buffer, dataPtr, size)
   console.log('Packet前16字节:', Array.from(packetData.slice(0, 16))
     .map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '))

   // 预期输出：
   // 0x00 0x00 0x00 0x01 0x67 ...  (H264 NAL起始码)
   ```

7. **验证结论**：
   - ✅ 单个packet无法单独播放
   - ✅ 需要收集完整GOP（包括SPS/PPS/多个NAL）才能重组
   - ✅ 攻击成本远高于方案A

---

## 🔍 常见问题排查

### 问题1: openDecoder失败，返回8

**症状**：
```
❌ openDecoder失败: 8
```

**原因**：FIFO数据不足，FFmpeg无法探测格式

**解决**：
- 已修复！确保首块发送512KB
- 检查日志是否显示 `✅ 首块发送成功 (524288 bytes)`

### 问题2: 鉴权失败

**症状**：
```
❌ Thunder鉴权失败
❌ 鉴权服务不通：认证服务器无响应
```

**排查步骤**：

1. **检查网络请求**（Network标签）
   - 找到 `/wauth/init/v2` 请求
   - 查看Status Code
   - 查看Response

2. **检查CORS**
   - 如果看到CORS错误，确保服务器启动时加了 `--cors`
   - 或在thunder-config.js中配置正确的authServer

3. **检查鉴权参数**
   ```javascript
   // ThunderAuthAdapter.js:98
   const appid = 'b72d8f35e1c7abde45f69c82d7314590'  // 检查是否正确
   ```

### 问题3: 下载失败

**症状**：
```
❌ 下载失败: 403/404
```

**排查**：
- 检查视频URL是否可访问
- 在新标签页直接访问视频URL测试
- 检查Range请求是否被服务器支持

### 问题4: Packet回调未触发

**症状**：
```
📊 总共输出 0 个packets到回调
```

**排查**：

1. **检查回调是否设置成功**
   ```javascript
   // Console中执行
   Module._js_getPacketCallbackStatus()  // 应该返回非0值
   ```

2. **检查FFmpeg demux是否成功**
   ```javascript
   // Console中执行
   Module._js_getVideoStreamIndex()  // 应该返回0
   Module._js_getAudioStreamIndex()  // 应该返回1
   ```

3. **手动触发packet读取**
   ```javascript
   // Console中执行
   for (let i = 0; i < 10; i++) {
     const ret = Module._js_readOnePacket()
     console.log('readOnePacket返回:', ret)
   }
   ```

---

## 📊 预期测试结果对比

### 修复前（openDecoder失败）

```
❌ openDecoder失败: 8
```

**原因**：死锁逻辑
- sendData(type=1) 需要 gotStreamInfo==1
- gotStreamInfo 在 openDecoder 内设置
- openDecoder 需要FIFO有数据
- 但数据因 type=1 未写入FIFO

### 修复后（成功）

```
✅ 首块发送成功 (524288 bytes)
✅ Decoder打开成功
✅ 成功读取 10 个packets
```

**修复方式**：
- 首块512KB用 type=0 触发headBuffer缓存
- openDecoder从headBuffer读取数据
- gotStreamInfo=1后，后续type=1可写入FIFO

---

## 🎯 成功标准

测试成功的标志：

1. ✅ Thunder鉴权成功（状态面板绿色）
2. ✅ WASM模块加载成功
3. ✅ openDecoder返回0（不再报错8）
4. ✅ 成功获取视频流信息（1920x1080, H.264）
5. ✅ 成功读取packets（可以看到SPS/PPS/IDR/Non-IDR）
6. ✅ Packet回调正常触发（packetCount > 0）
7. ✅ H264 NAL分析正确（NAL type 7/8/5/1）

---

## 📝 测试报告模板

测试完成后，请记录以下信息：

```markdown
## 测试环境
- Chrome版本: ___
- 操作系统: ___
- 测试时间: ___

## 测试结果

### 1. Thunder鉴权
- [ ] 成功 / [ ] 失败
- 耗时: ___ ms
- 错误信息（如有）: ___

### 2. WASM模块初始化
- [ ] 成功 / [ ] 失败
- WASM文件大小: ___

### 3. openDecoder
- [ ] 成功 / [ ] 失败（错误码: ___）
- 首块大小: ___ bytes

### 4. Packet输出
- Packet总数: ___
- Video packets: ___
- Audio packets: ___
- 首个关键帧PTS: ___

### 5. 流信息
- 视频编码: ___
- 视频分辨率: ___
- 音频编码: ___
- 音频采样率: ___

### 6. 安全性验证
- [ ] 内存中无完整TS流
- [ ] Network抓包显示加密数据
- [ ] Packet数据无法单独播放

## 问题记录
___

## 建议
___
```

---

## 下一步

测试成功后，可以尝试：

1. **完整播放测试**（需要libmedia适配）
2. **性能测试**（解密demux耗时）
3. **Seek功能测试**
4. **长视频稳定性测试**

有任何问题随时在Console中查看详细日志！
