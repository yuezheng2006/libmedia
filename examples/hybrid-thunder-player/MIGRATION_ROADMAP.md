# Thunder混合播放器改造路线图

## 🎯 改造目标

将当前的**基础混合播放器**（index.html）升级为**完整功能播放器**，达到或超越原有ThunderPlayer（Vue组件）的能力水平。

### 目标对比

| 维度 | 当前状态 | 目标状态 | 差距 |
|------|---------|---------|------|
| **核心播放** | ✅ 播放/停止 | ✅ 播放/暂停/停止 | 缺少暂停 |
| **进度控制** | ❌ 无 | ✅ 进度条+拖拽+点击跳转 | 全部缺失 |
| **音量控制** | ❌ 无 | ✅ 滑块+静音+快捷键 | 全部缺失 |
| **声道切换** | ❌ 无 | ✅ 原唱/伴唱+记忆 | 全部缺失 |
| **视频信息** | ✅ 简化版 | ✅ 完整面板+性能 | 需要增强 |
| **截图功能** | ❌ 无 | ✅ WebGL+剪切板 | 全部缺失 |
| **全屏控制** | ❌ 无 | ✅ 全屏+快捷键 | 全部缺失 |
| **播放列表** | ❌ 无 | ✅ 上一首/下一首+循环 | 全部缺失 |
| **快捷键** | ❌ 无 | ✅ 15+快捷键 | 全部缺失 |
| **UI设计** | ⚠️ 基础 | ✅ 现代化+动画 | 需要美化 |

---

## 📋 完整改造TODO（优先级排序）

### Phase 4：基础播放控制 ⭐⭐⭐ (2天)

**目标**：实现基本的播放/暂停/停止控制

#### Task 4.1：添加暂停功能

- [ ] 在`index.html`中添加"暂停"按钮
- [ ] 实现`pausePlayback()`函数
  ```javascript
  async function pausePlayback() {
    if (state.player && state.player.pause) {
      await state.player.pause()
      log('⏸️ 已暂停', 'info')
    }
  }
  ```
- [ ] 实现播放/暂停状态切换
- [ ] 更新按钮文本（播放 ↔ 暂停）

#### Task 4.2：播放状态监听

- [ ] 监听libmedia的`playing`事件
- [ ] 监听libmedia的`paused`事件
- [ ] 更新UI状态显示

**验收标准**：
- ✅ 点击暂停按钮，视频立即暂停
- ✅ 点击播放按钮，视频从暂停位置继续播放
- ✅ 按钮文本正确切换
- ✅ 状态面板显示"播放中"/"已暂停"

---

### Phase 5：进度控制 ⭐⭐⭐ (3天)

**目标**：实现完整的进度条功能（显示、拖拽、点击跳转）

#### Task 5.1：进度条UI

- [ ] 添加进度条HTML结构（B站风格）
  ```html
  <div class="progress-bar" id="progressBar">
    <div class="progress-buffered" id="progressBuffered"></div>
    <div class="progress-fill" id="progressFill"></div>
    <div class="progress-handle" id="progressHandle"></div>
  </div>
  ```
- [ ] 添加CSS样式（渐变、阴影、hover效果）
- [ ] 添加时间显示（当前时间/总时长）

#### Task 5.2：进度更新

- [ ] 监听`timeupdate`事件
  ```javascript
  state.player.on('timeupdate', (time) => {
    updateProgressBar(time)
  })
  ```
- [ ] 实现`updateProgressBar()`函数
- [ ] 实现`formatTime(seconds)`工具函数
- [ ] 更新缓冲进度显示

#### Task 5.3：点击跳转

- [ ] 实现`onProgressClick(event)`函数
  ```javascript
  function onProgressClick(event) {
    const rect = progressBar.getBoundingClientRect()
    const clickX = event.clientX - rect.left
    const clickPercent = (clickX / rect.width) * 100
    const targetTime = (duration * clickPercent) / 100
    seekTo(targetTime)
  }
  ```
- [ ] 实现`seekTo(targetTime)`函数
- [ ] 处理seek loading状态

#### Task 5.4：拖拽功能

- [ ] 实现`onHandleMouseDown(event)`
- [ ] 实现`onDocumentMouseMove(event)`
- [ ] 实现`onDocumentMouseUp()`
- [ ] 添加拖拽中的视觉反馈
- [ ] 处理拖拽边界检查

**验收标准**：
- ✅ 进度条实时更新（每秒刷新）
- ✅ 点击进度条，视频跳转到对应位置
- ✅ 拖拽手柄，视频跟随跳转
- ✅ 缓冲进度正确显示
- ✅ 时间显示格式正确（MM:SS）

---

### Phase 6：音量控制 ⭐⭐⭐ (1天)

**目标**：实现音量调节和静音功能

#### Task 6.1：音量UI

- [ ] 添加音量滑块HTML
  ```html
  <div class="volume-control">
    <button id="muteBtn" onclick="toggleMute()">🔊</button>
    <input type="range" id="volumeSlider" min="0" max="100" value="80" />
    <span id="volumeText">80%</span>
  </div>
  ```
- [ ] 添加CSS样式（滑块、图标）

#### Task 6.2：音量调节

- [ ] 实现`handleVolumeChange(event)`
  ```javascript
  async function handleVolumeChange(event) {
    const volume = parseInt(event.target.value) / 100  // 0-1
    await state.player.setVolume(volume)
    localStorage.setItem('thunder-player-volume', event.target.value)
  }
  ```
- [ ] 从LocalStorage恢复音量
- [ ] 更新音量文本显示

#### Task 6.3：静音功能

- [ ] 实现`toggleMute()`函数
- [ ] 保存静音状态到LocalStorage
- [ ] 更新图标（🔊 ↔ 🔇）

#### Task 6.4：快捷键

- [ ] 监听键盘事件（↑/↓键）
- [ ] 实现音量增加/减少（每次10%）
- [ ] M键切换静音

**验收标准**：
- ✅ 拖动滑块，音量实时变化
- ✅ 点击静音按钮，声音立即静音
- ✅ 音量设置在刷新后保持
- ✅ ↑/↓键可以调节音量
- ✅ M键可以切换静音

---

### Phase 7：声道切换 ⭐⭐ (1天)

**目标**：实现原唱/伴唱切换（双音轨视频）

#### Task 7.1：声道切换UI

- [ ] 添加声道切换按钮
  ```html
  <button id="audioTrackBtn" onclick="toggleAudioTrack()">
    <span id="audioTrackIcon">🎤</span>
    <span id="audioTrackText">原唱</span>
  </button>
  ```

#### Task 7.2：声道切换逻辑

- [ ] 实现`toggleAudioTrack()`函数
  ```javascript
  function toggleAudioTrack() {
    if (state.player && state.player.switchAudioTrack) {
      state.player.switchAudioTrack()
      // 更新UI
      const isOriginal = !isOriginal
      updateAudioTrackUI(isOriginal)
      localStorage.setItem('thunder-player-audio-track', isOriginal ? 'original' : 'accompaniment')
    }
  }
  ```
- [ ] 从LocalStorage恢复声道设置
- [ ] 切歌时保持声道设置

#### Task 7.3：快捷键

- [ ] A键切换声道

**验收标准**：
- ✅ 点击按钮，声道立即切换
- ✅ 按钮文本更新（原唱 ↔ 伴唱）
- ✅ 图标更新（🎤 ↔ 🎵）
- ✅ 声道设置在刷新后保持
- ✅ 切歌后声道设置不重置

---

### Phase 8：截图功能 ⭐⭐ (2天)

**目标**：实现视频截图并复制到剪切板

#### Task 8.1：截图按钮

- [ ] 添加截图按钮
  ```html
  <button id="screenshotBtn" onclick="takeScreenshot()" title="截屏 (S)">
    📷
  </button>
  ```

#### Task 8.2：WebGL截图

- [ ] 实现`tryWebGLScreenshot(canvas)`
  ```javascript
  async function tryWebGLScreenshot(canvas) {
    const gl = canvas.getContext('webgl')
    const pixels = new Uint8Array(width * height * 4)
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels)

    // 翻转Y轴 + 转换为blob
    const blob = await convertPixelsToBlob(pixels, width, height)
    return blob
  }
  ```
- [ ] 处理Y轴翻转
- [ ] 转换为PNG blob

#### Task 8.3：Canvas2D截图（降级方案）

- [ ] 实现`tryCanvasScreenshot(canvas)`
  ```javascript
  async function tryCanvasScreenshot(canvas) {
    return new Promise((resolve) => {
      canvas.toBlob((blob) => {
        resolve(blob)
      }, 'image/png')
    })
  }
  ```

#### Task 8.4：剪切板复制

- [ ] 实现`copyToClipboard(blob)`
  ```javascript
  async function copyToClipboard(blob) {
    await navigator.clipboard.write([
      new ClipboardItem({ 'image/png': blob })
    ])
    showMessage('✅ 截图已复制到剪切板')
  }
  ```
- [ ] 添加Toast提示
- [ ] 处理权限拒绝

#### Task 8.5：快捷键

- [ ] S键截图

**验收标准**：
- ✅ 点击按钮，截取当前画面
- ✅ 截图自动复制到剪切板
- ✅ Toast提示操作成功
- ✅ S键可以快速截图
- ✅ WebGL和Canvas2D都能正确截图

---

### Phase 9：全屏控制 ⭐ (1天)

**目标**：实现全屏播放功能

#### Task 9.1：全屏按钮

- [ ] 添加全屏按钮
  ```html
  <button id="fullscreenBtn" onclick="toggleFullscreen()" title="全屏 (F)">
    ⛶
  </button>
  ```

#### Task 9.2：全屏切换

- [ ] 实现`toggleFullscreen()`函数
  ```javascript
  function toggleFullscreen() {
    if (!document.fullscreenElement) {
      videoContainer.requestFullscreen()
    } else {
      document.exitFullscreen()
    }
  }
  ```
- [ ] 监听`fullscreenchange`事件
- [ ] 更新按钮图标（⛶ ↔ ⛶̅）

#### Task 9.3：全屏样式

- [ ] 添加全屏CSS
  ```css
  .video-container:-webkit-full-screen { /* 全屏样式 */ }
  .video-container:fullscreen { /* 全屏样式 */ }
  ```

#### Task 9.4：快捷键

- [ ] F键切换全屏
- [ ] ESC键退出全屏（浏览器默认）

**验收标准**：
- ✅ 点击按钮，视频全屏显示
- ✅ 全屏状态下控制栏仍然可用
- ✅ 退出全屏后恢复正常
- ✅ F键可以快速全屏
- ✅ ESC键可以退出全屏

---

### Phase 10：播放列表 ⭐⭐ (2天)

**目标**：支持多首歌曲播放、上一首/下一首、循环模式

#### Task 10.1：播放列表UI

- [ ] 添加播放列表HTML
  ```html
  <div class="playlist-panel">
    <div class="playlist-header">
      <h3>播放列表</h3>
      <span id="playlistCount">0 首歌曲</span>
    </div>
    <div id="playlistContainer"></div>
  </div>
  ```
- [ ] 实现播放列表项渲染

#### Task 10.2：上一首/下一首

- [ ] 添加上一首/下一首按钮
- [ ] 实现`playPrevious()`函数
  ```javascript
  async function playPrevious() {
    if (state.currentIndex > 0) {
      state.currentIndex--
      await switchSong(state.currentIndex)
    } else if (state.repeatMode === 'repeat-all') {
      state.currentIndex = state.playlist.length - 1
      await switchSong(state.currentIndex)
    }
  }
  ```
- [ ] 实现`playNext()`函数
- [ ] 实现`switchSong(index)`函数

#### Task 10.3：循环模式

- [ ] 添加循环模式按钮
- [ ] 实现三种模式切换
  - 顺序播放（播完停止）
  - 列表循环（播完回到第一首）
  - 单曲循环（重复播放当前歌曲）
- [ ] 实现`toggleRepeatMode()`函数
- [ ] 更新按钮图标（🔁 / 🔂）

#### Task 10.4：自动播放下一首

- [ ] 监听`ended`事件
  ```javascript
  state.player.on('ended', () => {
    if (state.repeatMode === 'repeat-one') {
      seekTo(0)
      state.player.play()
    } else if (state.repeatMode === 'repeat-all' || state.currentIndex < state.playlist.length - 1) {
      playNext()
    }
  })
  ```

#### Task 10.5：快捷键

- [ ] L键切换循环模式
- [ ] Shift+←键上一首
- [ ] Shift+→键下一首

**验收标准**：
- ✅ 显示播放列表，显示歌曲数量
- ✅ 点击播放列表项，切换到对应歌曲
- ✅ 点击上一首，播放上一首歌曲
- ✅ 点击下一首，播放下一首歌曲
- ✅ 三种循环模式正确工作
- ✅ 播放结束后根据循环模式自动播放

---

### Phase 11：快捷键系统 ⭐⭐ (1天)

**目标**：实现完整的快捷键支持

#### Task 11.1：快捷键映射表

- [ ] 创建快捷键配置
  ```javascript
  const KEYBOARD_SHORTCUTS = {
    ' ': { action: 'togglePlay', description: '播放/暂停' },
    'ArrowLeft': { action: 'seekBackward', description: '后退5秒' },
    'ArrowRight': { action: 'seekForward', description: '前进5秒' },
    'ArrowUp': { action: 'volumeUp', description: '音量+10%' },
    'ArrowDown': { action: 'volumeDown', description: '音量-10%' },
    'M': { action: 'toggleMute', description: '静音/取消静音' },
    'F': { action: 'toggleFullscreen', description: '全屏/退出全屏' },
    'A': { action: 'toggleAudioTrack', description: '原唱/伴唱' },
    'S': { action: 'takeScreenshot', description: '截屏' },
    'L': { action: 'toggleRepeatMode', description: '循环模式' },
    'Tab': { action: 'toggleVideoInfo', description: '媒体信息' },
    '?': { action: 'showHelp', description: '快捷键帮助' }
  }
  ```

#### Task 11.2：快捷键监听

- [ ] 实现`handleKeyDown(event)`函数
  ```javascript
  document.addEventListener('keydown', (event) => {
    if (event.target.tagName === 'INPUT') return  // 忽略输入框

    const shortcut = KEYBOARD_SHORTCUTS[event.key]
    if (shortcut) {
      event.preventDefault()
      executeShortcut(shortcut.action)
    }
  })
  ```

#### Task 11.3：快捷键帮助面板

- [ ] 添加快捷键帮助UI
- [ ] ?键显示快捷键列表
- [ ] ESC键关闭帮助面板

**验收标准**：
- ✅ 所有快捷键正常工作
- ✅ 输入框中快捷键不触发
- ✅ ?键显示快捷键帮助
- ✅ 快捷键帮助面板美观清晰

---

### Phase 12：UI优化 ⭐⭐ (3天)

**目标**：提升UI美观度和用户体验

#### Task 12.1：中央播放按钮

- [ ] 添加中央大播放按钮（暂停时显示）
  ```html
  <div class="center-play-control" v-if="!isPlaying">
    <div class="center-play-btn">
      <svg class="play-icon"><!-- Play图标 --></svg>
    </div>
  </div>
  ```
- [ ] 添加动画效果（放大/缩小）
- [ ] 点击画布播放/暂停

#### Task 12.2：Toast提示

- [ ] 实现`showMessage(text, type)`函数
  ```javascript
  function showMessage(text, type = 'info') {
    const toast = document.createElement('div')
    toast.className = `toast toast-${type}`
    toast.textContent = text
    document.body.appendChild(toast)

    setTimeout(() => toast.classList.add('show'), 10)
    setTimeout(() => {
      toast.classList.remove('show')
      setTimeout(() => toast.remove(), 300)
    }, 2000)
  }
  ```
- [ ] 添加Toast CSS（淡入/淡出动画）

#### Task 12.3：加载动画

- [ ] 添加视频加载动画（Spinner）
- [ ] 添加切换加载蒙层
- [ ] 显示加载进度文本

#### Task 12.4：媒体信息面板增强

- [ ] 添加视频编码信息（H264/AAC）
- [ ] 添加分辨率显示（1920x1080）
- [ ] 添加FPS显示（实时帧率）
- [ ] 添加FIFO使用率（流控状态）
- [ ] 添加比特率显示

#### Task 12.5：响应式设计

- [ ] 添加移动端媒体查询
  ```css
  @media (max-width: 768px) {
    .control-layout { flex-direction: column; }
    .volume-slider { width: 100%; }
  }
  ```
- [ ] 优化触摸交互
- [ ] 缩小按钮间距

#### Task 12.6：主题优化

- [ ] 统一配色方案（暗色主题）
- [ ] 添加渐变背景
- [ ] 添加阴影和圆角
- [ ] 优化图标（使用SVG）

**验收标准**：
- ✅ 中央播放按钮美观，动画流畅
- ✅ Toast提示显示清晰，自动消失
- ✅ 加载动画友好，避免白屏
- ✅ 媒体信息面板完整详细
- ✅ 移动端适配良好
- ✅ 整体UI现代美观

---

## 📊 工作量总结

| Phase | 模块 | 优先级 | 工作量 | 累计 |
|-------|------|-------|-------|------|
| Phase 4 | 基础播放控制 | ⭐⭐⭐ | 2天 | 2天 |
| Phase 5 | 进度控制 | ⭐⭐⭐ | 3天 | 5天 |
| Phase 6 | 音量控制 | ⭐⭐⭐ | 1天 | 6天 |
| Phase 7 | 声道切换 | ⭐⭐ | 1天 | 7天 |
| Phase 8 | 截图功能 | ⭐⭐ | 2天 | 9天 |
| Phase 9 | 全屏控制 | ⭐ | 1天 | 10天 |
| Phase 10 | 播放列表 | ⭐⭐ | 2天 | 12天 |
| Phase 11 | 快捷键系统 | ⭐⭐ | 1天 | 13天 |
| Phase 12 | UI优化 | ⭐⭐ | 3天 | 16天 |
| **总计** | | | **16天** | |

---

## 🚀 实施策略

### 建议开发顺序

**第一阶段（1周）- 核心功能**：
1. Phase 4: 基础播放控制 (2天)
2. Phase 5: 进度控制 (3天)
3. Phase 6: 音量控制 (1天)
4. Phase 7: 声道切换 (1天)

**第二阶段（1周）- 高级功能**：
5. Phase 8: 截图功能 (2天)
6. Phase 9: 全屏控制 (1天)
7. Phase 10: 播放列表 (2天)
8. Phase 11: 快捷键系统 (1天)

**第三阶段（3天）- UI打磨**：
9. Phase 12: UI优化 (3天)

### 里程碑

**里程碑1（7天后）**：核心功能完成
- ✅ 播放/暂停/停止
- ✅ 进度条拖拽
- ✅ 音量调节
- ✅ 声道切换

**里程碑2（14天后）**：高级功能完成
- ✅ 截图功能
- ✅ 全屏播放
- ✅ 播放列表
- ✅ 快捷键支持

**里程碑3（16天后）**：UI完善
- ✅ 现代化UI
- ✅ 动画效果
- ✅ 响应式设计
- ✅ 完整用户体验

---

## 📝 技术栈选择

### 纯HTML/JS vs Vue组件

**选项A：继续使用纯HTML/JS**
- ✅ 优势：
  - 无需构建工具
  - 部署简单（直接打开HTML）
  - 轻量级（无框架依赖）
  - 学习成本低
- ❌ 劣势：
  - 代码组织性差
  - 状态管理复杂
  - 无响应式绑定
  - 难以维护

**选项B：迁移到Vue组件**
- ✅ 优势：
  - 响应式数据绑定
  - 组件化开发
  - 代码复用性强
  - 可继承ThunderPlayer代码
- ❌ 劣势：
  - 需要构建工具（Vite）
  - 部署稍复杂
  - 学习曲线

**推荐方案**：
- **Phase 4-9**：继续使用纯HTML/JS（快速验证功能）
- **Phase 10-12**：考虑迁移到Vue（长期维护）

### 代码复用策略

**从ThunderPlayer复用**：
- ✅ 可直接复用：
  - `composables/useVolumeControl.js`
  - `composables/useProgressControl.js`
  - `composables/useProgressDrag.js`
  - `utils/index.js` (formatTime, showMessage)
- ⚠️ 需要适配：
  - `composables/usePlayerControl.js` (适配libmedia API)
- ❌ 不可复用：
  - Vue特定代码（template, setup, emit）

**迁移步骤**：
1. 先用纯JS实现功能（验证可行性）
2. 创建Vue组件封装（`HybridThunderPlayer.vue`）
3. 复用composables和utils
4. 测试并优化

---

## 🧪 测试计划

### 功能测试矩阵

| 功能 | 明文视频 | 加密视频 | 多音轨 | 高清 | 移动端 |
|------|---------|---------|-------|------|--------|
| 播放/暂停 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 进度拖拽 | ✅ | ✅ | ✅ | ✅ | ⚠️ |
| 音量调节 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 声道切换 | N/A | N/A | ✅ | ✅ | ✅ |
| 截图 | ✅ | ✅ | ✅ | ✅ | ⚠️ |
| 全屏 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 播放列表 | ✅ | ✅ | ✅ | ✅ | ✅ |

### 浏览器兼容性测试

| 浏览器 | 版本 | WebCodecs | 关键功能 | 状态 |
|--------|------|-----------|---------|------|
| Chrome | 94+ | ✅ | 全部 | 主要支持 |
| Edge | 94+ | ✅ | 全部 | 主要支持 |
| Firefox | 最新 | ❌ | WASM软解 | 降级支持 |
| Safari | 15+ | ❌ | WASM软解 | 降级支持 |

### 性能测试指标

| 指标 | 目标 | 测试方法 |
|------|------|---------|
| 首次播放延迟 | <2s | 点击播放到首帧显示 |
| 切歌延迟 | <1s | 点击下一首到首帧显示 |
| 内存占用 | <100MB | 播放10分钟后的内存 |
| CPU占用 | <20% | 播放1080p视频（WebCodecs） |
| FIFO使用率 | 50%-80% | 下载播放过程中的稳定性 |

---

## 🔧 开发环境准备

### 1. 安装依赖

```bash
# 进入项目目录
cd /Users/vincentyang/Documents/Github/libmedia/libmedia

# 安装npm依赖（如果需要）
npm install

# 启动开发服务器
npm run server
```

### 2. 开发工具

**必需**：
- Chrome 94+ / Edge 94+ （WebCodecs支持）
- VS Code / WebStorm（代码编辑器）
- Git（版本控制）

**推荐插件（VS Code）**：
- Live Server（实时预览）
- ESLint（代码检查）
- Prettier（代码格式化）
- Vue Language Features（如果迁移到Vue）

### 3. 调试配置

**Chrome DevTools设置**：
```
1. 打开 chrome://flags
2. 启用 "Experimental Web Platform features"
3. 启用 "SharedArrayBuffer in non-isolated pages via COOP/COEP"
4. 重启浏览器
```

**WASM调试**：
```javascript
// 启用WASM日志
Module._initDecoder(fileSize, 1, enableDecryption)  // logLevel=1
```

---

## 📚 参考资料

### 文档

- [ARCHITECTURE_V2.md](./ARCHITECTURE_V2.md) - V2架构说明
- [TECHNICAL_SPECIFICATION.md](./TECHNICAL_SPECIFICATION.md) - 技术规范
- [SOLUTION.md](./SOLUTION.md) - 解决方案记录

### 原有代码

- ThunderPlayer组件：`.temp/thunderwebplayer/packages/webplayer-vue/src/components/ThunderPlayer/`
- libmedia文档：`dist/avplayer/README.md`
- Thunder WASM：`wasm/thunder-demuxer/`

### 外部资源

- [WebCodecs API](https://developer.mozilla.org/en-US/docs/Web/API/WebCodecs_API)
- [libmedia GitHub](https://github.com/zhaohappy/libmedia)
- [FFmpeg.wasm](https://ffmpegwasm.netlify.app/)

---

## ✅ 验收标准（最终目标）

### 功能完整性

- [ ] 播放控制：播放、暂停、停止、上一首、下一首、循环模式
- [ ] 进度控制：进度条、拖拽、点击跳转、缓冲进度、时间显示
- [ ] 音量控制：音量滑块、静音、快捷键、记忆功能
- [ ] 声道切换：原唱/伴唱、快捷键、记忆功能
- [ ] 视频信息：媒体信息面板、编码格式、性能指标
- [ ] 截图功能：WebGL/Canvas2D截屏、剪切板复制、快捷键
- [ ] 全屏控制：全屏切换、快捷键
- [ ] 播放列表：列表显示、切歌、自动播放
- [ ] 快捷键：15+快捷键、帮助面板

### 性能指标

- [ ] 首次播放延迟 <2秒
- [ ] 切歌延迟 <1秒
- [ ] 内存占用 <100MB
- [ ] CPU占用 <20%（WebCodecs）
- [ ] FIFO流控稳定（50%-80%）

### 用户体验

- [ ] UI美观现代
- [ ] 动画流畅自然
- [ ] Toast提示友好
- [ ] 加载状态清晰
- [ ] 移动端适配良好
- [ ] 快捷键直观易用

### 代码质量

- [ ] 代码结构清晰
- [ ] 注释完整详细
- [ ] 无严重bug
- [ ] 性能优化到位
- [ ] 兼容性测试通过

---

**文档版本**：v1.0
**创建日期**：2025年
**预计完成**：16个工作日
**当前Phase**：Phase 3 已完成 ✅
**下一Phase**：Phase 4 - 基础播放控制
