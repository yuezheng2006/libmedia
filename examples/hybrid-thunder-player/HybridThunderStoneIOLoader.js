/**
 * 混合ThunderStone IOLoader
 * 
 * 核心功能：
 * 1. 继承libmedia的CustomIOLoader基类
 * 2. 集成ThunderStone WASM解密能力
 * 3. 实现透明解密：网络数据 → 解密 → libmedia解封装
 * 
 * 数据流：
 * Fetch API → 读取数据 → ThunderStone解密 → 返回给libmedia
 * 
 * 使用方式：
 * const loader = new HybridThunderStoneIOLoader({
 *   url: 'https://example.com/video.ts',
 *   thunderModule: window.ThunderModule,
 *   debug: false  // 生产环境关闭调试日志
 * })
 * await player.load(loader)
 * 
 * @version 1.0.0 - Production Ready
 */

class HybridThunderStoneIOLoader extends AVPlayer.IOLoader.CustomIOLoader {
  constructor(options) {
    super() // 必须调用父类构造函数
    
    this.url = options.url
    this.thunderModule = options.thunderModule
    
    // 网络相关
    this.response = null
    this.reader = null
    this.totalSize = 0
    this.currentPosition = 0n
    
    // 解密相关
    this.decryptHandle = 0
    this.isEncrypted = false
    this.headerBuffer = null
    this.pendingBuffer = null // 缓存未消费的数据
    
    // ⚠️ 新增：8KB对齐缓冲区（累积数据直到凑够8KB再解密）
    this.alignBuffer = new Uint8Array(0)
    // 当前alignBuffer在文件中的起始位置
    this.alignBufferStartPos = 0n
    
    // 初始化解密器
    this._initDecryptor()
  }

  /**
   * 初始化ThunderStone解密器
   */
  _initDecryptor() {
    if (!this.thunderModule) {
      throw new Error('ThunderStone WASM模块未提供')
    }

    this.decryptHandle = this.thunderModule._tsInitDecrypt()
    if (!this.decryptHandle) {
      throw new Error('ThunderStone解密器初始化失败')
    }

    console.log('✅ ThunderStone解密器初始化成功, handle:', this.decryptHandle)
  }

  /**
   * CustomIOLoader必须实现的属性
   */
  get ext() {
    return this.url.split('.').pop()?.split('?')[0] || 'ts'
  }

  get flags() {
    return 0
  }

  get name() {
    return `HybridThunderStone(${this.url})`
  }

  get minBuffer() {
    return 4
  }

  /**
   * 打开数据源
   */
  async open() {
    try {
      console.log(`📂 打开数据源: ${this.url}`)

      // 1. 获取文件大小
      console.log('📡 [OPEN] 步骤1: 获取文件大小...')
      const headResp = await fetch(this.url, { method: 'HEAD' })
      const contentLength = headResp.headers.get('Content-Length')
      if (contentLength) {
        this.totalSize = parseInt(contentLength)
        console.log(`📊 文件大小: ${this.totalSize} bytes`)
      } else {
        console.warn('⚠️ 无法获取文件大小')
      }

      // 2. 开始流式读取
      console.log('📡 [OPEN] 步骤2: 开始流式读取...')
      this.response = await fetch(this.url)
      if (!this.response.ok) {
        throw new Error(`HTTP ${this.response.status}: ${this.response.statusText}`)
      }
      this.reader = this.response.body.getReader()
      this.currentPosition = 0n
      console.log('✅ [OPEN] 步骤2完成: 获取到reader')

      // 3. 读取前512字节检测加密
      console.log('📡 [OPEN] 步骤3: 检测加密状态...')
      await this._checkEncryption()
      console.log('✅ [OPEN] 步骤3完成: 加密检测完成')

      console.log(`✅ 数据源已打开, 加密状态: ${this.isEncrypted ? '加密' : '明文'}`)
      return 0
    } catch (error) {
      console.error('❌ 打开数据源失败:', error)
      console.error('❌ 错误堆栈:', error.stack)
      return -1
    }
  }

  /**
   * 检测文件是否加密
   */
  async _checkEncryption() {
    console.log('🔍 检测文件加密状态...')

    // 读取512字节头部
    this.headerBuffer = new Uint8Array(512)
    let headerRead = 0

    while (headerRead < 512) {
      const { value, done } = await this.reader.read()
      if (done) {
        console.error('❌ 文件过小，无法读取完整头部')
        return
      }

      const toCopy = Math.min(value.length, 512 - headerRead)
      this.headerBuffer.set(value.subarray(0, toCopy), headerRead)
      headerRead += toCopy

      // 如果有剩余数据，缓存起来（重要！）
      if (toCopy < value.length) {
        this.pendingBuffer = value.subarray(toCopy)
        console.log(`📦 缓存剩余数据: ${this.pendingBuffer.length} 字节`)

        // 打印pendingBuffer的前16字节（这些数据从文件512字节开始）
        const pendingHex = Array.from(this.pendingBuffer.slice(0, 16))
          .map(b => b.toString(16).padStart(2, '0')).join(' ')
        console.log(`  前16字节: ${pendingHex}`)
        console.log(`  首字节: 0x${this.pendingBuffer[0].toString(16)}`)
        if (this.pendingBuffer[0] === 0x47) {
          console.warn(`  ⚠️ 警告：pendingBuffer首字节是0x47（TS同步字节），这不应该是加密数据！`)
        }
      }
    }

    // 调用ThunderStone WASM检测加密
    const bufferPtr = this.thunderModule._malloc(512)
    try {
      this.thunderModule.HEAPU8.set(this.headerBuffer, bufferPtr)
      
      // 🔍 关键：_tsCheckDecrypt 会修改内部状态，必须在初始化后立即调用
      console.log('🔍 调用 _tsCheckDecrypt...')
      const checkResult = this.thunderModule._tsCheckDecrypt(this.decryptHandle, bufferPtr, 512)
      
      // 详细日志（在判定前）
      const headerHex = Array.from(this.headerBuffer.slice(0, 32))
        .map(b => b.toString(16).padStart(2, '0')).join(' ')
      const headerAscii = Array.from(this.headerBuffer.slice(0, 32))
        .map(b => (b >= 32 && b < 127) ? String.fromCharCode(b) : '.').join('')
      
      console.log('═'.repeat(60))
      console.log('🔍 ThunderStone加密检测详细信息:')
      console.log(`  解密器句柄: ${this.decryptHandle}`)
      console.log(`  检测结果码: ${checkResult}`)
      console.log(`  头部前32字节(HEX): ${headerHex}`)
      console.log(`  头部前32字节(ASCII): ${headerAscii}`)
      console.log(`  ⚠️ 注意：ThunderStone逻辑与常规相反！`)
      console.log(`  返回值含义: 0=加密, -3=明文`)
      
      // 判定加密状态（注意：返回 0 表示加密！）
      this.isEncrypted = (checkResult === 0)  // 修复：0 表示加密
      console.log(`  最终判定: ${this.isEncrypted ? '✓ 加密流' : '○ 明文流'}`)
      console.log('═'.repeat(60))
      
      // 如果是明文但头部看起来像加密数据，输出警告
      if (!this.isEncrypted && this.headerBuffer[0] !== 0x47) { // 0x47 是TS同步字节
        console.warn('⚠️ 警告：检测为明文，但头部不是标准TS格式！')
        console.warn(`   首字节: 0x${this.headerBuffer[0].toString(16)}, 期望: 0x47`)
      }
    } finally {
      this.thunderModule._free(bufferPtr)
    }

    // currentPosition从512开始（头部已读取）
    this.currentPosition = 512n
  }

  /**
   * 读取数据（核心方法）
   * ⚠️ 关键改进：循环读取直到凑够libmedia要求的数据量
   */
  async read(buffer) {
    try {
      console.log(`📖 [READ] 被调用, 请求 ${buffer.length}B, 当前位置=${this.currentPosition}`)
      
      // 1. 先返回头部数据（原始数据，不解密）
      if (this.headerBuffer) {
        const len = Math.min(buffer.length, this.headerBuffer.length)
        buffer.set(this.headerBuffer.subarray(0, len))
        
        if (len < this.headerBuffer.length) {
          this.headerBuffer = this.headerBuffer.subarray(len)
          console.log(`📤 [READ] 返回头部数据: ${len}B，剩余头部: ${this.headerBuffer.length}B`)
        } else {
          this.headerBuffer = null
          console.log(`📤 [READ] 返回头部数据: ${len}B，头部读取完毕`)
        }
        
        return len
      }

      // 2. 循环读取，直到凑够请求的数据量
      let totalFilled = 0

      // ⚠️ 关键：追踪当前网络数据在文件中的起始位置
      let currentNetworkDataFilePos = this.currentPosition

      while (totalFilled < buffer.length) {
        // 记录本轮网络读取的起始文件位置
        const thisRoundFilePos = currentNetworkDataFilePos

        // 2.1 先从网络读取原始数据
        const remaining = buffer.length - totalFilled
        const rawData = new Uint8Array(remaining)
        let rawOffset = 0

        // 消费缓存
        if (this.pendingBuffer && rawOffset < rawData.length) {
          const toCopy = Math.min(this.pendingBuffer.length, rawData.length - rawOffset)
          rawData.set(this.pendingBuffer.subarray(0, toCopy), rawOffset)
          rawOffset += toCopy

          if (toCopy < this.pendingBuffer.length) {
            this.pendingBuffer = this.pendingBuffer.subarray(toCopy)
          } else {
            this.pendingBuffer = null
          }
        }

        // 从网络读取
        while (rawOffset < rawData.length) {
          const { value, done } = await this.reader.read()
          
          if (done) {
            console.log(`🏁 [READ] 网络流结束`)
            // 如果还有数据在alignBuffer中，尝试处理
            if (rawOffset === 0 && this.alignBuffer.length === 0) {
              if (totalFilled === 0) {
                console.log(`📭 [READ] EOF: 无数据可返回`)
                return -1
              }
              // 已经有部分数据，返回已填充的
              console.log(`📤 [READ] EOF，返回已填充: ${totalFilled}B`)
              return totalFilled
            }
            break
          }

          const toCopy = Math.min(value.length, rawData.length - rawOffset)
          rawData.set(value.subarray(0, toCopy), rawOffset)
          rawOffset += toCopy

          if (toCopy < value.length) {
            this.pendingBuffer = value.subarray(toCopy)
            break
          }
        }

        if (rawOffset === 0) {
          // 没有更多数据了
          break
        }

        // 2.2 处理解密（如果需要）
        if (this.isEncrypted) {
          const outputSlice = buffer.subarray(totalFilled, buffer.length)
          // ✅ 关键修复：传入本轮网络数据的文件起始位置
          const decrypted = this._processAlignedDecryption(outputSlice, rawData.subarray(0, rawOffset), thisRoundFilePos)
          totalFilled += decrypted

          // ✅ 更新文件位置：按实际从网络读取的数据量递增
          currentNetworkDataFilePos += BigInt(rawOffset)

          if (decrypted === 0 && this.alignBuffer.length > 0) {
            // 数据全部进入alignBuffer，继续读取更多数据
            console.log(`🔄 [READ] 数据全部进入缓冲，继续读取...`)
            continue
          }
        } else {
          // 明文数据直接填充
          const toCopy = Math.min(rawOffset, buffer.length - totalFilled)
          buffer.set(rawData.subarray(0, toCopy), totalFilled)
          totalFilled += toCopy
          currentNetworkDataFilePos += BigInt(toCopy)
        }
        
        // 如果已经填满，退出
        if (totalFilled >= buffer.length) {
          break
        }
      }

      // ✅ 更新全局文件读取位置
      this.currentPosition = currentNetworkDataFilePos

      console.log(`📤 [READ] 返回总计: ${totalFilled}B / ${buffer.length}B, 新文件位置: ${this.currentPosition}`)
      return totalFilled

    } catch (error) {
      console.error(`❌ [READ] 读取数据失败:`, error)
      console.error(`❌ [READ] 错误堆栈:`, error.stack)
      return -1
    }
  }

  /**
   * 处理对齐解密（核心方法）
   * 参考软解方案的 alignFifoWrite 逻辑
   *
   * @param {Uint8Array} outputBuffer - 输出缓冲区
   * @param {Uint8Array} newData - 本轮从网络读取的新数据
   * @param {BigInt} newDataFileOffset - 新数据在文件中的起始位置（对应C软解的offset参数）
   *
   * 流程：
   * 1. 将alignBuffer + 新数据合并
   * 2. 只解密8KB对齐的部分
   * 3. 未对齐的尾部数据存回alignBuffer
   * 4. 只返回已解密的数据给播放器
   */
  _processAlignedDecryption(outputBuffer, newData, newDataFileOffset) {
    const BLOCK_SIZE = 8192
    const HEAD_SIZE = 512

    try {
      console.log(`🔄 [对齐处理] 开始处理: alignBuffer=${this.alignBuffer.length}B, newData=${newData.length}B, newDataFileOffset=${newDataFileOffset}`)

      // 第一步：合并 alignBuffer + 新数据
      const totalData = new Uint8Array(this.alignBuffer.length + newData.length)
      totalData.set(this.alignBuffer, 0)
      totalData.set(newData, this.alignBuffer.length)

      console.log(`🔍 [对齐处理] 详细信息:`)
      console.log(`  - alignBuffer长度: ${this.alignBuffer.length}B`)
      console.log(`  - newData长度: ${newData.length}B`)
      console.log(`  - newDataFileOffset: ${newDataFileOffset}`)
      console.log(`  - alignBufferStartPos: ${this.alignBufferStartPos}`)
      console.log(`  - 当前currentPosition: ${this.currentPosition}`)

      // ⚠️ 关键修复：判断是否是新批次，并计算正确的文件偏移
      // isNewBatch = alignBuffer为空，意味着这是一个新的对齐批次
      const isNewBatch = (this.alignBuffer.length === 0)
      let baseFileOffset

      if (isNewBatch) {
        // 新批次：新数据的文件位置就是合并数据的起始位置
        baseFileOffset = newDataFileOffset
        this.alignBufferStartPos = baseFileOffset
        console.log(`📍 [对齐处理] 新批次，起始位置: ${baseFileOffset}`)
      } else {
        // 继续批次：使用之前保存的alignBuffer起始位置
        // alignBuffer中的数据起始位置不变，新数据是追加的
        baseFileOffset = this.alignBufferStartPos
        console.log(`🔄 [对齐处理] 继续之前批次，alignBuffer起始位置: ${baseFileOffset}`)
        console.log(`  ⚠️ 验证连续性:`)
        console.log(`    alignBuffer结束位置: ${Number(this.alignBufferStartPos) + this.alignBuffer.length}`)
        console.log(`    newData起始位置: ${newDataFileOffset}`)
        console.log(`    是否连续: ${Number(this.alignBufferStartPos) + this.alignBuffer.length === Number(newDataFileOffset) ? '✅' : '❌'}`)
      }
      
      // 第二步：计算可解密的对齐部分
      const totalSize = totalData.length
      const alignedSize = Math.floor(totalSize / BLOCK_SIZE) * BLOCK_SIZE
      const unalignedSize = totalSize - alignedSize
      
      console.log(`📊 [对齐处理] 数据统计: total=${totalSize}B, aligned=${alignedSize}B (${alignedSize/BLOCK_SIZE}块), unaligned=${unalignedSize}B`)
      
      // 第三步：解密对齐部分
      let decryptedData
      if (alignedSize > 0) {
        console.log(`🔐 [对齐处理] 准备一次性解密 ${alignedSize} 字节...`)
        decryptedData = totalData.subarray(0, alignedSize)
        // ✅ 修复：调用新的一次性解密方法，使用计算出的baseFileOffset
        this._decryptAlignedDataOnce(decryptedData, Number(baseFileOffset), isNewBatch)
        console.log(`✅ [对齐处理] 解密成功`)
      } else {
        console.log(`⏸️ [对齐处理] 数据不足8KB，暂不解密`)
        decryptedData = new Uint8Array(0)
      }
      
      // 第四步：保存未对齐部分到alignBuffer
      if (unalignedSize > 0) {
        this.alignBuffer = totalData.subarray(alignedSize)
        // ⚠️ 关键修复：alignBufferStartPos记录剩余数据在文件中的起始位置
        // 剩余数据的文件位置 = 本次解密的起始位置 + 已解密的大小
        this.alignBufferStartPos = baseFileOffset + BigInt(alignedSize)
        console.log(`📦 [对齐处理] 暂存未对齐数据: ${unalignedSize}B，新起始位置: ${this.alignBufferStartPos}`)
      } else {
        this.alignBuffer = new Uint8Array(0)
        console.log(`✨ [对齐处理] 数据完全对齐，无剩余`)
      }

      // 第五步：将解密后的数据复制到outputBuffer
      const outputSize = Math.min(decryptedData.length, outputBuffer.length)
      if (outputSize > 0) {
        outputBuffer.set(decryptedData.subarray(0, outputSize), 0)
        console.log(`📤 [对齐处理] 返回已解密数据: ${outputSize}B`)
      } else {
        console.warn(`⚠️ [对齐处理] 无可返回数据！decryptedData=${decryptedData.length}B, outputBuffer=${outputBuffer.length}B`)
      }

      // ✅ 不再在这里更新 currentPosition
      // currentPosition 由 read() 方法统一管理，按网络读取的数据量递增

      return outputSize
      
    } catch (error) {
      console.error(`❌ [对齐处理] 处理失败:`, error)
      console.error(`❌ [对齐处理] 错误堆栈:`, error.stack)
      console.error(`❌ [对齐处理] 当前状态: alignBuffer=${this.alignBuffer?.length}, newData=${newData?.length}, currentPos=${this.currentPosition}`)
      throw error
    }
  }
  
  /**
   * 一次性解密所有对齐的数据（基于 ThunderWebPlayer 软解的实现）
   * @param {Uint8Array} data - 要解密的数据（必须是8KB的倍数）
   * @param {number} baseFileOffset - 数据在文件中的起始位置
   * @param {boolean} isNewBatch - 是否是新批次（决定是否需要 seek）
   */
  _decryptAlignedDataOnce(data, baseFileOffset, isNewBatch) {
    const BLOCK_SIZE = 8192
    const HEAD_SIZE = 512
    const Module = this.thunderModule

    // 验证数据对齐
    if (data.length % BLOCK_SIZE !== 0) {
      throw new Error(`数据未对齐: ${data.length}，应该是8192的倍数`)
    }

    const blockCount = data.length / BLOCK_SIZE
    console.log(`🔐 [解密] 准备一次性解密: ${blockCount}个块 (${data.length}B), baseFileOffset=${baseFileOffset}`)

    // ⚠️ 关键修复：检查数据是否已经是明文
    // ThunderStone 格式的第一个块（块0）通常是明文（包含 PAT/PMT）
    const firstByte = data[0]
    if (firstByte === 0x47) {
      console.log(`  ℹ️  [解密] 检测到明文数据（首字节0x47），跳过解密`)
      console.log(`  ℹ️  [解密] 但需要手动推进 mRealSegPos 状态...`)

      // ✅ 即使跳过解密，也要更新解密器状态
      if (isNewBatch) {
        const startBlockIndex = Math.floor((baseFileOffset - HEAD_SIZE) / BLOCK_SIZE)
        Module._tsDataDecryptSeek(this.decryptHandle, startBlockIndex)
        console.log(`  🎯 [解密] 明文块，但仍需 seek 到 blockIndex=${startBlockIndex}`)
      }

      // 手动推进状态：对于明文数据，我们需要让解密器知道跳过了这些块
      // 方法：seek到下一个块的位置
      const nextBlockIndex = Math.floor((baseFileOffset - HEAD_SIZE) / BLOCK_SIZE) + blockCount
      Module._tsDataDecryptSeek(this.decryptHandle, nextBlockIndex)
      console.log(`  ⏭️  [解密] 推进状态到下一块: blockIndex=${nextBlockIndex}`)

      return // 直接返回，不解密
    }

    // ✅ 关键：只在新批次时 seek
    if (isNewBatch) {
      const startBlockIndex = Math.floor((baseFileOffset - HEAD_SIZE) / BLOCK_SIZE)
      Module._tsDataDecryptSeek(this.decryptHandle, startBlockIndex)
      console.log(`  🎯 [解密] 新批次，seek到 blockIndex=${startBlockIndex}`)
    } else {
      console.log(`  🔄 [解密] 继续批次，不再 seek（让解密器自动管理状态）`)
    }

    // 分配一次性内存
    const bufferPtr = Module._malloc(data.length)
    if (!bufferPtr) {
      throw new Error(`WASM内存分配失败: ${data.length} bytes`)
    }

    try {
      // 复制数据到 WASM 内存
      const heapBuffer = new Uint8Array(Module.HEAPU8.buffer, bufferPtr, data.length)
      heapBuffer.set(data)

      // 打印解密前数据（前16字节）
      const beforeHex = Array.from(data.subarray(0, 16))
        .map(b => b.toString(16).padStart(2, '0')).join(' ')
      console.log(`  📥 [解密] 解密前(前16字节): ${beforeHex}`)

      // ✅ 一次性解密所有块
      const result = Module._tsDataDecrypt(this.decryptHandle, bufferPtr, data.length)

      if (result !== 0) {
        throw new Error(`解密失败: result=${result}`)
      }

      // 复制解密后的数据回原数组
      const decryptedData = new Uint8Array(Module.HEAPU8.buffer, bufferPtr, data.length)
      data.set(decryptedData)

      // 打印解密后数据（前16字节）
      const afterHex = Array.from(decryptedData.subarray(0, 16))
        .map(b => b.toString(16).padStart(2, '0')).join(' ')
      console.log(`  📤 [解密] 解密后(前16字节): ${afterHex}`)

      // 验证首字节（TS同步字节应该是0x47）
      if (decryptedData[0] === 0x47) {
        console.log(`  ✅ [解密] 首块TS同步字节正确(0x47)`)
      } else {
        console.warn(`  ⚠️ [解密] 首块首字节非0x47: 0x${decryptedData[0].toString(16)}`)
      }

      console.log(`✅ [解密] 一次性解密成功: ${data.length}B (${blockCount}个块)`)

    } finally {
      Module._free(bufferPtr)
    }
  }

  /**
   * Seek操作
   */
  async seek(position) {
    try {
      console.log(`⏩ Seek到位置: ${position}`)

      // 通知解密器seek
      if (this.isEncrypted) {
        const blockIndex = Math.floor((Number(position) - 512) / 8192)
        this.thunderModule._tsDataDecryptSeek(this.decryptHandle, blockIndex)
      }

      // 关闭当前流
      if (this.reader) {
        await this.reader.cancel()
      }

      // 重新打开流（Range请求）
      this.response = await fetch(this.url, {
        headers: { Range: `bytes=${Number(position)}-` }
      })
      this.reader = this.response.body.getReader()
      this.currentPosition = position
      this.headerBuffer = null
      this.pendingBuffer = null

      // ✅ 重要：清空对齐缓冲区
      this.alignBuffer = new Uint8Array(0)
      this.alignBufferStartPos = position

      console.log(`✅ Seek成功，已清空对齐缓冲区`)
      return 0
    } catch (error) {
      console.error('❌ Seek失败:', error)
      return -1
    }
  }

  /**
   * 获取文件大小
   */
  async size() {
    return BigInt(this.totalSize || 0)
  }

  /**
   * 停止读取
   */
  async stop() {
    console.log('⏹️ 停止数据源')

    if (this.reader) {
      try {
        await this.reader.cancel()
      } catch (e) {
        console.warn('reader.cancel() 异常:', e)
      }
      this.reader = null
    }

    if (this.decryptHandle) {
      this.thunderModule._tsDeinitDecrypt(this.decryptHandle)
      this.decryptHandle = 0
    }

    this.response = null
    this.headerBuffer = null
    this.pendingBuffer = null
  }
}

// 导出
if (typeof module !== 'undefined' && module.exports) {
  module.exports = HybridThunderStoneIOLoader
}
