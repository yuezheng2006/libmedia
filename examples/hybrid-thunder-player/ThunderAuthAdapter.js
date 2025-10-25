/**
 * Thunder鉴权适配器
 *
 * 职责：
 * 1. 调用WASM模块的鉴权功能
 * 2. 提供统一的鉴权接口
 * 3. 管理鉴权状态
 *
 * 重要说明：
 * - Thunder只有一套鉴权，发生在WASM层（js_init_auth）
 * - 不需要调用Thunder Player的initSDK()做鉴权
 * - initSDK()只是参数设置，真正的鉴权在WASM内部
 *
 * 使用方式：
 * const adapter = new ThunderAuthAdapter()
 * await adapter.init({ appid, uid, sdk_sn })
 * const isAuthorized = adapter.isAuthorized()
 */

class ThunderAuthAdapter {
  constructor() {
    this.isInitialized = false
    this.authStatus = 'NOT_INITIALIZED' // NOT_INITIALIZED | INITIALIZING | SUCCESS | FAILED
    this.authParams = null
  }

  /**
   * 初始化鉴权
   * @param {Object} params - 鉴权参数
   * @param {string} params.appid - 应用ID
   * @param {string} params.uid - 用户ID（可选）
   * @param {string} params.sdk_sn - SDK序列号
   * @param {string} params.authServer - 认证服务器地址（可选，优先级最高）
   * @returns {Promise<boolean>} 是否初始化成功
   */
  async init(params) {
    if (this.isInitialized && this.authStatus === 'SUCCESS') {
      console.log('✅ Thunder鉴权已完成，跳过重复初始化')
      return true
    }

    if (this.authStatus === 'INITIALIZING') {
      console.warn('⏳ Thunder鉴权正在进行中，请等待...')
      return this.waitForAuth()
    }

    try {
      this.authStatus = 'INITIALIZING'
      this.authParams = params

      console.log('🔐 开始Thunder WASM鉴权...')
      console.log('  - appid:', params.appid)
      console.log('  - uid:', params.uid || '(未提供)')
      console.log('  - sdk_sn:', params.sdk_sn ? '✓' : '✗')
      console.log('  - authServer:', params.authServer || '(使用配置文件或默认值)')

      // 设置authServer到全局变量（如果提供）
      if (params.authServer) {
        globalThis.thunderPlayerAuthServer = params.authServer
      }

      // 调用WASM鉴权
      const result = await this.initWASMAuth(params)

      if (result) {
        this.authStatus = 'SUCCESS'
        this.isInitialized = true
        console.log('✅ Thunder WASM鉴权成功')
        return true
      } else {
        this.authStatus = 'FAILED'
        console.error('❌ Thunder WASM鉴权失败')
        return false
      }
    } catch (error) {
      this.authStatus = 'FAILED'
      console.error('❌ Thunder鉴权异常:', error)
      return false
    }
  }

  /**
   * 初始化WASM模块的鉴权
   * @param {Object} params - 鉴权参数
   * @returns {Promise<boolean>}
   */
  async initWASMAuth(params) {
    try {
      console.log('🔧 初始化WASM模块鉴权...')

      // 检查WASM模块是否已加载
      if (typeof Module === 'undefined' || typeof Module._js_init_auth !== 'function') {
        console.warn('⚠️ WASM模块未加载或缺少js_init_auth函数')
        return false
      }

      // 准备参数（需要转换为C字符串）
      const appid = params.appid
      const uid = params.uid || `libmedia-${Date.now()}`
      const sdk_sn = params.sdk_sn

      // 分配C字符串（使用标准Emscripten API）
      const appidLen = lengthBytesUTF8(appid) + 1
      const uidLen = lengthBytesUTF8(uid) + 1
      const sdkSnLen = lengthBytesUTF8(sdk_sn) + 1

      const appidPtr = Module._malloc(appidLen)
      const uidPtr = Module._malloc(uidLen)
      const sdkSnPtr = Module._malloc(sdkSnLen)

      Module.stringToUTF8(appid, appidPtr, appidLen)
      Module.stringToUTF8(uid, uidPtr, uidLen)
      Module.stringToUTF8(sdk_sn, sdkSnPtr, sdkSnLen)

      // 分配响应缓冲区
      const responseSize = 4096
      const responseBuffer = Module._malloc(responseSize)

      console.log('  - appid:', appid)
      console.log('  - uid:', uid)
      console.log('  - sdk_sn:', sdk_sn)

      // 调用WASM鉴权
      const initRet = Module._js_init_auth(appidPtr, uidPtr, sdkSnPtr, responseBuffer, responseSize)

      // 释放C字符串
      Module._free(appidPtr)
      Module._free(uidPtr)
      Module._free(sdkSnPtr)

      if (initRet !== 0) {
        Module._free(responseBuffer)
        console.error(`❌ WASM鉴权初始化失败: ${initRet}`)
        return false
      }

      console.log('⏳ WASM鉴权请求已发送，等待响应...')

      // 轮询检查鉴权状态
      const maxWaitTime = 10000 // 10秒
      const checkInterval = 200
      let waited = 0

      while (waited < maxWaitTime) {
        const authStatus = Module._get_auth_status_wrapper()

        if (authStatus === 1) {
          Module._free(responseBuffer)
          console.log('✅ WASM模块鉴权成功')
          return true
        } else if (authStatus === -1) {
          Module._free(responseBuffer)
          console.error('❌ WASM模块鉴权失败')
          return false
        }

        await new Promise(resolve => setTimeout(resolve, checkInterval))
        waited += checkInterval
      }

      Module._free(responseBuffer)
      console.error('❌ WASM鉴权超时')
      return false

    } catch (error) {
      console.error('❌ WASM鉴权异常:', error)
      return false
    }
  }

  /**
   * 等待鉴权完成
   * @returns {Promise<boolean>}
   */
  async waitForAuth() {
    const maxWaitTime = 10000 // 最多等待10秒
    const checkInterval = 100
    let waited = 0

    while (this.authStatus === 'INITIALIZING' && waited < maxWaitTime) {
      await new Promise(resolve => setTimeout(resolve, checkInterval))
      waited += checkInterval
    }

    return this.authStatus === 'SUCCESS'
  }

  /**
   * 检查是否已鉴权
   * @returns {boolean}
   */
  isAuthorized() {
    return this.isInitialized && this.authStatus === 'SUCCESS'
  }

  /**
   * 获取鉴权状态
   * @returns {string}
   */
  getStatus() {
    return this.authStatus
  }

  /**
   * 获取鉴权参数
   * @returns {Object|null}
   */
  getAuthParams() {
    return this.authParams
  }

  /**
   * 销毁鉴权适配器
   */
  destroy() {
    this.isInitialized = false
    this.authStatus = 'NOT_INITIALIZED'
    this.authParams = null

    // 清空全局authServer配置
    if (globalThis.thunderPlayerAuthServer) {
      globalThis.thunderPlayerAuthServer = undefined
    }
  }
}

// 导出（支持ES6和传统方式）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = ThunderAuthAdapter
}
