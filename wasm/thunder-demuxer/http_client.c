#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <emscripten.h>
#include "http_client.h"
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/buffer.h>
#include <openssl/sha.h>

// 为Emscripten环境添加time_t定义和time函数实现，解决编译错误
#ifdef __EMSCRIPTEN__
#include <stdint.h>
typedef int64_t time_t;

// 为Emscripten环境添加time函数实现
time_t time(time_t* timer) {
    struct timeval tv;
    time_t result;
    
    gettimeofday(&tv, NULL);
    result = (time_t)tv.tv_sec;
    
    if (timer) {
        *timer = result;
    }
    
    return result;
}
#endif

// 定义最大响应数据大小
#define MAX_RESPONSE_SIZE 8192

// 定义随机字符串的长度
#define SEED_LENGTH 16

// 定义请求超时时间（毫秒）
#define REQUEST_TIMEOUT_MS 5000

// base64编码的RSA公钥
#define PUBLIC_KEY_BASE64 "LS10tLS1CRUadJTiBQVUJeMSUMgS0VZLS0tLS0KTUlJQ0lqQU5CZ2txaGtpRzl3MEJBUUVGQUFPQ0FnOEFNSUlDQ2dLQ0FnRUFzdUJDMGE2b1hHeHFOMjAwS3BONgpPWmtBWjRCZFo0MWJCb21XTjJ6VFlKbmlTZXg2MEl2THZuS05ML1VLeEJ5TUZCZTN6S3E1b3cyTFNFbEFIQVcvCitxdDlNdm52OG1EOFV5bUJCeFhpNVNyR2ZnT2Y5aVR4elpTbEMxck5XMlNJZGwyNnU2ZmZ6WUx2dk9SU1I1TnEKT09yMks4VGt3Q0ZoSmFlejZCNldvNmdUWVJLRENSZlFRMFp3VlhVOXNPc3BHbzdnNHp5MzBBc0loSTl6cWo5UAovRHdmSG1obDFPVXRuN3lodDRkR2pCS3QzbE80eEE3Wnk1TURicVFBSmEzVDlmb1RNWVd2QWh1Y3Jab2o4SFkrCjdyWjFGWCtyeDFWZWJxUjc3Q2Q1VFJGbGRTczl0VTdUSmdRMDhSOFF4NHlLdEtnR0ZsNG1qWG1yaVpqdWIwSEMKRkpORElnR1FoQ0FXMHZiQ2FhTHhIQnZKekc5WXR0ZTU2SlBvcG9QRFZlN3BwakpWc0ZHaWNXNm0xcHc0STRZQwpRKzh0bFRmdGtSYkJLR3hQVG5zeGdrWU5OS01rUzRCWmNzS0hoRlUxUDFDdXAwbEdSZkxxcWFyV1dPSDUyQ1lBCm1oV21XT1kvRDNUSnYzL01aQnRMb09wSnFndXJQTXNNYlZDVWhycWVTSHJONDRtendIK21iV0pIUnNIMkhmNXAKZjROQVUrQSs4NlRKblg0YXdpUDJpaE4zc20zRlZyQ0o0V29LaWpQbDBaTXJkTnhvaDdhU0c4RjFmSW1BVTJhcgpUbm82Wk41YXROb3RCVC8yamNkbzlkMmFua0xHM1E4TU9zV2o1Y2VCUzJRN01lSURrUk1Zc1duZExJQ0dSdlRNCkQvY3A5M0RXMXJTL09KaVVwemxidWxFQ0F3RUFBUT09Ci0tLS0tRU5EIFBVQkxJQyBLRVktLS0tLQ=="

// 全局授权状态
int g_auth_status = 0;  // 0-未授权，1-已授权，-1-授权失败

// 临时保存签名验证用的seed
static char g_last_seed[SEED_LENGTH + 1] = {0};

// 获取当前授权状态
int get_auth_status() {
    return g_auth_status;
}

// 回调函数结构体
typedef struct {
    HttpResponseCallback callback;
    HttpResponseCallback user_callback; // 用户提供的回调函数
    void* user_data;
} CallbackContext;

// 解码Base64字符串
static unsigned char* base64_decode(const char* input, size_t* output_length) {
    BIO *b64, *bmem;
    size_t length = strlen(input);
    unsigned char* buffer = (unsigned char*)malloc(length);
    
    if (!buffer) return NULL;
    
    bmem = BIO_new_mem_buf(input, -1);
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_push(b64, bmem);
    
    *output_length = BIO_read(bmem, buffer, length);
    BIO_free_all(bmem);
    
    return buffer;
}

// 反混淆函数：移除混淆后字符串的第 3(索引2), 11(索引11), 20(索引21) 位的字符
int deobfuscate_string(const char *obfuscated, char *original_buffer, size_t buffer_size) {
    size_t obfuscated_len = strlen(obfuscated);
    // Expected original length: obfuscated length - 3 insertions
    size_t original_len_needed = obfuscated_len > 3 ? (obfuscated_len - 3 + 1) : 1; // +1 for null terminator

    if (buffer_size < original_len_needed) {
         fprintf(stderr, "Error: Deobfuscation buffer too small. Needed %zu, got %zu\n", original_len_needed, buffer_size);
        return -1; // Indicate error (buffer too small)
    }

    size_t i = 0; // index for obfuscated string
    size_t j = 0; // index for original_buffer

    while (obfuscated[i] != '\0') {
        // Skip the characters inserted at indices 2, 11, 21 in the obfuscated string
        if (i == 2 || i == 11 || i == 21) {
            i++; // Skip this character
            continue;
        }
        original_buffer[j++] = obfuscated[i++];
    }
    original_buffer[j] = '\0'; // Null terminate the deobfuscated string

    return 0; // Indicate success
}

// 获取解码后的PEM格式公钥
static const char* get_public_key_pem() {
    static char* pem_key = NULL;
    
    // 只需要解析一次
    if (pem_key) {
        return pem_key;
    }
    
    // 解码 Base64
    size_t decoded_len = 0;
    // 先对混淆的公钥进行反混淆处理
    char deobfuscated_key[2048] = {0}; // 足够大的缓冲区
    if (deobfuscate_string(PUBLIC_KEY_BASE64, deobfuscated_key, sizeof(deobfuscated_key)) != 0) {
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_KEY_LOAD, "Failed to load public key");
        return NULL;
    }
    
    unsigned char* decoded = base64_decode(deobfuscated_key, &decoded_len);
    
    if (!decoded) {
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_KEY_LOAD, "Failed to load dpublic key");
        return NULL;
    }
    
    // 确保字符串以NULL结尾
    pem_key = malloc(decoded_len + 1);
    if (!pem_key) {
        free(decoded);
        security_log(LOG_LEVEL_ERROR, LOG_ERR_MEM_ALLOC, "Public key buffer");
        return NULL;
    }
    
    memcpy(pem_key, decoded, decoded_len);
    pem_key[decoded_len] = '\0';
    
    free(decoded);
    return pem_key;
}

// 使用RSA公钥加密数据
static unsigned char* rsa_encrypt(const unsigned char* data, size_t data_len, 
                                size_t* encrypted_len) {
    // 初始化 OpenSSL 错误处理
    ERR_load_crypto_strings();
    
    // 获取PEM格式的公钥
    const char* pem_key = get_public_key_pem();
    if (!pem_key) {
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_KEY_LOAD, "Failed to get PEM key");
        return NULL;
    }
    
    // 创建新的 BIO 对象，直接使用PEM格式的公钥
    BIO* keybio = BIO_new_mem_buf(pem_key, -1);
    if (!keybio) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_KEY_LOAD, err_buf);
        return NULL;
    }
    
    security_log(LOG_LEVEL_INFO, LOG_MSG_RSA_KEY_LOADING, NULL);
    
    // 尝试从 PEM 格式读取 RSA 公钥
    RSA* rsa = NULL;
    rsa = PEM_read_bio_RSA_PUBKEY(keybio, NULL, NULL, NULL);
    
    if (!rsa) {
        // 打印错误信息
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_KEY_LOAD, err_buf);
        
        BIO_free(keybio);
        return NULL;
    }
    
    BIO_free(keybio);
    
    security_log(LOG_LEVEL_INFO, LOG_MSG_RSA_KEY_SUCCESS, NULL);
    
    // 获取RSA密钥的大小并分配足够的空间
    int rsa_size = RSA_size(rsa);
    unsigned char* encrypted = (unsigned char*)malloc(rsa_size);
    if (!encrypted) {
        RSA_free(rsa);
        security_log(LOG_LEVEL_ERROR, LOG_ERR_MEM_ALLOC, "RSA encryption buffer");
        return NULL;
    }
    
    // 使用公钥加密数据
    int result = RSA_public_encrypt(data_len, data, encrypted, rsa, RSA_PKCS1_PADDING);
    if (result == -1) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_ENCRYPT, err_buf);
        
        free(encrypted);
        RSA_free(rsa);
        return NULL;
    }
    
    *encrypted_len = result;
    RSA_free(rsa);
    
    security_log_fmt(LOG_LEVEL_INFO, LOG_MSG_RSA_ENCRYPT_SUCCESS, "%d bytes", result);
    return encrypted;
}

// 使用RSA公钥验证签名 - 使用标准PKCS1v15签名验证
int verify_signature(const unsigned char* signature, size_t signature_len, 
                    const unsigned char* data, size_t data_len) {
    // 初始化 OpenSSL 错误处理
    ERR_load_crypto_strings();
    
    // 获取PEM格式的公钥
    const char* pem_key = get_public_key_pem();
    if (!pem_key) {
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_KEY_LOAD, "Failed to get PEM key");
        return 0;
    }
    
    // 创建新的 BIO 对象，使用PEM格式的公钥
    BIO* keybio = BIO_new_mem_buf(pem_key, -1);
    if (!keybio) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_KEY_LOAD, err_buf);
        return 0;
    }
    
    // 从PEM格式读取RSA公钥
    RSA* rsa = PEM_read_bio_RSA_PUBKEY(keybio, NULL, NULL, NULL);
    BIO_free(keybio);
    
    if (!rsa) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_KEY_LOAD, err_buf);
        return 0;
    }
    
    // 计算数据的SHA256哈希
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, data_len);
    SHA256_Final(hash, &sha256);
    
    security_log_fmt(LOG_LEVEL_DEBUG, LOG_MSG_RSA_VERIFY_SUCCESS, "Hash calculated: %d bytes", SHA256_DIGEST_LENGTH);
    
    // 使用RSA_verify_PKCS1_PSS验证签名
    int result = RSA_verify(NID_sha256, hash, SHA256_DIGEST_LENGTH, 
                            (unsigned char*)signature, signature_len, rsa);
    
    if (result == 1) {
        security_log(LOG_LEVEL_INFO, LOG_MSG_RSA_VERIFY_SUCCESS, NULL);
        RSA_free(rsa);
        return 1;
    } else {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_VERIFY, err_buf);
        RSA_free(rsa);
        return 0;
    }
}

// 生成随机字符串作为seed
static void generate_random_seed(char* buffer, size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t charset_size = sizeof(charset) - 1;
    
    // 初始化随机数生成器
    srand((unsigned int)time(NULL));
    
    for (size_t i = 0; i < length; i++) {
        buffer[i] = charset[rand() % charset_size];
    }
    buffer[length] = '\0';
}

// 获取当前时间戳（秒）
static long get_current_timestamp() {
    return (long)time(NULL);
}

// JavaScript 回调函数 - 用于二进制数据传输
EM_JS(void, js_http_post_binary, (const char* url, const unsigned char* data, size_t data_len, int callback_id), {
    // 创建Uint8Array来存储二进制数据
    const binaryData = new Uint8Array(data_len);
    for (let i = 0; i < data_len; i++) {
        binaryData[i] = HEAPU8[data + i];
    }
    
    // 调用JavaScript桥接函数并等待响应
    ThunderPlayerBridge.httpPostBinary(url, binaryData, callback_id);
});

// 原始JavaScript回调函数，保留用于非加密请求
EM_JS(void, js_http_post, (const char* url, const char* data, int callback_id), {
    // 调用JavaScript桥接函数
    ThunderPlayerBridge.httpPost(url, data, callback_id);
});

// JavaScript 回调处理函数
void EMSCRIPTEN_KEEPALIVE http_response_handler(const char* response_data, int status, int callback_id) {
    // 获取回调上下文
    CallbackContext* ctx = (CallbackContext*)callback_id;
    
    if (ctx && ctx->callback) {
        // 调用用户提供的回调函数
        ctx->callback(response_data, status, ctx->user_data);
        // 释放回调上下文
        free(ctx);
    }
}


// 授权回调处理函数
static void auth_callback(const char* response_data, int status, void* context) {
    security_log_fmt(LOG_LEVEL_INFO, LOG_MSG_HTTP_RESP_COMPLETE, "Status: %d", status);
    
    // 调用用户提供的回调（如果有）
    CallbackContext* ctx = (CallbackContext*)context;
    ModuleResponseContext* module_ctx = NULL;
    if (ctx && ctx->user_data) {
        module_ctx = (ModuleResponseContext*)ctx->user_data;
        if (module_ctx) {
            module_ctx->status = status;
        }
    }

    // 首先根据HTTP状态码判断授权状态
    if (status != 200) {
        security_log_fmt(LOG_LEVEL_ERROR, LOG_MSG_AUTH_FAILED, "HTTP %d", status);
        g_auth_status = -1;  // 授权失败
        return;
    }
    
    // 解析响应数据
    if (!response_data) {
        security_log(LOG_LEVEL_ERROR, LOG_ERR_EMPTY_RESP, NULL);
        g_auth_status = -1;  // 授权失败
        return;
    }
    
    int seed_len = strlen(g_last_seed);
    // 验证seed是否有效
    if (seed_len == 0) {
        security_log(LOG_LEVEL_ERROR, LOG_ERR_EMPTY_SEED, NULL);
        g_auth_status = -1;  // 授权失败
        return;
    }
    
    security_log_fmt(LOG_LEVEL_DEBUG, LOG_MSG_AUTH_STATUS, "Seed length: %d", seed_len);
    
    // 检查响应数据是否是BASE64编码
    const char* base64_prefix = "BASE64:";
    size_t prefix_len = strlen(base64_prefix);
    
    if (strncmp(response_data, base64_prefix, prefix_len) == 0) {
        // 是BASE64编码的数据
        const char* base64_data = response_data + prefix_len;
        security_log_fmt(LOG_LEVEL_DEBUG, LOG_MSG_RESP_RECV, "BASE64 data length: %zu", strlen(base64_data));
        
        // 解码BASE64数据
        size_t decoded_len = 0;
        unsigned char* decoded_signature = base64_decode(base64_data, &decoded_len);
        
        if (!decoded_signature) {
            security_log(LOG_LEVEL_ERROR, LOG_ERR_B64_DECODE, NULL);
            g_auth_status = -1;  // 授权失败
            return;
        }
        
        security_log_fmt(LOG_LEVEL_DEBUG, LOG_MSG_RESP_RECV, "Decoded length: %zu bytes", decoded_len);
        
        // 使用解码后的签名进行验证
        if (verify_signature(decoded_signature, decoded_len, (const unsigned char*)g_last_seed, seed_len)) {
            security_log(LOG_LEVEL_INFO, LOG_MSG_AUTH_SUCCESS, NULL);
            g_auth_status = 1;  // 授权成功
        } else {
            security_log(LOG_LEVEL_ERROR, LOG_MSG_RSA_VERIFY_FAILED, NULL);
            g_auth_status = -1;  // 授权失败
        }
        
        // 释放解码后的数据
        free(decoded_signature);
    } else {
        // 原始二进制数据处理方式
        int response_len = strlen(response_data);
        security_log_fmt(LOG_LEVEL_DEBUG, LOG_MSG_RESP_RECV, "Binary data length: %d", response_len);
        
        // 从JavaScript收到的数据是原始二进制数据，转为字符串后，每个字节成为一个字符
        const unsigned char* binary_signature = (const unsigned char*)response_data;
        size_t binary_len = response_len;
        
        // 使用之前保存的seed验证签名
        if (verify_signature(binary_signature, binary_len, (const unsigned char*)g_last_seed, seed_len)) {
            security_log(LOG_LEVEL_INFO, LOG_MSG_AUTH_SUCCESS, NULL);
            g_auth_status = 1;  // 授权成功
        } else {
            security_log(LOG_LEVEL_ERROR, LOG_MSG_RSA_VERIFY_FAILED, NULL);
            g_auth_status = -1;  // 授权失败
        }
    }
    
    if (ctx && ctx->user_callback) {
        ctx->user_callback(response_data, status, ctx->user_data);
    }
}

// 初始化授权 - 使用RSA加密
int init_auth(
    const char* appid,
    const char* uid, 
    const char* sdk_sn,
    HttpResponseCallback callback,
    void* user_data
) {
    if (!appid || !uid || !sdk_sn) {
        security_log(LOG_LEVEL_ERROR, LOG_ERR_INVALID_PARAM, "Missing required parameter");
        return -1;
    }
    
    // 重置授权状态
    g_auth_status = 0;
    
    // 生成随机seed
    char seed[SEED_LENGTH + 1];
    generate_random_seed(seed, SEED_LENGTH);
    
    // 保存seed以供后续验证签名使用
    strncpy(g_last_seed, seed, SEED_LENGTH);
    g_last_seed[SEED_LENGTH] = '\0';
    
    // 获取当前时间戳
    long timestamp = get_current_timestamp();
    
    // 使用相对路径来调用我们的API代理
    char request_url[256] = "/api/wauth/init/v2";
    
    // 构建JSON请求体
    char request_data[1024];
    snprintf(
        request_data, 
        sizeof(request_data),
        "{\"seed\":\"%s\",\"appid\":\"%s\",\"time\":\"%ld\",\"uid\":\"%s\",\"sdk_sn\":\"%s\"}",
        seed, 
        appid,
        timestamp,
        uid,
        sdk_sn
    );
    
    security_log(LOG_LEVEL_DEBUG, LOG_MSG_AUTH_START, "Preparing request data");
    
    // 创建回调上下文
    CallbackContext* ctx = (CallbackContext*)malloc(sizeof(CallbackContext));
    if (!ctx) {
        security_log(LOG_LEVEL_ERROR, LOG_ERR_MEM_ALLOC, "Callback context");
        return -2;
    }
    
    // 设置auth_callback作为主回调，并将用户回调和用户数据保存
    ctx->callback = auth_callback;
    ctx->user_callback = callback;
    ctx->user_data = user_data;
    
    // 使用RSA加密请求数据
    size_t encrypted_len = 0;
    unsigned char* encrypted_data = rsa_encrypt((const unsigned char*)request_data, 
                                              strlen(request_data), 
                                              &encrypted_len);
    
    if (!encrypted_data) {
        free(ctx);
        security_log(LOG_LEVEL_ERROR, LOG_ERR_RSA_ENCRYPT, "Request data encryption failed");
        return -3;
    }
    
    security_log_fmt(LOG_LEVEL_INFO, LOG_MSG_HTTP_REQ_START, "URL: %s, Data size: %zu", request_url, encrypted_len);
    
    // 发送HTTP POST请求 - 使用二进制方式
    js_http_post_binary(request_url, encrypted_data, encrypted_len, (int)ctx);
    
    // 释放加密数据
    free(encrypted_data);
    
    security_log(LOG_LEVEL_INFO, LOG_MSG_REQ_SENT, NULL);
    
    return 0;
} 