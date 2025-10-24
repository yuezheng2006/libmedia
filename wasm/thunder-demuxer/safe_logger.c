#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <emscripten.h>
#include "http_client.h"

// 全局错误信息
int g_error_code = 0;
char g_error_message[256] = {0};

// 消息ID到字符串的映射表 - 刻意使用不容易联想的消息内容
static const char* message_table[] = {
    // 普通日志消息 (0-99)
    [LOG_MSG_INIT] = "System initialized",
    [LOG_MSG_VERSION] = "Version information",
    [LOG_MSG_MODULE_INIT] = "Module ready",
    [LOG_MSG_REQ_SENT] = "Request processed",
    [LOG_MSG_RESP_RECV] = "Response received",
    
    // RSA相关日志 (100-199)
    [LOG_MSG_RSA_KEY_LOADING] = "Processing key data",
    [LOG_MSG_RSA_KEY_SUCCESS] = "Key data processed successfully",
    [LOG_MSG_RSA_ENCRYPT_SUCCESS] = "Data encryption completed",
    [LOG_MSG_RSA_DECRYPT_SUCCESS] = "Data decryption completed",
    [LOG_MSG_RSA_VERIFY_SUCCESS] = "Verification successful",
    [LOG_MSG_RSA_VERIFY_FAILED] = "Verification unsuccessful",
    
    // HTTP相关日志 (200-299)
    [LOG_MSG_HTTP_REQ_START] = "Initiating network operation",
    [LOG_MSG_HTTP_RESP_START] = "Network response started",
    [LOG_MSG_HTTP_RESP_COMPLETE] = "Network operation completed",
    
    // 授权相关日志 (300-399)
    [LOG_MSG_AUTH_START] = "Starting validation process",
    [LOG_MSG_AUTH_SUCCESS] = "Validation process successful",
    [LOG_MSG_AUTH_FAILED] = "Validation process unsuccessful",
    [LOG_MSG_AUTH_STATUS] = "Validation status",
    
    // 错误日志 (900-999)
    [LOG_ERR_MEM_ALLOC] = "Memory allocation issue",
    [LOG_ERR_RSA_KEY_LOAD] = "Key processing issue",
    [LOG_ERR_RSA_ENCRYPT] = "Encryption issue",
    [LOG_ERR_RSA_DECRYPT] = "Decryption issue",
    [LOG_ERR_RSA_VERIFY] = "Verification issue",
    [LOG_ERR_HTTP_REQ] = "Network request issue",
    [LOG_ERR_INVALID_PARAM] = "Invalid parameter issue",
    [LOG_ERR_EMPTY_RESP] = "Empty response issue",
    [LOG_ERR_EMPTY_SEED] = "Token generation issue",
    [LOG_ERR_B64_DECODE] = "Data format issue"
};

// Emscripten日志级别映射
static int em_log_level_map[] = {
    [LOG_LEVEL_DEBUG] = EM_LOG_DEBUG,
    [LOG_LEVEL_INFO] = EM_LOG_CONSOLE,
    [LOG_LEVEL_WARNING] = EM_LOG_WARN,
    [LOG_LEVEL_ERROR] = EM_LOG_ERROR,
    [LOG_LEVEL_CRITICAL] = EM_LOG_ERROR
};

// 记录安全日志 - 基本版本
void security_log(LogLevel level, LogMessageId msg_id, const char* param) {
    // 获取消息
    const char* message = (msg_id < sizeof(message_table)/sizeof(message_table[0])) ? 
                        message_table[msg_id] : "Unknown message";
    
    // 映射日志级别
    int em_level = em_log_level_map[level];
    
    // 更新全局错误信息（仅对错误级别）
    if (level >= LOG_LEVEL_ERROR) {
        g_error_code = msg_id;
        snprintf(g_error_message, sizeof(g_error_message), "%s: %s", 
                 message, param ? param : "");
    }
    
    // 记录日志，仅包含消息ID和消息，不包含具体代码位置信息
    if (param) {
        emscripten_log(em_level, "MSG[%d]: %s - %s", msg_id, message, param);
    } else {
        emscripten_log(em_level, "MSG[%d]: %s", msg_id, message);
    }
}

// 记录安全日志 - 格式化版本
void security_log_fmt(LogLevel level, LogMessageId msg_id, const char* format, ...) {
    char buffer[256];
    va_list args;
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    security_log(level, msg_id, buffer);
} 