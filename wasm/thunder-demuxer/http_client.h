#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 定义最大响应大小
#define MAX_RESPONSE_SIZE 8192

// 定义模块内部的响应上下文结构体
typedef struct {
    char buffer[MAX_RESPONSE_SIZE];
    int status;
    int complete;
} ModuleResponseContext;

/**
 * HTTP 响应回调函数类型
 * @param response_data 响应数据
 * @param status HTTP状态码
 * @param user_data 用户数据指针
 */
typedef void (*HttpResponseCallback)(const char* response_data, int status, void* user_data);

/**
 * 授权状态
 * 0 - 未授权
 * 1 - 授权成功
 * -1 - 授权失败
 */
extern int g_auth_status;

/**
 * 获取当前授权状态
 * @return 当前授权状态，0-未授权，1-已授权，-1-授权失败
 */
int get_auth_status();

// /**
//  * 发送HTTP POST请求
//  * @param url 请求URL
//  * @param data 请求数据
//  * @param callback 响应回调函数
//  * @param user_data 用户数据指针，会在回调中返回
//  * @return 0表示成功，非0表示错误
//  */
// int http_post(const char* url, const char* data, HttpResponseCallback callback, void* user_data);

/**
 * 初始化 URL 授权接口，该接口会构建所需的请求参数并发送到指定接口
 * @param appid 应用ID
 * @param uid 用户ID
 * @param sdk_sn SDK序列号
 * @param callback 响应回调函数
 * @param user_data 用户数据指针
 * @return 0表示成功，非0表示错误
 */
int init_auth(
    const char* appid,
    const char* uid,
    const char* sdk_sn,
    HttpResponseCallback callback,
    void* user_data
);

/**
 * 使用RSA公钥验证签名
 * @param signature 签名数据
 * @param signature_len 签名数据长度
 * @param data 原始数据
 * @param data_len 原始数据长度
 * @return 1表示验证成功，0表示验证失败
 */
int verify_signature(const unsigned char* signature, size_t signature_len, 
                     const unsigned char* data, size_t data_len);

// 安全日志系统 - 日志级别
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_CRITICAL = 4
} LogLevel;

// 安全日志系统 - 日志消息ID
typedef enum {
    // 普通日志消息 (0-99)
    LOG_MSG_INIT = 0,
    LOG_MSG_VERSION = 1,
    LOG_MSG_MODULE_INIT = 2,
    LOG_MSG_REQ_SENT = 3,
    LOG_MSG_RESP_RECV = 4,
    
    // RSA相关日志 (100-199)
    LOG_MSG_RSA_KEY_LOADING = 100,
    LOG_MSG_RSA_KEY_SUCCESS = 101,
    LOG_MSG_RSA_ENCRYPT_SUCCESS = 102,
    LOG_MSG_RSA_DECRYPT_SUCCESS = 103,
    LOG_MSG_RSA_VERIFY_SUCCESS = 104,
    LOG_MSG_RSA_VERIFY_FAILED = 105,
    
    // HTTP相关日志 (200-299)
    LOG_MSG_HTTP_REQ_START = 200,
    LOG_MSG_HTTP_RESP_START = 201,
    LOG_MSG_HTTP_RESP_COMPLETE = 202,
    
    // 授权相关日志 (300-399)
    LOG_MSG_AUTH_START = 300,
    LOG_MSG_AUTH_SUCCESS = 301,
    LOG_MSG_AUTH_FAILED = 302,
    LOG_MSG_AUTH_STATUS = 303,
    
    // 错误日志 (900-999)
    LOG_ERR_MEM_ALLOC = 900,
    LOG_ERR_RSA_KEY_LOAD = 901,
    LOG_ERR_RSA_ENCRYPT = 902,
    LOG_ERR_RSA_DECRYPT = 903,
    LOG_ERR_RSA_VERIFY = 904,
    LOG_ERR_HTTP_REQ = 905,
    LOG_ERR_INVALID_PARAM = 906,
    LOG_ERR_EMPTY_RESP = 907,
    LOG_ERR_EMPTY_SEED = 908,
    LOG_ERR_B64_DECODE = 909
} LogMessageId;

// 全局错误信息
extern int g_error_code;
extern char g_error_message[256];

// 安全日志函数声明
void security_log(LogLevel level, LogMessageId msg_id, const char* param);
void security_log_fmt(LogLevel level, LogMessageId msg_id, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_H */ 