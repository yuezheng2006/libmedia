/**
 * Thunder鉴权适配器
 * 
 * 职责：
 * 1. 复用Thunder SDK的鉴权机制
 * 2. 提供统一的鉴权接口
 * 3. 管理鉴权状态
 * 
 * 使用方式：
 * const adapter = new ThunderAuthAdapter()
 * await adapter.init({ appid, uid, sdk_sn })
 * const isAuthorized = adapter.isAuthorized()
 */

class ThunderAuthAdapter {
  constructor() {
    this.authPlayer = null
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

      console.log('🔐 开始Thunder鉴权初始化...')
      console.log('  - appid:', params.appid)
      console.log('  - uid:', params.uid || '(未提供)')
      console.log('  - sdk_sn:', params.sdk_sn ? '✓' : '✗')

      // 确保Thunder Player脚本已加载
      if (typeof window.Player === 'undefined') {
        throw new Error('Thunder Player脚本未加载，请先加载 thunder-decrypt 相关脚本')
      }

      // 创建临时Player实例用于鉴权
      this.authPlayer = new window.Player()

      // 调用Thunder SDK的initSDK进行鉴权
      const result = await this.authPlayer.initSDK({
        appid: params.appid,
        uid: params.uid || `libmedia-${Date.now()}`,
        sdk_sn: params.sdk_sn
      })

      if (result) {
        this.authStatus = 'SUCCESS'
        this.isInitialized = true
        console.log('✅ Thunder鉴权成功')
        return true
      } else {
        this.authStatus = 'FAILED'
        console.error('❌ Thunder鉴权失败')
        return false
      }
    } catch (error) {
      this.authStatus = 'FAILED'
      console.error('❌ Thunder鉴权异常:', error)
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
    if (this.authPlayer) {
      // Thunder Player的资源清理
      this.authPlayer = null
    }
    this.isInitialized = false
    this.authStatus = 'NOT_INITIALIZED'
    this.authParams = null
  }
}

// 导出（支持ES6和传统方式）
if (typeof module !== 'undefined' && module.exports) {
  module.exports = ThunderAuthAdapter
}
