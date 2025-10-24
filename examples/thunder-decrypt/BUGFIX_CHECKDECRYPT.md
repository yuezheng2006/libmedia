# ThunderStone tsCheckDecrypt 返回值逻辑修复

## 问题发现

在集成ThunderStone解密功能时，发现 `tsCheckDecrypt` 的返回值逻辑与常规错误码约定**完全相反**。

## 错误的理解（修复前）

```javascript
// ❌ 错误的实现
const checkResult = thunderModule._tsCheckDecrypt(handle, bufferPtr, 512)
this.isEncrypted = (checkResult === -3)  // 错误！
```

我们最初认为：
- 返回 `-3` 表示加密流
- 返回 `0` 表示明文流

## 正确的逻辑（修复后）

通过查看ThunderStone WASM源码 (`tsDecrypt.c:62-66`)：

```c
int tsCheckDecrypt(void *handle, unsigned char *buffer, int size)
{
    // ... 省略前面的代码 ...
    
    GetDeHeadInfo(&tdDecrypt->cryptService, (unsigned char *) buffer, &tdDecrypt->FileHeadInfo);
    av_log(NULL, AV_LOG_INFO,"thunderstone encrypt is %d ", tdDecrypt->FileHeadInfo.isEncrypt);

    if (tdDecrypt->FileHeadInfo.isEncrypt > 0)
        return 0;   // ✅ 加密返回 0
    return -3;      // ✅ 明文返回 -3
}
```

**正确的返回值含义**：
- **返回 `0`** = 加密流（需要解密）
- **返回 `-3`** = 明文流（不需要解密）

## 修复的文件

### 1. HybridThunderStoneIOLoader.js
```javascript
// ✅ 正确的实现
const checkResult = this.thunderModule._tsCheckDecrypt(this.decryptHandle, bufferPtr, 512)
this.isEncrypted = (checkResult === 0)  // 修复：0 表示加密
```

### 2. full-demo.html
```javascript
// ✅ 正确的实现
const checkResult = this.thunderModule._tsCheckDecrypt(this.decryptHandle, bufferPtr, 512)
this.isEncrypted = (checkResult === 0)  // 修复：0 表示加密
```

### 3. ThunderStoneDecryptor.ts
```typescript
// ✅ 正确的实现
const result = this.module._tsCheckDecrypt(this.handle, bufferPtr, 512)
// ⚠️ 注意：ThunderStone逻辑相反：
// result == 0 表示加密流
// result == -3 表示明文流
this.isEncrypted = (result === 0)
```

## 为什么会有这个问题？

ThunderStone加密库的设计逻辑与常规的错误码约定相反：
- **常规约定**：成功返回0，错误返回负数
- **ThunderStone**：加密（需要特殊处理）返回0，明文（正常情况）返回-3

这可能是因为在解密库的设计中，将"需要解密"视为一种特殊的成功状态，而"不需要解密"视为一种错误或异常状态。

## 影响范围

此问题导致：
1. ✅ 所有加密的TS文件被误判为明文，解密失败
2. ✅ H.264解码器报错 "Error splitting the input into NAL units"
3. ✅ 内存分配错误 "can not alloc buffer"

修复后，加密流可以正确识别并解密播放。

## 测试验证

使用加密TS文件测试：
- URL: `https://qnktv.ktvdaren.com/tempmls/convert/20250407/4115794.ts`
- 文件大小: 187502876 bytes
- 头部前32字节: `6d d4 8f fc b5 c3 e9 a4 c5 fb 9c db b9 53 47 93 24 80 a9 2a b8 07 00 a4 d5 35 ea 79 fa 04 88 8b`

**修复前**：
- checkResult: `0`
- 判定: 明文流 ❌
- 结果: 解码失败

**修复后**：
- checkResult: `0`
- 判定: 加密流 ✅
- 结果: 正常解密播放

## 经验教训

1. **不要假设第三方库遵循常规约定**
   - 务必查看源码确认返回值含义
   - 特别是涉及加密、安全等敏感领域的库

2. **加强日志输出**
   - 输出原始返回值
   - 输出判定逻辑和结果
   - 便于快速定位问题

3. **参考官方实现**
   - ThunderWebPlayer软解方案中的decoder.c正确使用了这个API
   - 遇到问题时应首先查看官方示例代码

## 日期

2025-01-24
