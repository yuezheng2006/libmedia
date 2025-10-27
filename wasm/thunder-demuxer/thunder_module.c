#include <emscripten.h>
#include <string.h>
#include "http_client.h"


// 删除不再需要的函数声明
// extern int init_auth_wrapper(const char* appid, const char* uid, const char* sdk_sn);
// extern int get_auth_status_wrapper();

// 静态版本字符串
static const char VERSION_STRING[] = "1.0.0";

// 声明外部JS函数
// extern void js_http_post(const char* url, const char* data, int callback_id);
// extern void js_http_post_binary(const char* url, const unsigned char* data, size_t data_len, int callback_id);

// 导出到JavaScript的接口
// 必须使用EMSCRIPTEN_KEEPALIVE宏标记，以防被优化掉

/**
 * 导出函数：获取版本信息
 * 返回一个指向版本字符串的指针
 */
EMSCRIPTEN_KEEPALIVE
const char* js_get_version() {
    // 打印调试信息
    security_log_fmt(LOG_LEVEL_INFO, LOG_MSG_VERSION, "Version: %s", VERSION_STRING);
    return VERSION_STRING;
}


// /**
//  * 导出函数：发送二进制数据的HTTP POST请求（JS桥接）
//  * 注意：此函数只是为了导出，实际由JavaScript桥接模块调用
//  */
// EMSCRIPTEN_KEEPALIVE
// void js_http_post_binary(const char* url, const unsigned char* data, size_t data_len, int callback_id) {
//     // 此函数声明用于导出，实际实现在JavaScript桥接模块中
//     // 仅供编译器识别，防止编译错误
//     emscripten_log(EM_LOG_CONSOLE, "js_http_post_binary called");
// }


// 回调函数定义
void auth_callback(const char* response, int status, void* user_data);

// 导出鉴权相关函数给JavaScript/WebAssembly
/**
 * 导出函数：初始化授权
 * @param appid 应用ID
 * @param uid 用户ID
 * @param sdk_sn SDK序列号
 * @param response_data 响应数据缓冲区
 * @param response_size 响应数据缓冲区大小
 * @return HTTP状态码（成功）或错误码（失败）
 */
EMSCRIPTEN_KEEPALIVE
int js_init_auth(const char* appid, const char* uid, const char* sdk_sn, char* response_data, int response_size) {
    // 创建一个静态响应回调上下文
    static ModuleResponseContext ctx = {{0}, 0, 0};
    
    // 重置上下文
    ctx.buffer[0] = '\0';
    ctx.status = 0;
    ctx.complete = 0;
    
    // 重置全局授权状态
    g_auth_status = 0;
    security_log_fmt(LOG_LEVEL_INFO, LOG_MSG_AUTH_START, "%s", appid);
    
    // 调用授权方法
    int result = init_auth(appid, uid, sdk_sn, auth_callback, &ctx);
    if (result != 0) {
        security_log_fmt(LOG_LEVEL_ERROR, LOG_ERR_HTTP_REQ, "Error code: %d", result);
        return result;
    }
    
    // 不在C端等待，直接返回0表示请求已发送
    // JavaScript端将通过轮询方式检查鉴权状态
    security_log(LOG_LEVEL_INFO, LOG_MSG_REQ_SENT, "Async auth request sent");
    
    // 返回0表示请求已发送，需要通过get_auth_status_wrapper获取最终状态
    return 0;
}

// 回调函数实现
void auth_callback(const char* response, int status, void* user_data) {
    ModuleResponseContext *ctx = (ModuleResponseContext*)user_data;
    
    if (ctx) {
        if (response) {
            strncpy(ctx->buffer, response, MAX_RESPONSE_SIZE - 1);
            ctx->buffer[MAX_RESPONSE_SIZE - 1] = '\0';
        } else {
            ctx->buffer[0] = '\0';
        }
        
        ctx->status = status;
        ctx->complete = 1;
        
        security_log_fmt(LOG_LEVEL_INFO, LOG_MSG_HTTP_RESP_COMPLETE, "Status: %d", status);
    }
}

// 导出鉴权相关函数给JavaScript/WebAssembly
/**
 * 导出函数：获取当前授权状态
 * @return 授权状态：0-未授权，1-已授权，-1-授权失败
 */
EMSCRIPTEN_KEEPALIVE
int get_auth_status_wrapper() {
    // 从http_client.c中获取鉴权状态
    extern int get_auth_status();
    int status = get_auth_status();
    
    // 输出日志，便于调试
    security_log_fmt(LOG_LEVEL_INFO, LOG_MSG_AUTH_STATUS, "Status: %d", status);
    
    return status;
}

// ✅ 新增：导出readOnePacket函数和stream metadata函数
// 声明decoder.c中的函数
extern int readOnePacket();
extern void setPacketCallback(void *callback);
extern int getVideoStreamIndex();
extern int getAudioStreamIndex();
extern int getVideoCodecId();
extern int getAudioCodecId();
extern int getVideoWidth();
extern int getVideoHeight();
extern int getAudioSampleRate();
extern int getAudioChannels();
extern int readFromFIFO(unsigned char *buffer, int size);

EMSCRIPTEN_KEEPALIVE
int js_readOnePacket() {
    return readOnePacket();
}

EMSCRIPTEN_KEEPALIVE
void js_setPacketCallback(void *callback) {
    setPacketCallback(callback);
}

EMSCRIPTEN_KEEPALIVE
int js_getVideoStreamIndex() {
    return getVideoStreamIndex();
}

EMSCRIPTEN_KEEPALIVE
int js_getAudioStreamIndex() {
    return getAudioStreamIndex();
}

EMSCRIPTEN_KEEPALIVE
int js_getVideoCodecId() {
    return getVideoCodecId();
}

EMSCRIPTEN_KEEPALIVE
int js_getAudioCodecId() {
    return getAudioCodecId();
}

EMSCRIPTEN_KEEPALIVE
int js_getVideoWidth() {
    return getVideoWidth();
}

EMSCRIPTEN_KEEPALIVE
int js_getVideoHeight() {
    return getVideoHeight();
}

EMSCRIPTEN_KEEPALIVE
int js_getAudioSampleRate() {
    return getAudioSampleRate();
}

EMSCRIPTEN_KEEPALIVE
int js_getAudioChannels() {
    return getAudioChannels();
}

// ✅ 新增：导出readFromFIFO函数（直接读取FIFO中的原始TS流）
EMSCRIPTEN_KEEPALIVE
int js_readFromFIFO(unsigned char *buffer, int size) {
    return readFromFIFO(buffer, size);
}

// ✅ 新增：导出getFIFOSize函数（查询FIFO当前使用量，用于流控）
extern int getFIFOSize();

EMSCRIPTEN_KEEPALIVE
int js_getFIFOSize() {
    return getFIFOSize();
}

// 模块初始化函数
int main() {
    // 输出初始化消息
    security_log_fmt(LOG_LEVEL_INFO, LOG_MSG_MODULE_INIT, "Version: %s", VERSION_STRING);
    return 0;
} 