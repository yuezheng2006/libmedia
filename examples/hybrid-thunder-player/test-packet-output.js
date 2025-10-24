/**
 * 测试packet输出功能
 * 在浏览器控制台执行此脚本
 */

console.log('🔬 开始测试packet输出功能...');

// 步骤1：检查函数是否导出
console.log('\n步骤1：检查新增函数');
console.log('_js_setPacketCallback:', typeof Module._js_setPacketCallback);
console.log('_js_readOnePacket:', typeof Module._js_readOnePacket);

if (typeof Module._js_setPacketCallback !== 'function') {
    console.error('❌ _js_setPacketCallback 未导出！需要重新刷新页面加载新WASM');
} else {
    console.log('✅ 函数导出正常');
}

// 步骤2：创建packet回调
console.log('\n步骤2：创建packet回调');

window.testPacketCallback = Module.addFunction(
    function(stream_type, dataPtr, size, pts, dts, flags) {
        const streamName = stream_type === 0 ? 'VIDEO' : 'AUDIO';
        const isKeyframe = (flags & 1) !== 0;

        console.log(`\n🎬 收到${streamName} packet:`, {
            size: size + ' bytes',
            pts: pts,
            dts: dts,
            keyframe: isKeyframe ? '✓' : '✗'
        });

        // 读取前16字节
        const data = new Uint8Array(Module.HEAPU8.buffer, dataPtr, Math.min(16, size));
        const hex = Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' ');
        console.log('  前16字节:', hex);

        // H264分析
        if (stream_type === 0 && size >= 5) {
            // 检查NAL header
            if (data[0] === 0 && data[1] === 0 && data[2] === 0 && data[3] === 1) {
                const nalType = data[4] & 0x1F;
                const nalNames = {
                    1: 'Non-IDR slice',
                    5: 'IDR slice (关键帧)',
                    6: 'SEI',
                    7: 'SPS',
                    8: 'PPS',
                    9: 'AU delimiter'
                };
                console.log(`  ✅ H264 NAL类型: ${nalType} (${nalNames[nalType] || '其他'})`);
            } else if (data[0] === 0 && data[1] === 0 && data[2] === 1) {
                const nalType = data[3] & 0x1F;
                console.log(`  ✅ H264 NAL类型 (3字节起始码): ${nalType}`);
            }
        }

        // AAC分析
        if (stream_type === 1 && size >= 2) {
            if ((data[0] === 0xFF && (data[1] & 0xF0) === 0xF0)) {
                console.log('  ✅ AAC ADTS header');
            }
        }
    },
    'viiiiii'  // void(int, int, int, int64, int64, int)
);

console.log('✅ Packet回调函数已创建:', window.testPacketCallback);

// 步骤3：设置回调到decoder
console.log('\n步骤3：将回调设置到decoder');
Module._js_setPacketCallback(window.testPacketCallback);
console.log('✅ 回调已设置');

// 步骤4：测试读取packet
console.log('\n步骤4：开始读取packet');
console.log('⚠️ 注意：需要先播放视频，确保decoder已初始化并有数据');

// 提供测试函数
window.testReadPackets = function(count = 10) {
    console.log(`\n📖 读取 ${count} 个packets...`);
    let successCount = 0;
    let errorCount = 0;

    for (let i = 0; i < count; i++) {
        const result = Module._js_readOnePacket();
        if (result === 0) {
            successCount++;
        } else {
            errorCount++;
            if (errorCount === 1) {
                console.warn(`⚠️ readOnePacket返回: ${result} (可能是EOF或无数据)`);
            }
        }
    }

    console.log(`\n✅ 读取完成: 成功${successCount}个, 错误${errorCount}个`);
    return { successCount, errorCount };
};

console.log('\n📋 测试步骤总结:');
console.log('1. ✅ 函数检查完成');
console.log('2. ✅ 回调创建完成');
console.log('3. ✅ 回调设置完成');
console.log('4. ⏸️  等待视频播放后，执行: window.testReadPackets(10)');
console.log('\n提示：点击"加载并播放"按钮后，再运行 window.testReadPackets()');
