/*
 * libmedia ThunderStone解密IOLoader
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

import CustomIOLoader from './CustomIOLoader'
import ThunderStoneDecryptor, { ThunderStoneModule } from './ThunderStoneDecryptor'
import type { Uint8ArrayInterface } from 'common/io/interface'
import type { Data } from 'common/types/type'

/**
 * ThunderStone解密IOLoader配置
 */
export interface ThunderStoneIOLoaderOptions {
  /** ThunderStone WASM模块 */
  thunderModule: ThunderStoneModule
  /** 底层数据加载器 */
  baseLoader: CustomIOLoader
}

/**
 * ThunderStone解密IOLoader
 * 
 * 透明解密ThunderStone加密的媒体流：
 * - 自动检测512字节头部判断是否加密
 * - 按8KB块解密数据
 * - 支持seek操作
 * - 完全透明，libmedia其他模块无感知
 */
export default class ThunderStoneIOLoader extends CustomIOLoader {
  private options: ThunderStoneIOLoaderOptions
  private decryptor: ThunderStoneDecryptor
  private currentPos: bigint = 0n
  private headerBuffer: Uint8Array | null = null
  private isEncrypted: boolean = false

  constructor(options: ThunderStoneIOLoaderOptions) {
    super()
    this.options = options
    this.decryptor = new ThunderStoneDecryptor(options.thunderModule)
  }

  get ext(): string {
    return this.options.baseLoader.ext
  }

  get flags(): int32 {
    return this.options.baseLoader.flags
  }

  get name(): string {
    return `ThunderStone(${this.options.baseLoader.name})`
  }

  get minBuffer(): number {
    return this.options.baseLoader.minBuffer
  }

  public async open(): Promise<int32> {
    const result = await this.options.baseLoader.open()
    if (result < 0) {
      return result
    }

    // 读取512字节头部检查是否加密
    this.headerBuffer = new Uint8Array(512)
    const headerRead = await this.options.baseLoader.read(this.headerBuffer as Uint8ArrayInterface)
    
    if (headerRead < 512) {
      return -1 // 头部数据不足
    }

    // 检查是否为加密流
    this.isEncrypted = this.decryptor.checkHeader(this.headerBuffer)
    this.currentPos = 512n

    return 0
  }

  public async read(buffer: Uint8ArrayInterface, options?: Data): Promise<int32> {
    // 如果头部还未返回给调用方，先返回头部
    if (this.headerBuffer) {
      const len = Math.min(buffer.length, this.headerBuffer.length)
      buffer.set(this.headerBuffer.subarray(0, len))
      
      if (len < this.headerBuffer.length) {
        // 头部未读完，保留剩余部分
        this.headerBuffer = this.headerBuffer.subarray(len)
      }
      else {
        this.headerBuffer = null
      }
      return len
    }

    // 从底层加载器读取数据
    const bytesRead = await this.options.baseLoader.read(buffer, options)
    
    if (bytesRead <= 0) {
      return bytesRead
    }

    // 如果是加密流，需要解密
    if (this.isEncrypted) {
      // ThunderStone按8KB块加密，这里需要分块处理
      const BLOCK_SIZE = 8192
      const HEAD_SIZE = 512
      let offset = 0
      
      while (offset < bytesRead) {
        const blockSize = Math.min(BLOCK_SIZE, bytesRead - offset)
        const blockData = new Uint8Array(buffer.buffer, buffer.byteOffset + offset, blockSize)
        
        // 计算块索引（关键！）
        // 公式：(文件位置 - 512) / 8192
        const fileOffset = Number(this.currentPos) + offset
        const blockIndex = Math.floor((fileOffset - HEAD_SIZE) / BLOCK_SIZE)
        
        // 解密当前块
        const decrypted = this.decryptor.decrypt(blockData, blockIndex)
        buffer.set(decrypted, offset)
        
        offset += blockSize
      }
    }

    this.currentPos += BigInt(bytesRead)
    return bytesRead
  }

  public async write(buffer: Uint8ArrayInterface): Promise<int32> {
    return await this.options.baseLoader.write(buffer)
  }

  public async seek(pos: int64, options?: Data): Promise<int32> {
    // 计算块索引并通知解密器
    if (this.isEncrypted && pos >= 512n) {
      const HEAD_SIZE = 512
      const BLOCK_SIZE = 8192
      const blockIndex = Math.floor((Number(pos) - HEAD_SIZE) / BLOCK_SIZE)
      this.decryptor.seek(blockIndex)
    }
    
    const result = await this.options.baseLoader.seek(pos, options)
    if (result === 0) {
      this.currentPos = pos
      this.headerBuffer = null // 清空缓存的头部
    }
    
    return result
  }

  public async size(): Promise<int64> {
    return await this.options.baseLoader.size()
  }

  public async stop(): Promise<void> {
    this.decryptor.destroy()
    await this.options.baseLoader.stop()
  }
}
