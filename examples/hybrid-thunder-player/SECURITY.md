# 🔒 混合播放器安全性说明

## 安全架构

### ✅ 核心安全保障

1. **解密算法隔离**
   - AES-128-CBC算法完全在WASM二进制内
   - 密钥派生逻辑封装在WASM中
   - JavaScript层无法访问密钥和算法细节

2. **最小权限原则**
   - JavaScript层只负责数据搬运
   - WASM层独占解密权限
   - 播放器层只接收明文数据

3. **内存隔离**
   ```
   JavaScript堆内存  ←→  WASM线性内存  ←→  播放器内存
        ↑                   ↑                 ↑
   对齐缓冲管理          解密执行           解码播放
   ```

---

## 安全性对比

| 安全维度 | 软解方案 | 混合方案 | 差异 |
|---------|---------|---------|------|
| 解密算法保护 | ✅ WASM | ✅ WASM | 无差异 |
| 密钥管理 | ✅ WASM | ✅ WASM | 无差异 |
| 明文数据可见性 | ⚠️ WASM内存 | ⚠️ JS堆+WASM内存 | **略低** |
| 调试信息泄露 | ⚠️ C日志 | ⚠️ JS日志 | **相同风险** |

---

## ⚠️ 已知风险点

### 1. **明文数据在JavaScript内存中可见**

**风险描述**：
- 解密后的TS流明文数据存在JavaScript Uint8Array中
- 通过Chrome DevTools的Memory Profiler可以查看堆快照
- 有经验的攻击者可能提取明文片段

**缓解措施**：
- 明文数据生命周期很短（立即传给播放器后释放）
- 使用WebCodecs硬解码，数据快速进入GPU不可访问区域
- 生产环境禁用Source Maps和调试器

### 2. **调试日志泄露信息**

**风险代码**：
```javascript
// ❌ 开发环境（泄露16字节明文）
console.log(`📤 [解密] 解密后: ${afterHex}`)

// ✅ 生产环境（关闭调试日志）
if (this.debug) {
  console.log(`📤 [解密] 解密后: ${afterHex}`)
}
```

**缓解措施**：
- 添加`debug`开关，生产环境默认`false`
- 构建时使用terser删除所有`console.*`调用

---

## 🛡️ 生产环境加固

### 1. 关闭调试日志

```javascript
const loader = new HybridThunderStoneIOLoader({
  url: 'https://example.com/video.ts',
  thunderModule: window.ThunderModule,
  debug: false  // ← 生产环境必须关闭
})
```

### 2. 代码混淆

```json
// webpack.config.js
{
  optimization: {
    minimizer: [
      new TerserPlugin({
        terserOptions: {
          compress: {
            drop_console: true,  // 删除所有console调用
            pure_funcs: ['console.log', 'console.debug']
          },
          mangle: {
            properties: {
              regex: /^_/  // 混淆私有属性
            }
          }
        }
      })
    ]
  }
}
```

### 3. Content Security Policy

```html
<meta http-equiv="Content-Security-Policy" content="
  default-src 'self';
  script-src 'self' 'wasm-unsafe-eval';
  connect-src 'self' https://qnktv.ktvdaren.com;
  media-src blob: https:;
">
```

### 4. 禁用DevTools（可选）

```javascript
// 检测DevTools打开
(function() {
  const threshold = 160
  setInterval(() => {
    if (window.outerWidth - window.innerWidth > threshold ||
        window.outerHeight - window.innerHeight > threshold) {
      // DevTools已打开，执行保护措施
      document.body.innerHTML = '调试工具已被禁用'
    }
  }, 1000)
})()
```

---

## 📊 安全性总结

### **核心加密安全等级：与软解方案相同**

✅ **保持一致的安全要素**：
- AES-128-CBC算法
- 密钥派生机制
- 加密头解析逻辑
- Thunder鉴权体系

⚠️ **略微降低的安全要素**：
- 明文数据在JS堆内存中短暂可见
- 调试日志可能泄露信息（可关闭）

### **整体评估：适用于商业级加密内容保护**

混合方案的安全性**略低于**纯WASM方案，但**远高于**明文传输。对于以下场景**完全适用**：

✅ 付费点播内容保护  
✅ 会员专享内容加密  
✅ 企业内部培训视频  
✅ 在线教育课程加密  

❌ **不适用场景**：
- 军事级机密视频
- 涉及国家安全的内容
- 需要DRM级别保护的好莱坞大片

---

## 🔐 进一步增强（可选）

如需更高安全性，可考虑：

1. **全WASM方案**：将libmedia也编译到WASM，实现完全隔离
2. **EME + DRM**：集成Widevine/PlayReady等商业DRM方案
3. **TEE集成**：利用硬件可信执行环境（需浏览器支持）

---

**最后更新**：2025-01-XX  
**安全审计**：建议每季度重新评估
