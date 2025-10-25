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

    // WASM decoder状态
    this.decoderInitialized = false
    this.decoderOpened = false
    this.packetCallback = null
    this.packetBuffer = []  // 缓存packet直到libmedia请求

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
      const initRet = this.thunderModule._initDecoder(this.totalSize, 0)
      if (initRet !== 0) {
        throw new Error(`initDecoder失败: ${initRet}`)
      }
      this.decoderInitialized = true
      this.log('  ✓ Decoder初始化成功')

      // 3. 设置packet回调（WASM会在demux后调用此回调）
      this.setupPacketCallback()

      // 4. 开始流式下载并喂给WASM
      this.log('  开始下载视频数据...')
      this.startDownload()

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

        // 首块发送后，打开decoder（触发demux）
        if (type === 0 && !this.decoderOpened) {
          this.log('  打开decoder (开始demux)...')
          const openRet = this.thunderModule._openDecoder(0, 0, 0, 0, 0, 0)
          if (openRet !== 0) {
            console.error(`❌ openDecoder失败: ${openRet}`)
            break
          }
          this.decoderOpened = true
          this.log('  ✓ Decoder打开成功')

          // 获取流信息
          this.getStreamInfo()
        }

        // 持续读取packets
        if (this.decoderOpened) {
          this.readPackets()
        }

        // 简单流控：如果packet缓冲过多，等待消费
        while (this.packetBuffer.length > 100) {
          await new Promise(resolve => setTimeout(resolve, 10))
        }
      }

    } catch (error) {
      console.error('❌ 下载失败:', error)
      this.isStreamEnded = true
    }
  }

  /**
   * 从WASM读取packets（触发packet回调）
   */
  readPackets() {
    // 批量读取packets
    for (let i = 0; i < 10; i++) {
      const ret = this.thunderModule._js_readOnePacket()
      if (ret !== 0) {
        break  // EAGAIN或EOF
      }
    }
  }

  /**
   * 获取流信息
   */
  getStreamInfo() {
    const videoIdx = this.thunderModule._js_getVideoStreamIndex()
    const audioIdx = this.thunderModule._js_getAudioStreamIndex()

    if (videoIdx >= 0) {
      this.videoStream = {
        codecId: this.thunderModule._js_getVideoCodecId(),
        width: this.thunderModule._js_getVideoWidth(),
        height: this.thunderModule._js_getVideoHeight()
      }
      this.log(`  ✓ Video: ${this.videoStream.width}x${this.videoStream.height}, codec=${this.videoStream.codecId}`)
    }

    if (audioIdx >= 0) {
      this.audioStream = {
        codecId: this.thunderModule._js_getAudioCodecId(),
        sampleRate: this.thunderModule._js_getAudioSampleRate(),
        channels: this.thunderModule._js_getAudioChannels()
      }
      this.log(`  ✓ Audio: ${this.audioStream.sampleRate}Hz, ${this.audioStream.channels}ch, codec=${this.audioStream.codecId}`)
    }
  }

  /**
   * 读取数据（libmedia调用）
   * ⚠️ 关键：这里返回的是packet数据，不是原始TS流
   */
  async read(buffer) {
    // 等待至少有一个packet
    while (this.packetBuffer.length === 0 && !this.isStreamEnded) {
      await new Promise(resolve => setTimeout(resolve, 10))

      // 继续读取packets
      if (this.decoderOpened) {
        this.readPackets()
      }
    }

    // 如果没有packet且流已结束，返回EOF
    if (this.packetBuffer.length === 0) {
      this.log('📭 EOF: 无更多packets')
      return -1
    }

    // 取出一个packet填充到buffer
    const packet = this.packetBuffer.shift()
    const copySize = Math.min(packet.data.length, buffer.length)
    buffer.set(packet.data.subarray(0, copySize), 0)

    if (this.debug) {
      const type = packet.stream_type === 0 ? 'VIDEO' : 'AUDIO'
      console.log(`📤 [Read] ${type} packet: ${copySize}B`)
    }

    return copySize
  }

  /**
   * Seek操作
   */
  async seek(position) {
    this.log(`⏩ Seek到: ${position}`)
    // TODO: 实现seek逻辑
    return -1  // 暂不支持
  }

  /**
   * 获取文件大小
   */
  async size() {
    return BigInt(this.totalSize || 0)
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
