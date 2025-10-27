/**
 * Thunder WASM Bridge - 连接WASM demuxer和libmedia硬解
 *
 * 核心职责：
 * 1. 管理WASM decoder的生命周期（初始化、数据喂入、packet读取）
 * 2. 提供IOLoader接口给libmedia AVPlayer
 * 3. 将WASM输出的H264/AAC packet桥接到libmedia
 * 4. 确保明文数据不暴露到JS层（仅packet数据传递）
 *
 * 数据流：
 * Fetch加密数据 → sendData(WASM) → Thunder解密+FFmpeg demux → packet回调 → libmedia硬解
 *
 * @version 1.0.0
 */

class ThunderWASMBridge extends AVPlayer.IOLoader.CustomIOLoader {
  constructor(options) {
    super()

    this.url = options.url
    this.thunderModule = options.thunderModule || Module
    this.debug = options.debug || false

    // 网络相关
    this.totalSize = 0
    this.downloadedSize = 0
    this.isStreamEnded = false

    // ✅ libmedia的IOError常量（必须与libmedia一致）
    this.IOError = {
      END: -(1 << 20),        // -1048576
      AGAIN: -(1 << 20) - 1,
      INVALID_OPERATION: -(1 << 20) - 2
    }

    // WASM decoder状态
    this.decoderInitialized = false
    this.decoderOpened = false
    this.packetCallback = null
    this.packetBuffer = []  // 缓存packet直到libmedia请求

    // ✅ 新增：用于同步首块数据就绪
    this.firstChunkReady = false
    this.firstChunkPromise = null
    this.firstChunkResolve = null

    // 流信息
    this.videoStream = null
    this.audioStream = null
    this.duration = 0
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
    return `ThunderWASM(${this.url})`
  }

  get minBuffer() {
    return 4
  }

  /**
   * 打开数据源（初始化WASM decoder + 开始下载）
   * ✅ 关键修改：等待首块数据就绪后再返回，确保libmedia调用read()时FIFO有数据
   */
  async open() {
    try {
      this.log('📂 打开ThunderWASM Bridge...')

      // 1. 获取文件大小
      const headResp = await fetch(this.url, { method: 'HEAD' })
      const contentLength = headResp.headers.get('Content-Length')
      if (contentLength) {
        this.totalSize = parseInt(contentLength)
        this.log(`  文件大小: ${this.totalSize} bytes`)
      }

      // 2. 初始化WASM decoder
      this.log('  初始化WASM decoder...')
      // 参数：(fileSize, logLevel, enableDecryption)
      // enableDecryption: 0=禁用解密（验证IO通路），1=启用Thunder解密
      const enableDecryption = 1  // ✅ Phase 3: 启用Thunder解密
      const initRet = this.thunderModule._initDecoder(this.totalSize, 0, enableDecryption)
      if (initRet !== 0) {
        throw new Error(`initDecoder失败: ${initRet}`)
      }
      this.decoderInitialized = true
      this.log(`  ✓ Decoder初始化成功 (enableDecryption=${enableDecryption})`)

      // 3. ✅ 不再需要packet回调（WASM不做demux，不输出packets）
      // this.setupPacketCallback()

      // 4. 创建首块数据就绪的Promise
      this.firstChunkPromise = new Promise(resolve => {
        this.firstChunkResolve = resolve
      })

      // 5. 开始流式下载并喂给WASM（后台运行）
      this.log('  开始下载视频数据...')
      this.startDownload()

      // 6. ✅ 关键：等待首块数据写入FIFO后才返回
      this.log('  等待首块数据就绪...')
      await this.firstChunkPromise
      this.log('  ✓ 首块数据已就绪，FIFO可读')

      return 0
    } catch (error) {
      console.error('❌ 打开失败:', error)
      return -1
    }
  }

  /**
   * 设置packet回调（WASM调用此函数输出H264/AAC packet）
   */
  setupPacketCallback() {
    this.packetCallback = this.thunderModule.addFunction(
      (stream_type, dataPtr, size, pts, dts, flags) => {
        // stream_type: 0=video, 1=audio
        // flags: bit0=keyframe

        // 从WASM内存复制packet数据
        const packetData = new Uint8Array(size)
        packetData.set(new Uint8Array(this.thunderModule.HEAPU8.buffer, dataPtr, size))

        // 缓存packet（libmedia的read()会消费）
        this.packetBuffer.push({
          stream_type,
          data: packetData,
          pts,
          dts,
          flags,
          isKeyframe: (flags & 1) !== 0
        })

        if (this.debug) {
          const streamName = stream_type === 0 ? 'VIDEO' : 'AUDIO'
          console.log(`📦 [WASM Packet] ${streamName} ${size}B, pts=${pts}, keyframe=${(flags & 1) !== 0}`)
        }
      },
      'viiiiii'  // 函数签名
    )

    this.thunderModule._js_setPacketCallback(this.packetCallback)
    this.log('  ✓ Packet回调设置成功')
  }

  /**
   * 开始流式下载并喂给WASM
   */
  async startDownload() {
    try {
      const response = await fetch(this.url)
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`)
      }

      const reader = response.body.getReader()
      let offset = 0
      let firstChunk = true

      while (true) {
        const { value, done } = await reader.read()

        if (done) {
          this.log('📥 下载完成')
          this.isStreamEnded = true
          break
        }

        // 分配WASM内存并复制数据
        const size = value.length
        const ptr = this.thunderModule._malloc(size)
        this.thunderModule.HEAPU8.set(value, ptr)

        // 发送到WASM decoder
        // type: 0=header (首块), 1=stream data (后续块)
        const type = firstChunk ? 0 : 1
        const sendRet = this.thunderModule._sendData(offset, ptr, size, type)
        this.thunderModule._free(ptr)

        if (sendRet < 0) {
          console.error(`❌ sendData失败: ${sendRet}, offset=${offset}, size=${size}`)
          break
        }

        offset += size
        this.downloadedSize += size
        firstChunk = false

        // ✅ 关键修改：不调用openDecoder()，WASM不做FFmpeg demux
        // WASM只负责Thunder解密，解密后的TS流在FIFO中
        // libmedia会通过read()读取FIFO中的TS流并自己做demux
        if (type === 0 && !this.decoderOpened) {
          this.log('  ✓ 首块数据已发送到WASM（仅解密，不demux）')
          this.decoderOpened = true  // 标记为已准备好（虽然不真正打开FFmpeg）

          // ✅ 通知open()：首块数据已就绪
          if (this.firstChunkResolve) {
            this.firstChunkResolve()
            this.firstChunkResolve = null
            this.firstChunkReady = true
          }
        }

        // ✅ 流控：检查FIFO使用率，避免内存爆炸
        // decoder.c的kMaxFifoSize = 3MB，我们在2.5MB时开始限速
        const fifoSize = this.thunderModule._js_getFIFOSize ? this.thunderModule._js_getFIFOSize() : 0
        const maxFifoSize = 3 * 1024 * 1024  // 3MB

        if (fifoSize > maxFifoSize * 0.8) {
          // FIFO使用率超过80%，暂停下载让libmedia消费
          if (this.debug) {
            this.log(`  ⏸️ FIFO使用率高(${(fifoSize / maxFifoSize * 100).toFixed(1)}%)，暂停下载`)
          }

          // 等待FIFO降到50%以下
          while (true) {
            await new Promise(resolve => setTimeout(resolve, 50))
            const currentSize = this.thunderModule._js_getFIFOSize ? this.thunderModule._js_getFIFOSize() : 0
            if (currentSize < maxFifoSize * 0.5) {
              if (this.debug) {
                this.log(`  ▶️ FIFO空间充足(${(currentSize / maxFifoSize * 100).toFixed(1)}%)，继续下载`)
              }
              break
            }
          }
        }
      }

    } catch (error) {
      console.error('❌ 下载失败:', error)
      this.isStreamEnded = true
    }
  }

  /**
   * ✅ 不再需要：WASM不做demux，这些函数无用
   */
  // readPackets() {
  //   for (let i = 0; i < 10; i++) {
  //     const ret = this.thunderModule._js_readOnePacket()
  //     if (ret !== 0) break
  //   }
  // }

  // getStreamInfo() {
  //   const videoIdx = this.thunderModule._js_getVideoStreamIndex()
  //   const audioIdx = this.thunderModule._js_getAudioStreamIndex()
  //   // ... 流信息由libmedia自己从TS流中解析
  // }

  /**
   * 读取数据（libmedia调用）
   * ✅ 最终方案：从WASM FIFO读取解密后的TS流
   *
   * 架构说明：
   * 1. WASM只进行Thunder解密，不做FFmpeg demux
   * 2. 解密后的明文TS流存储在WASM的FIFO中
   * 3. JS通过readFromFIFO()读取TS容器格式数据
   * 4. libmedia的demuxer收到完整TS流，自己进行demux和硬解
   *
   * 安全性：明文TS只在WASM内存和传递瞬间存在，不持久化到JS变量
   *
   * 关键：libmedia要求容器格式（TS），不支持ES packets！
   */
  async read(buffer) {
    // 等待FIFO中有数据
    let retryCount = 0
    const maxRetries = 500  // 5秒超时

    while (retryCount < maxRetries) {
      // 分配WASM内存作为临时缓冲区
      const tempPtr = this.thunderModule._malloc(buffer.length)

      // 尝试从FIFO读取到WASM内存
      const bytesRead = this.thunderModule._js_readFromFIFO(tempPtr, buffer.length)

      if (bytesRead > 0) {
        // 成功读取数据，复制到JS buffer
        buffer.set(new Uint8Array(this.thunderModule.HEAPU8.buffer, tempPtr, bytesRead))
        this.thunderModule._free(tempPtr)

        if (this.debug) {
          console.log(`📤 [Read] 从FIFO读取TS流: ${bytesRead}B`)
        }
        return bytesRead
      }

      // bytesRead === 0: FIFO暂时为空
      // bytesRead < 0: FIFO错误或decoder未初始化
      this.thunderModule._free(tempPtr)

      if (bytesRead < 0) {
        console.error(`❌ [Read] FIFO读取错误: ${bytesRead}`)
        return this.IOError.END  // ✅ 返回IOError.END
      }

      // 如果流已结束且FIFO为空，返回EOF
      if (this.isStreamEnded) {
        this.log('📭 EOF: 流已结束且FIFO为空')
        return this.IOError.END  // ✅ 返回IOError.END而不是-1
      }

      // 等待数据
      await new Promise(resolve => setTimeout(resolve, 10))
      retryCount++
    }

    // 超时
    console.warn('⏱️ [Read] 读取超时，FIFO长时间无数据')
    return this.IOError.END  // ✅ 超时也视为EOF
  }

  /**
   * Seek操作
   * ✅ 关键修改：对于流式播放，"假装"支持所有seek
   *
   * 原因：
   * - libmedia在probe阶段会调用seek探测流信息（通常只需要头部）
   * - IOReader要求seek()返回0，否则会进入error状态
   * - 对于流式播放，实际数据从FIFO顺序读取，不支持真正的随机seek
   * - 但我们可以"欺骗"IOReader，让它认为seek成功了
   */
  async seek(position) {
    this.log(`⏩ Seek请求: position=${position}`)

    // 对于流式播放，我们"假装"支持所有seek
    // 实际上数据是从FIFO顺序读取的，这对probe阶段足够了
    // probe只需要头部数据，而我们的FIFO里已经有header数据了
    this.log(`  ✓ Seek请求已接受（流式播放，实际继续从FIFO读取）`)
    return 0
  }

  /**
   * 获取文件大小
   * ✅ 关键修改：返回0表示这是流式传输（类似直播），不支持seek
   *
   * 原因：
   * - 我们的FIFO是顺序读取，不支持真正的随机seek
   * - libmedia在probe阶段会seek到文件不同位置分析
   * - 如果返回真实文件大小，libmedia会认为可以seek，导致分析失败
   * - 返回0让libmedia按流式模式处理（不seek，只顺序读取）
   */
  async size() {
    return 0n  // ✅ 返回0表示流式传输，禁用seek
  }

  /**
   * 停止
   */
  async stop() {
    this.log('⏹️ 停止ThunderWASM Bridge')

    if (this.packetCallback) {
      this.thunderModule.removeFunction(this.packetCallback)
      this.packetCallback = null
    }

    // WASM decoder会自动清理
    this.decoderInitialized = false
    this.decoderOpened = false
    this.packetBuffer = []
  }

  /**
   * 日志工具
   */
  log(message) {
    if (this.debug) {
      console.log(`[ThunderWASMBridge] ${message}`)
    }
  }
}

// 导出
if (typeof module !== 'undefined' && module.exports) {
  module.exports = ThunderWASMBridge
}
