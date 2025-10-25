/**
 * Thunder Web Player HTTP Bridge
 * 二进制数据传输桥接模块
 */

// 创建日志记录器实例（将在common.js导入后初始化）
let bridgeLogger;

// 初始化日志记录器（在common.js加载后调用）
function initLogger() {
  if (typeof Logger !== 'undefined') {
    bridgeLogger = new Logger("HTTPBridge");
    console.log("Logger类可用，初始化日志记录器");
  } else {
    console.warn("Logger类不可用，使用console代替");
  }
}

// 日志输出函数
// 🎯 简化日志 - 只关注核心节点: 网络连通性、速度、鉴权
function logDebug(message, ...args) {
  // 只记录关键调试信息
  const coreKeywords = ['认证服务器', '网络', '速度', '鉴权', '连接', '响应时间', '错误'];
  if (coreKeywords.some(keyword => message.includes(keyword))) {
    console.log(`🔍 [${new Date().toLocaleTimeString()}] ${message}`, ...args);
  }
}

function logInfo(message, ...args) {
  // 只记录重要信息
  const coreKeywords = ['认证', '网络', '速度', '连接', '成功', '完成'];
  if (coreKeywords.some(keyword => message.includes(keyword))) {
    console.info(`ℹ️ [${new Date().toLocaleTimeString()}] ${message}`, ...args);
  }
}

function logWarn(message, ...args) {
  // 警告信息始终显示
  console.warn(`⚠️ [${new Date().toLocaleTimeString()}] ${message}`, ...args);
}

function logError(message, ...args) {
  // 错误信息始终显示
  console.error(`❌ [${new Date().toLocaleTimeString()}] ${message}`, ...args);
}

// 🎯 核心监控日志 - 专门用于关键节点
function logCore(type, message, data = {}) {
  const timestamp = new Date().toLocaleTimeString();
  const icons = {
    network: '🌐',
    speed: '⚡', 
    auth: '🔐',
    success: '✅',
    error: '❌'
  };
  
  console.log(`${icons[type] || '📊'} [${timestamp}] ${message}`, data);
}

// ==================== Base64 转换模块 ====================
/**
 * 将二进制数据转换为 base64 字符串
 * @param {ArrayBuffer} buffer 二进制数据
 * @returns {string} base64 字符串
 */
function arrayBufferToBase64(buffer) {
  const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  let binary = '';
  const len = bytes.byteLength;
  
  // 处理大数据量 - 分块处理以避免堆栈溢出
  const chunkSize = 1024;
  for (let i = 0; i < len; i += chunkSize) {
    const chunk = bytes.slice(i, Math.min(i + chunkSize, len));
    const binaryChunk = Array.from(chunk).map(b => String.fromCharCode(b)).join('');
    binary += binaryChunk;
  }
  
  return btoa(binary);
}

/**
 * 从 base64 字符串转换为 ArrayBuffer
 * @param {string} base64 base64 字符串
 * @returns {ArrayBuffer} ArrayBuffer
 */
function base64ToArrayBuffer(base64) {
  const binaryString = window.atob(base64);
  const bytes = new Uint8Array(binaryString.length);
  for (let i = 0; i < binaryString.length; i++) {
    bytes[i] = binaryString.charCodeAt(i);
  }
  return bytes.buffer;
}

// ==================== HTTP 客户端模块 ====================
/**
 * HTTP请求工具类
 */
class HttpClient {
  /**
   * 发送二进制POST请求
   * @param {string} url 请求URL
   * @param {Uint8Array} data 二进制数据
   * @param {Object} options 请求选项
   * @returns {Promise<Object>} HTTP响应
   */
  static async postBinary(url, data, options = {}) {
    logDebug('HttpClient.postBinary', url, `Binary data length: ${data.length}`);

    try {
      const { 
        headers = {}, 
        timeout = 10000, // 鉴权请求10秒超时 
        mode = 'cors',
        responseType = 'binary'
      } = options;
      
      // 创建AbortController用于超时控制
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), timeout);
      
      // 设置请求头，避免CORS预检
      const defaultHeaders = {
        // 不设置默认Content-Type，让调用者决定
        ...headers
      };
      
      logDebug('HttpClient.postBinary headers:', defaultHeaders, 'mode:', mode);
      
      // 发送请求
      const response = await fetch(url, {
        method: 'POST',
        headers: defaultHeaders,
        body: data,
        signal: controller.signal,
        mode
      });
      
      // 清除超时定时器
      clearTimeout(timeoutId);
      
      // 获取响应头
      const responseHeaders = {};
      response.headers.forEach((value, key) => {
        responseHeaders[key] = value;
      });
      
      console.log('HttpClient.postBinary response:', response, "responseType:", responseType);
      
      // 根据响应类型处理数据
      if (responseType === 'binary') {
        try {
          let isReadable = response.body && !response.bodyUsed;
          console.log("response.body:", response.body, "response.bodyUsed:", response.bodyUsed, "isReadable: ", isReadable);
          // 检查是否为ReadableStream并且是否可读
          if (isReadable) {
            console.log('Response has a readable body stream');
            
            // 读取为ArrayBuffer
            const arrayBuffer = await response.arrayBuffer();
            console.log('ArrayBuffer created from response', arrayBuffer.byteLength);
            
            // 创建Uint8Array视图，直接返回二进制数据
            const binaryData = new Uint8Array(arrayBuffer);
            console.log('Binary data read from response', binaryData.length);
            
            return {
              status: response.status,
              data: binaryData,  // 直接返回二进制数据，不转换为base64
              headers: responseHeaders,
              isBinary: true
            };
          } else {
            console.warn('Response body stream is not available or already used');
            return {
              status: response.status,
              data: new Uint8Array(0),  // 返回空的二进制数组
              headers: responseHeaders,
              isBinary: true
            };
          }
        } catch (e) {
          console.error('Error processing binary response:', e);
          throw e;
        }
      } else {
        // 处理文本响应
        let responseText = '';
        try {
          responseText = await response.text();
          console.log('Text response received, length:', responseText.length);
        } catch (e) {
          console.error('Error reading response text:', e);
          throw e;
        }
        
        return {
          status: response.status,
          data: responseText,
          headers: responseHeaders
        };
      }
    } catch (error) {
      console.error('HttpClient.postBinary error:', error);
      return {
        status: 0,
        data: error instanceof Error ? error.message : String(error)
      };
    }
  }
}

// ==================== HTTP WASM 桥接模块 ====================
// 保存回调函数的映射表
const callbackMap = new Map();
let nextCallbackId = 1;

/**
 * 注册一个HTTP请求回调函数
 * @param {Function} callback 回调函数
 * @returns {number} 回调ID
 */
function registerHttpCallback(callback) {
  const callbackId = nextCallbackId++;
  callbackMap.set(callbackId, callback);
  return callbackId;
}

/**
 * 删除一个HTTP请求回调函数
 * @param {number} callbackId 回调ID
 */
function unregisterHttpCallback(callbackId) {
  callbackMap.delete(callbackId);
}

/**
 * 执行HTTP POST请求 - 二进制数据版本
 * @param {number} urlPtr C字符串指针，指向URL
 * @param {Uint8Array} data Uint8Array二进制数据
 * @param {number} callbackId 回调ID
 */
async function httpPostBinary(urlPtr, data, callbackId) {
  const wasmModule = globalThis.Module;
  if (!wasmModule) {
    logError('WASM module not loaded');
    return;
  }
  
  // 从WASM内存读取URL字符串
  let url = wasmModule.UTF8ToString(urlPtr);
  
  // 智能处理认证API路径：使用全局配置常量或默认服务器
  if (url === '/api/wauth/init/v2') {
    const originalUrl = url;
    
    // 🎯 静默检查配置源（不输出调试信息）
    
    // 认证服务器优先级检查（从高到低）
    let authServer = null;
    
    // 🎯 在WASM环境中正确获取window对象
    const getWindowObject = () => {
      // 尝试多种方式获取window对象
      return globalThis.window || 
             (typeof window !== 'undefined' ? window : null) ||
             (globalThis.self && globalThis.self.window) ||
             null;
    };
    
    const win = getWindowObject();
    
    
    // 1. 最高优先级：initSDK传入的authServer参数
    if (globalThis.thunderPlayerAuthServer) {
      authServer = globalThis.thunderPlayerAuthServer;
      logCore('auth', '使用initSDK传入的认证服务器', { server: authServer });
    }
    // 2. 第二优先级：thunder-config.js中的THUNDER_CONFIG.authServer配置
    else if (win && win.THUNDER_CONFIG && win.THUNDER_CONFIG.authServer) {
      authServer = win.THUNDER_CONFIG.authServer;
      logCore('auth', '使用thunder-config.js配置', { server: authServer });
    }
    // 3. 第三优先级：配置文件中的THUNDER_AUTH_SERVER常量
    else if (win && win.THUNDER_AUTH_SERVER) {
      authServer = win.THUNDER_AUTH_SERVER;
      logCore('auth', '使用window.THUNDER_AUTH_SERVER配置', { server: authServer });
    }
    // 4. 尝试全局变量
    else if (globalThis.THUNDER_AUTH_SERVER) {
      authServer = globalThis.THUNDER_AUTH_SERVER;
      logCore('auth', '使用全局THUNDER_AUTH_SERVER', { server: authServer });
    }
    // 5. 尝试直接访问配置常量
    else if (typeof THUNDER_AUTH_SERVER !== 'undefined') {
      authServer = THUNDER_AUTH_SERVER;
      logCore('auth', '使用常量THUNDER_AUTH_SERVER', { server: authServer });
    }
    
    // 6. 如果没有找到任何配置，使用hardcode配置
    if (!authServer) {
      // 🎯 构建时环境hardcode配置
      // 这个值会在构建时被替换为对应的环境配置
      authServer = 'BUILD_TIME_AUTH_SERVER_PLACEHOLDER';
      
      logCore('auth', 'Hardcode配置: 构建时环境');
    }
    
    
    // 确保authServer不以/结尾，避免双斜杠
    const cleanAuthServer = authServer.endsWith('/') ? authServer.slice(0, -1) : authServer;
    // 重写URL：移除/api前缀，因为服务器实际路径是/wauth/init/v2
    url = cleanAuthServer + '/wauth/init/v2';
    
    logDebug('认证URL重写', `${originalUrl} -> ${url}`);
  }
  
  // 🎯 核心监控：网络请求开始
  const startTime = performance.now();
  logCore('network', `认证请求开始 → ${url}`, { dataSize: data.length });
  
  try {
    // 发送HTTP请求，使用最简单的请求头避免CORS预检
    const response = await HttpClient.postBinary(url, data, {
      headers: {
        'Content-Type': 'text/plain'  // 使用简单Content-Type避免CORS预检
      },
      mode: 'cors',
      responseType: 'binary' // 指定响应类型为二进制
    });
    
    // 🎯 核心监控：网络响应和速度
    const endTime = performance.now();
    const responseTime = Math.round(endTime - startTime);
    
    if (response.status === 200) {
      logCore('success', `认证请求成功`, { 
        status: response.status, 
        responseTime: `${responseTime}ms`,
        dataSize: response.data?.length || 0
      });
      logCore('speed', `响应速度: ${responseTime}ms`);
    } else {
      // 🎯 触发错误事件到主线程
      if (typeof postMessage !== 'undefined') {
        postMessage({
          type: 'ERROR_EVENT',
          errorCode: 'NETWORK_FAILED_2001',
          message: '认证请求失败',
          details: { 
            status: response.status, 
            responseTime: responseTime 
          }
        });
      }
      
      logCore('error', `认证请求失败`, { 
        status: response.status, 
        responseTime: `${responseTime}ms` 
      });
    }

    // 处理响应数据 - 如果是Uint8Array，直接使用；如果是字符串，转换为二进制
    let binaryData;
    if (response.data instanceof Uint8Array) {
      binaryData = response.data;
    } else if (typeof response.data === 'string') {
      // 将字符串转换为Uint8Array
      const encoder = new TextEncoder();
      binaryData = encoder.encode(response.data);
    } else {
      logError('Unexpected response data type: ' + typeof response.data);
      binaryData = new Uint8Array(0);
    }
    
    if (binaryData.length === 0) {
      logWarn('Response binary data is empty, this might cause issues');
    } else {
      logDebug(`Got binary response data of ${binaryData.length} bytes`);
    }
    
    // 转换为base64编码的字符串
    let responseBase64 = '';
    try {
      // 使用安全的方法将二进制数据转换为base64字符串
      responseBase64 = arrayBufferToBase64(binaryData);
      
      // 在base64字符串前加上标记，表示这是base64编码的数据
      // 这样C代码可以知道如何处理
      const prefixedResponse = "BASE64:" + responseBase64;
      
      // 将base64字符串复制到WASM内存中
      let responsePtr = 0;
      try {
        // 为响应数据在WASM内存中分配空间
        responsePtr = wasmModule._malloc(prefixedResponse.length + 1);
        
        if (!responsePtr) {
          logError('Failed to allocate memory for response data');
          return;
        }
        
        // 将数据写入WASM内存
        wasmModule.stringToUTF8(prefixedResponse, responsePtr, prefixedResponse.length + 1);
        
        // 使用WASM的http_response_handler处理响应
        logDebug(`Calling WASM http_response_handler with ptr=${responsePtr}, status=${response.status}, callbackId=${callbackId}`);
        wasmModule._http_response_handler(responsePtr, response.status, callbackId);
      } finally {
        // 清理分配的内存
        if (responsePtr) {
          wasmModule._free(responsePtr);
          logDebug(`Freed WASM memory at address ${responsePtr}`);
        }
      }
    } catch (e) {
      logError('Error converting binary to base64: ' + e.message);
      return;
    }
  } catch (error) {
    logError('httpPostBinary error: ' + (error instanceof Error ? error.message : String(error)));
    
    // 在错误情况下也调用回调，但带有错误消息
    try {
      const wasmModule = globalThis.Module;
      const errorMessage = error instanceof Error ? error.message : String(error);
      
      // 为错误消息分配内存
      const errorPtr = wasmModule._malloc(errorMessage.length + 1);
      if (errorPtr) {
        try {
          wasmModule.stringToUTF8(errorMessage, errorPtr, errorMessage.length + 1);
          wasmModule._http_response_handler(errorPtr, 0, callbackId);
        } finally {
          wasmModule._free(errorPtr);
        }
      }
    } catch (allocError) {
      logError('Failed to report error to WASM: ' + (allocError instanceof Error ? allocError.message : String(allocError)));
    }
  }
}

// 导出到全局，供WASM调用
globalThis.ThunderPlayerBridge = {
  httpPostBinary,
  registerHttpCallback,
  unregisterHttpCallback
};

// 延迟初始化Logger，确保在common.js加载后调用
if (typeof self !== 'undefined') {
  self.addEventListener('message', function initLoggerOnce() {
    if (typeof Logger !== 'undefined') {
      initLogger();
      self.removeEventListener('message', initLoggerOnce);
    }
  });
} 