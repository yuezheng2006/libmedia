/*
 * libmedia ThunderStone解密器
 *
 * 版权所有 (C) 2025 赵高兴
 * Copyright (C) 2025 Gaoxing Zhao
 *
 * 此文件是 libmedia 的一部分
 * This file is part of libmedia.
 * 
 * libmedia 是自由软件；您可以根据 GNU Lesser General Public License（GNU LGPL）3.1
 * 或任何其更新的版本条款重新分发或修改它
 * libmedia is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.1 of the License, or (at your option) any later version.
 *
 * libmedia 希望能够为您提供帮助，但不提供任何明示或暗示的担保，包括但不限于适销性或特定用途的保证
 * 您应该在程序分发时收到一份 GNU Lesser General Public License 的副本。如果没有，请参阅 <http://www.gnu.org/licenses/>
 * libmedia is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libmedia. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * ThunderStone加密格式说明：
 * - 前512字节为加密头（magic + metadata）
 * - 之后每8KB为一个加密块
 * - 使用AES-128-CBC加密算法
 * 
 * ⚠️ 重要：ThunderStone的 tsCheckDecrypt 返回值逻辑：
 *   - 返回 0: 表示加密流
 *   - 返回 -3: 表示明文流
 *   （与常规错误码逻辑相反！）
 */

/**
 * Emscripten WASM模块标准接口
 */
interface EmscriptenModule {
  _malloc: (size: number) => number
  _free: (ptr: number) => void
  HEAPU8: Uint8Array
}

export interface ThunderStoneModule extends EmscriptenModule {
  _tsInitDecrypt: () => number
  _tsDeinitDecrypt: (handle: number) => void
  _tsCheckDecrypt: (handle: number, bufferPtr: number, size: number) => number
  _tsDataDecrypt: (handle: number, bufferPtr: number, size: number) => number
  _tsDataDecryptSeek: (handle: number, blockIndex: number) => void
}

export default class ThunderStoneDecryptor {
  private module: ThunderStoneModule
  private handle: number
  private isEncrypted: boolean = false
  private headerChecked: boolean = false

  constructor(module: ThunderStoneModule) {
    this.module = module
    this.handle = this.module._tsInitDecrypt()
    if (!this.handle) {
      throw new Error('ThunderStone解密器初始化失败')
    }
  }

  /**
   * 检查512字节头部是否为加密流
   * @param header 512字节头部数据
   * @returns true表示加密流，false表示明文流
   * 
   * ⚠️ 注意：ThunderStone的 tsCheckDecrypt 返回值逻辑：
   *   - 返回 0 = 加密流
   *   - 返回 -3 = 明文流
   */
  public checkHeader(header: Uint8Array): boolean {
    if (header.length < 512) {
      throw new Error('头部数据必须至少512字节')
    }

    const bufferPtr = this.module._malloc(512)
    try {
      this.module.HEAPU8.set(header.subarray(0, 512), bufferPtr)
      const result = this.module._tsCheckDecrypt(this.handle, bufferPtr, 512)
      
      // ⚠️ 注意：ThunderStone逻辑相反：
      // result == 0 表示加密流
      // result == -3 表示明文流
      this.isEncrypted = (result === 0)
      this.headerChecked = true
      
      return this.isEncrypted
    }
    finally {
      this.module._free(bufferPtr)
    }
  }

  /**
   * 解密数据块
   * @param data 待解密数据（最大8KB）
   * @param blockIndex 块索引（从0开始计数）
   * @returns 解密后的数据
   * 
   * ⚠️ 注意：blockIndex计算公式：(文件位置 - 512) / 8192
   * 因为前512字节是明文头部，加密数据从512字节开始
   * ⚠️ 重要：必须先调用_tsDataDecryptSeek设置块位置，再调用_tsDataDecrypt解密
   */
  public decrypt(data: Uint8Array, blockIndex: number): Uint8Array {
    if (!this.headerChecked) {
      throw new Error('必须先调用checkHeader检查头部')
    }

    // 如果不是加密流，直接返回原始数据
    if (!this.isEncrypted) {
      return data
    }

    const bufferPtr = this.module._malloc(data.length)
    try {
      this.module.HEAPU8.set(data, bufferPtr)
      
      // 先设置块位置（关键！）
      this.module._tsDataDecryptSeek(this.handle, blockIndex)
      
      // 然后解密（⚠️ 只传3个参数！）
      const result = this.module._tsDataDecrypt(
        this.handle,
        bufferPtr,
        data.length
      )

      if (result !== 0) {
        throw new Error(`ThunderStone解密失败: ${result}`)
      }

      // 从WASM内存复制解密后的数据
      return new Uint8Array(
        this.module.HEAPU8.buffer,
        bufferPtr,
        data.length
      ).slice()
    }
    finally {
      this.module._free(bufferPtr)
    }
  }

  /**
   * Seek操作时需要通知解密器
   * @param blockIndex 块索引
   */
  public seek(blockIndex: number): void {
    if (this.isEncrypted) {
      this.module._tsDataDecryptSeek(this.handle, blockIndex)
    }
  }

  /**
   * 销毁解密器，释放资源
   */
  public destroy(): void {
    if (this.handle) {
      this.module._tsDeinitDecrypt(this.handle)
      this.handle = 0
    }
  }
}
