#include <stdio.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include "../http_client.h"

// 为Emscripten环境添加CLOCK_MONOTONIC和clock_gettime的定义
#ifdef __EMSCRIPTEN__
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

int clock_gettime(int clk_id, struct timespec *tp) {
    struct timeval now;
    int ret = gettimeofday(&now, NULL);
    if (ret < 0) {
        return ret;
    }
    tp->tv_sec = now.tv_sec;
    tp->tv_nsec = now.tv_usec * 1000;
    return 0;
}
#endif

// 声明thunder_module.c中定义的js_init_auth函数
extern int js_init_auth(const char* appid, const char* uid, const char* sdk_sn, char* response_data, int response_size);
// 声明thunder_module.c中定义的get_auth_status_wrapper函数
extern int get_auth_status_wrapper(void);

typedef void(*VideoCallback)(unsigned char *buff, int size, int key_frame, double timestamp);
typedef void(*AudioCallback)(unsigned char *buff, int size, double timestamp);
typedef void(*DownloaderCtrlCallback)(int ctrl);
// ✅ 新增：Packet回调类型（用于输出H264/AAC packet给libmedia）
// 注意：pts和dts使用int而非int64_t,因为Emscripten的函数指针不支持int64参数
// 对于大多数视频,PTS/DTS值在int32范围内足够使用
typedef void(*PacketCallback)(int stream_type, unsigned char *data, int size, int pts, int dts, int flags);

#ifdef __cplusplus
extern "C" {
#endif

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/fifo.h"
#include "libavutil/pixdesc.h"
#include "log.h"
//#include "libswscale/swscale.h"

#define MIN(X, Y)  ((X) < (Y) ? (X) : (Y))

const int kCustomIoBufferSize = 64 * 1024;  // Windows优化：增大IO缓冲区
const int kInitialPcmBufferSize = 512 * 1024;  // Windows优化：增大初始PCM缓冲区到512KB
const int kDefaultFifoSize = 20 * 1024 * 1024; // 10MB, 默认缓冲区大小

const int kMinDecoderSize = 512 * 1024; //如果要解码的话，最小需要512KB

const int kMinFifoSize = 10 * 1024 * 1024; // 10MB, 小于 10MB 时开启流控
const int kMaxFifoSize = 18 * 1024 * 1024; // 18MB, 大于 18MB 时停止流控

typedef enum ErrorCode {
    kErrorCode_Success = 0,
    kErrorCode_Invalid_Param = 1,
    kErrorCode_Invalid_State = 2,
    kErrorCode_Invalid_Data = 3,
    kErrorCode_Invalid_Format = 4,
    kErrorCode_NULL_Pointer = 5,
    kErrorCode_Open_File_Error = 6,
    kErrorCode_Eof = 7,
    kErrorCode_FFmpeg_Error = 8,
    kErrorCode_Old_Frame = 9,
    kErrorCode_Fifo_Full = 10
} ErrorCode;

#define THUNDERSTONE_DECRTPT_SUPPORT 1



typedef struct LS_FileInfo {
    char fileName[32];
    int fileLen;
    int fileOffset;
} LS_FileInfo;


typedef struct WebDecoder {
    AVFormatContext *avformatContext;
    AVCodecContext *videoCodecContext;
    AVCodecContext *audioCodecContext;
    AVCodecContext *audioCodecContext2;
    AVFrame *avFrame;
    int videoStreamIdx;
    int audioStreamIdx;
    int audioStreamIdx2;
    VideoCallback videoCallback;
    AudioCallback audioCallback;
    DownloaderCtrlCallback downloaderCtrlCallback;
    PacketCallback packetCallback;  // ✅ 新增：packet回调
    unsigned char *yuvBuffer;
    //unsigned char *rgbBuffer;
    unsigned char *pcmBuffer;
    int currentPcmBufferSize;
    int videoBufferSize;
    int videoSize;
    //struct SwsContext* swsCtx;
    unsigned char *customIoBuffer;
    // FILE *fp;
    // char fileName[64];
    int64_t fileSize;
    int64_t seek_pos;
    int64_t fileReadPos;
    int64_t fileWritePos;
    int64_t lastRequestOffset;
    double beginTimeOffset;
    int accurateSeek;
    AVFifoBuffer *fifo;
    int fifoSize;
    // 新增：用于存储头部数据
    unsigned char *headBuffer;
    int headBufferSize;
    int headOffset;
    // 新增：用于存储尾部数据
    unsigned char *tailBuffer;
    int tailBufferSize;
    int tailOffset;
    int gotStreamInfo;
    int audioSwitch;
    // int wait_seek_done;
#if THUNDERSTONE_DECRTPT_SUPPORT
    // int tsDecryptCheck;
    void *tsDecrypt;
    // 新增：用于8KB对齐处理的缓冲区
    AVFifoBuffer *alignFifo;
    unsigned char alignBuffer[10 * 1024 * 1024];
    int enableDecryption; // 是否启用解密（用于调试IO通路）
#endif
    int dataOffset;
    int readEof;
    int auth_status; // 鉴权状态：0-未鉴权，1-鉴权成功，-1-鉴权失败

    int mediaType;
    LS_FileInfo ls_files[3];
    char* lsLyricsBuf;
    int lsStartOffset;
    int seeking;
    double seekTimestamp;
} WebDecoder;


#define MEDIA_TYPE_TS 0
#define MEDIA_TYPE_LS 1
#define MEDIA_TYPE_ULS 2
#define MEDIA_TYPE_MLS 3

#if THUNDERSTONE_DECRTPT_SUPPORT
#include "tsDecrypt.h"
#define ENCRYPT_HEAD_SIZE   (512)
#define ENCRYPT_CHUNK_SIZE  (1024 * 8)
#endif


WebDecoder *decoder = NULL;

unsigned long getTickCount() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * (unsigned long)1000 + ts.tv_nsec / 1000000;
}

void printFifoRate() {
    // return;
    if (decoder == NULL || decoder->fifo == NULL || decoder->fifoSize <= 0) {
        LOG_WARN("FIFO未初始化,无法打印占用率");
        return;
    }
    
    int usedSize = av_fifo_size(decoder->fifo);
    float usageRate = ((float)usedSize / decoder->fifoSize) * 100.0f;
    
    char progressBar[52] = {0}; // 50个字符用于进度条，加上'[]'和结束符
    progressBar[0] = '[';
    
    int barLength = 50;
    int filledLength = (int)(usageRate * barLength / 100.0f);
    
    for (int i = 0; i < barLength; i++) {
        if (i < filledLength) {
            progressBar[i + 1] = '=';
        } else {
            progressBar[i + 1] = ' ';
        }
    }
    
    progressBar[barLength + 1] = ']';
    progressBar[barLength + 2] = '\0';
    
    // LOG_DEBUG("FIFO占用率: %s %.2f%% (%d/%d字节)", 
    //           progressBar, usageRate, usedSize, decoder->fifoSize);
}

int getFifoUsedSize() {
    if (decoder == NULL || decoder->fifo == NULL) {
        return 0;
    }
    return av_fifo_size(decoder->fifo);
}

int getFifoTotalSize() {
    if (decoder == NULL) {
        return 0;
    }
    return decoder->fifoSize;
}

int openCodecContext(AVFormatContext *fmtCtx, enum AVMediaType type,int index, int *streamIdx, AVCodecContext **decCtx) {
    int ret = 0;
    do {
        int streamIndex		= -1;
        AVStream *st		= NULL;
        AVCodec *dec		= NULL;
        AVDictionary *opts	= NULL;

        ret = av_find_best_stream(fmtCtx, type, index, -1, NULL, 0);
        if (ret < 0) {
            LOG_ERROR("Could not find %s stream.", av_get_media_type_string(type));
            break;
        }

        streamIndex = ret;
        st = fmtCtx->streams[streamIndex];

        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            LOG_ERROR("Failed to find %s codec %d.", av_get_media_type_string(type), st->codecpar->codec_id);
            ret = AVERROR(EINVAL);
            break;
        }

        *decCtx = avcodec_alloc_context3(dec);
        if (!*decCtx) {
            LOG_ERROR("Failed to allocate the %s codec context.", av_get_media_type_string(type));
            ret = AVERROR(ENOMEM);
            break;
        }

        if ((ret = avcodec_parameters_to_context(*decCtx, st->codecpar)) != 0) {
            LOG_ERROR("Failed to copy %s codec parameters to decoder context.", av_get_media_type_string(type));
            break;
        }

        av_dict_set(&opts, "refcounted_frames", "0", 0);
        // av_dict_set(&opts, "err_recognize", "0", 0);  // 降低错误检测严格性
        // av_dict_set(&opts, "flags", "output_corrupt", 0);  // 允许输出部分损坏帧

        if ((ret = avcodec_open2(*decCtx, dec, NULL)) != 0) {
            LOG_ERROR("Failed to open %s codec.", av_get_media_type_string(type));
            break;
        }

        *streamIdx = streamIndex;
        avcodec_flush_buffers(*decCtx);
    } while (0);

    return ret;
}

void closeCodecContext(AVFormatContext *fmtCtx, AVCodecContext *decCtx, int streamIdx) {
    do {
        if (fmtCtx == NULL || decCtx == NULL) {
            break;
        }

        if (streamIdx < 0 || streamIdx >= fmtCtx->nb_streams) {
            break;
        }

        fmtCtx->streams[streamIdx]->discard = AVDISCARD_ALL;
        avcodec_close(decCtx);
    } while (0);
}

ErrorCode copyYuvData(AVFrame *frame, unsigned char *buffer, int width, int height) {
    ErrorCode ret		= kErrorCode_Success;
    unsigned char *src	= NULL;
    unsigned char *dst	= buffer;
    int i = 0;
    do {
        if (frame == NULL || buffer == NULL) {
            ret = kErrorCode_Invalid_Param;
            break;
        }

        if (!frame->data[0] || !frame->data[1] || !frame->data[2]) {
            ret = kErrorCode_Invalid_Param;
            break;
        }

        for (i = 0; i < height; i++) {
            src = frame->data[0] + i * frame->linesize[0];
            memcpy(dst, src, width);
            dst += width;
        }

        for (i = 0; i < height / 2; i++) {
            src = frame->data[1] + i * frame->linesize[1];
            memcpy(dst, src, width / 2);
            dst += width / 2;
        }

        for (i = 0; i < height / 2; i++) {
            src = frame->data[2] + i * frame->linesize[2];
            memcpy(dst, src, width / 2);
            dst += width / 2;
        }
    } while (0);
    return ret;	
}

/*
ErrorCode yuv420pToRgb32(unsigned char *yuvBuff, unsigned char *rgbBuff, int width, int height) {
    ErrorCode ret = kErrorCode_Success;
    AVPicture yuvPicture, rgbPicture;
    uint8_t *ptmp = NULL;
    do {
        if (yuvBuff == NULL || rgbBuff == NULL) {
            ret = kErrorCode_Invalid_Param
            break;
        }

        if (decoder == NULL || decoder->swsCtx == NULL) {
            ret = kErrorCode_Invalid_Param
            break;
        }

        
        avpicture_fill(&yuvPicture, yuvBuff, AV_PIX_FMT_YUV420P, width, height);
        avpicture_fill(&rgbPicture, rgbBuff, AV_PIX_FMT_RGB32, width, height);

        ptmp = yuvPicture.data[1];
        yuvPicture.data[1] = yuvPicture.data[2];
        yuvPicture.data[2] = ptmp;

        sws_scale(decoder->swsCtx, yuvPicture.data, yuvPicture.linesize, 0, height, rgbPicture.data, rgbPicture.linesize);
    } while (0);
    return ret;
}
*/

int roundUp(int numToRound, int multiple) {
    return (numToRound + multiple - 1) & -multiple;
}

// 在 YUV420P 格式上绘制文本水印
void drawTextWatermark(unsigned char *yuvBuffer, int width, int height, const char *text, double timestamp) {
    // 简单字体定义 - 每个字符是 5x7 点阵
    // 定义 "Only ThunderStone Test" 的每个字母的点阵
    // O
    const unsigned char O[7][5] = {
        {0, 1, 1, 1, 0},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {0, 1, 1, 1, 0}
    };
    // n
    const unsigned char n[7][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {1, 1, 1, 1, 0},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1}
    };
    // l
    const unsigned char l[7][5] = {
        {1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0},
        {0, 1, 1, 1, 0}
    };
    // y
    const unsigned char y[7][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {0, 1, 1, 1, 1},
        {0, 0, 0, 0, 1},
        {1, 1, 1, 1, 0}
    };
    // T
    const unsigned char T[7][5] = {
        {1, 1, 1, 1, 1},
        {0, 0, 1, 0, 0},
        {0, 0, 1, 0, 0},
        {0, 0, 1, 0, 0},
        {0, 0, 1, 0, 0},
        {0, 0, 1, 0, 0},
        {0, 0, 1, 0, 0}
    };
    // h
    const unsigned char h[7][5] = {
        {1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0},
        {1, 1, 1, 1, 0},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1}
    };
    // u
    const unsigned char u[7][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {0, 1, 1, 1, 1}
    };
    // d
    const unsigned char d[7][5] = {
        {0, 0, 0, 0, 1},
        {0, 0, 0, 0, 1},
        {0, 1, 1, 1, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {0, 1, 1, 1, 1}
    };
    // e
    const unsigned char e[7][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 1, 1, 1, 0},
        {1, 0, 0, 0, 1},
        {1, 1, 1, 1, 1},
        {1, 0, 0, 0, 0},
        {0, 1, 1, 1, 0}
    };
    // r
    const unsigned char r[7][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {1, 0, 1, 1, 0},
        {1, 1, 0, 0, 1},
        {1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0}
    };
    // S
    const unsigned char S[7][5] = {
        {0, 1, 1, 1, 0},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 0},
        {0, 1, 1, 1, 0},
        {0, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {0, 1, 1, 1, 0}
    };
    // t
    const unsigned char t[7][5] = {
        {0, 1, 0, 0, 0},
        {0, 1, 0, 0, 0},
        {1, 1, 1, 1, 0},
        {0, 1, 0, 0, 0},
        {0, 1, 0, 0, 0},
        {0, 1, 0, 0, 1},
        {0, 0, 1, 1, 0}
    };
    // s
    const unsigned char s[7][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 1, 1, 1, 0},
        {1, 0, 0, 0, 0},
        {0, 1, 1, 1, 0},
        {0, 0, 0, 0, 1},
        {1, 1, 1, 1, 0}
    };
    // 定义小写o字符的点阵
    const unsigned char o[7][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 1, 1, 1, 0},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1},
        {0, 1, 1, 1, 0}
    };
    
    // 空格
    const unsigned char space[7][5] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0}
    };
    
    // 放大倍数，要求原来的3倍大
    int scale = 9; // 之前是3，现在变为9倍
    
    // 固定水印文本 "Only ThunderStone Test"
    const char watermarkText[] = "Only ThunderStone Test";
    int textLen = strlen(watermarkText);
    
    // 计算水印位置 - 在画面中心并向上移动
    int charWidth = 5 * scale;
    int charHeight = 7 * scale;
    int charSpacing = 1 * scale;
    
    int watermarkWidth = textLen * charWidth + (textLen - 1) * charSpacing; // 每个字符之间有1个像素间距
    int watermarkHeight = charHeight;
    
    // 计算水平循环移动效果
    // 循环周期增加到20秒，使移动变慢
    double movePeriod = 20.0;
    double normalizedTime = fmod(timestamp, movePeriod) / movePeriod; // 0.0-1.0范围
    
    // 计算水平偏移量，使水印整体从左到右移动一次
    int maxOffset = width + watermarkWidth; // 总移动距离是屏幕宽度+水印宽度
    int horizontalOffset = (int)(normalizedTime * maxOffset) - watermarkWidth;
    
    // 设置水平起始位置
    int startX = horizontalOffset;
    
    // 随机化水印高度位置 - 使用时间戳作为随机种子
    // 确保水印位置在画面的1/10到7/10高度范围内
    int minHeight = height / 10;          // 画面1/10高度
    int maxHeight = (height * 7) / 10;    // 画面7/10高度
    int heightRange = maxHeight - minHeight;
    
    // 使用时间戳计算伪随机高度
    // 每10秒变换一次位置，避免频繁跳动
    double heightSeed = floor(timestamp / 10.0);
    int randomOffset = (int)(fmod(heightSeed * 7919, heightRange)); // 使用质数7919增加随机性
    int startY = minHeight + randomOffset;
    
    // Y平面索引计算
    int yPlaneSize = width * height;
    // U平面和V平面的起始位置
    unsigned char *uPlane = yuvBuffer + yPlaneSize;
    unsigned char *vPlane = uPlane + (yPlaneSize / 4);
    
    // 设置水印颜色 - 灰色半透明
    unsigned char yColor = 180;  // 灰色的亮度值，稍亮一些
    unsigned char uColor = 128;  // 中性U值
    unsigned char vColor = 128;  // 中性V值
    
    // 定义半透明度，0表示完全透明，255表示完全不透明
    int alpha = 170; // 增加透明度，让字体更明显
    
    // 在Y平面上直接绘制文本，放大显示，不再绘制背景和边框
    for (int charIdx = 0; charIdx < textLen; charIdx++) {
        char currentChar = watermarkText[charIdx];
        const unsigned char (*charData)[5] = NULL;
        
        // 选择当前字符的点阵
        switch (currentChar) {
            case 'O': charData = O; break;
            case 'n': charData = n; break;
            case 'l': charData = l; break;
            case 'y': charData = y; break;
            case 'T': charData = T; break;
            case 'h': charData = h; break;
            case 'u': charData = u; break;
            case 'd': charData = d; break;
            case 'e': charData = e; break;
            case 'r': charData = r; break;
            case 'S': charData = S; break;
            case 't': charData = t; break;
            case 's': charData = s; break;
            case 'o': charData = o; break;  // 添加小写o的映射
            case ' ': charData = space; break;
            default: continue; // 跳过不支持的字符
        }
        
        // 绘制字符(放大后)
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 5; x++) {
                if (charData[y][x]) {
                    // 放大每个点
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int pixelX = startX + charIdx * (charWidth + charSpacing) + x * scale + sx;
                            int pixelY = startY + y * scale + sy;
                            
                            // 确保像素在合法范围内，支持水平循环效果
                            // 不要在屏幕右侧映射到左侧，只允许从左侧进入
                            // 修复：移除对负坐标的特殊处理，防止右侧出现半截水印
                            // if (pixelX < 0) pixelX += width;
                            
                            // 仅绘制在屏幕内的像素
                            if (pixelX >= 0 && pixelX < width && pixelY >= 0 && pixelY < height) {
                                // 读取原始像素的Y值
                                unsigned char originalY = yuvBuffer[pixelY * width + pixelX];
                                
                                // 根据透明度混合Y值
                                unsigned char newY = (originalY * (255 - alpha) + yColor * alpha) / 255;
                                
                                // 修改Y平面的值
                                yuvBuffer[pixelY * width + pixelX] = newY;
                                
                                // 处理UV平面 - 每个2x2的Y块对应一个U和V值
                                if (pixelX % 2 == 0 && pixelY % 2 == 0) {
                                    int uvIndex = (pixelY / 2) * (width / 2) + (pixelX / 2);
                                    
                                    // 读取原始UV值
                                    unsigned char originalU = uPlane[uvIndex];
                                    unsigned char originalV = vPlane[uvIndex];
                                    
                                    // 根据透明度混合UV值
                                    uPlane[uvIndex] = (originalU * (255 - alpha) + uColor * alpha) / 255;
                                    vPlane[uvIndex] = (originalV * (255 - alpha) + vColor * alpha) / 255;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // LOG_DEBUG("移动的半透明水印已添加到YUV帧，文本: %s, 水平位置: %d, 垂直位置: %d", watermarkText, horizontalOffset, startY);
}

ErrorCode processDecodedVideoFrame(AVFrame *frame) {
    ErrorCode ret = kErrorCode_Success;
    double timestamp = 0.0f;
    do {
        if (frame == NULL ||
            decoder->videoCallback == NULL ||
            decoder->yuvBuffer == NULL ||
            decoder->videoBufferSize <= 0) {
            ret = kErrorCode_Invalid_Param;
            break;
        }

        if (decoder->videoCodecContext->pix_fmt != AV_PIX_FMT_YUV420P) {
            LOG_ERROR("Not YUV420P, but unsupported format %d.", decoder->videoCodecContext->pix_fmt);
            ret = kErrorCode_Invalid_Format;
            break;
        }

        ret = copyYuvData(frame, decoder->yuvBuffer, decoder->videoCodecContext->width, decoder->videoCodecContext->height);
        if (ret != kErrorCode_Success) {
            break;
        }

        timestamp = (double)frame->pts * av_q2d(decoder->avformatContext->streams[decoder->videoStreamIdx]->time_base);

        // // 添加水印，传递时间戳以实现动态效果
        // if(decoder->tsDecrypt != NULL) {
        //     drawTextWatermark(decoder->yuvBuffer, decoder->videoCodecContext->width, decoder->videoCodecContext->height, "Only ThunderStone Test", timestamp);
        // }

        /*
        ret = yuv420pToRgb32(decoder->yuvBuffer, decoder->rgbBuffer, decoder->videoCodecContext->width, decoder->videoCodecContext->height);
        if (ret != kErrorCode_Success) {
            break;
        }
        */

        if (decoder->accurateSeek && timestamp < decoder->beginTimeOffset) {
            //LOG_DEBUG("video timestamp %lf < %lf", timestamp, decoder->beginTimeOffset);
            ret = kErrorCode_Old_Frame;
            break;
        }
        decoder->videoCallback(decoder->yuvBuffer, decoder->videoSize, frame->key_frame, timestamp);
    } while (0);
    return ret;
}

ErrorCode processDecodedAudioFrame(AVFrame *frame,int audioIndex) {
    ErrorCode ret       = kErrorCode_Success;
    int sampleSize      = 0;
    int audioDataSize   = 0;
    int targetSize      = 0;
    int offset          = 0;
    int i               = 0;
    int ch              = 0;
    double timestamp    = 0.0f;
    AVCodecContext *audioCodecContext;
    int audioStreamIdx = 0;
    do {
        if (frame == NULL) {
            ret = kErrorCode_Invalid_Param;
            break;
        }
        if(audioIndex == 1){
            audioCodecContext = decoder->audioCodecContext;
            audioStreamIdx = decoder->audioStreamIdx;
        }else{
            audioCodecContext = decoder->audioCodecContext2;
            audioStreamIdx = decoder->audioStreamIdx2;
        }

        sampleSize = av_get_bytes_per_sample(audioCodecContext->sample_fmt);
        if (sampleSize < 0) {
            LOG_ERROR("Failed to calculate data size.");
            ret = kErrorCode_Invalid_Data;
            break;
        }

        if (decoder->pcmBuffer == NULL) {
            decoder->pcmBuffer = (unsigned char*)av_mallocz(kInitialPcmBufferSize);
            decoder->currentPcmBufferSize = kInitialPcmBufferSize;
            LOG_DEBUG("Initial PCM buffer size %d.", decoder->currentPcmBufferSize);
        }

        audioDataSize = frame->nb_samples * audioCodecContext->channels * sampleSize;
        if (decoder->currentPcmBufferSize < audioDataSize) {
            targetSize = roundUp(audioDataSize, 4);
            LOG_DEBUG("Current PCM buffer size %d not sufficient for data size %d, round up to target %d.",
                decoder->currentPcmBufferSize,
                audioDataSize,
                targetSize);
            decoder->currentPcmBufferSize = targetSize;
            av_free(decoder->pcmBuffer);
            decoder->pcmBuffer = (unsigned char*)av_mallocz(decoder->currentPcmBufferSize);
        }

        for (i = 0; i < frame->nb_samples; i++) {
            for (ch = 0; ch < audioCodecContext->channels; ch++) {
                memcpy(decoder->pcmBuffer + offset, frame->data[ch] + sampleSize * i, sampleSize);
                offset += sampleSize;
            }
        }

        timestamp = (double)frame->pts * av_q2d(decoder->avformatContext->streams[audioStreamIdx]->time_base);

        if (decoder->accurateSeek && timestamp < decoder->beginTimeOffset) {
            //LOG_DEBUG("audio timestamp %lf < %lf", timestamp, decoder->beginTimeOffset);
            ret = kErrorCode_Old_Frame;
            break;
        }

        if(decoder->audioCodecContext2 == NULL){
            //单音轨视频
            short* shortPcm = (short*)(decoder->pcmBuffer);
            if(decoder->audioSwitch == 1){
                for (size_t i = 0; i < audioDataSize/2; i+=2){
                    shortPcm[i+1] = shortPcm[i];
                }
            }else{
                for (size_t i = 0; i < audioDataSize/2; i+=2){
                    shortPcm[i] = shortPcm[i+1];
                }
            }
            LOG_DEBUG("🎵 单音轨音频回调: 数据大小=%d, 时间戳=%lf", audioDataSize, timestamp);
            decoder->audioCallback(decoder->pcmBuffer, audioDataSize, timestamp);
        }else{
            //双音轨视频
            // 🎯 修复音频切换逻辑：audioSwitch是音轨编号(1或2)，需要映射到正确的流索引
            int shouldPlayAudio = 0;
            if (decoder->audioSwitch == 1 && audioIndex == 1) {
                // 选择第一音轨，当前处理的是第一音轨
                shouldPlayAudio = 1;
            } else if (decoder->audioSwitch == 2 && audioIndex == 2) {
                // 选择第二音轨，当前处理的是第二音轨
                shouldPlayAudio = 1;
            }
            
            // 双音轨处理逻辑 - 简化日志
            if (shouldPlayAudio && decoder->audioCallback != NULL) {
                // 只在需要时记录关键信息，避免日志泛滥
                static int audioCallbackCount = 0;
                audioCallbackCount++;
                if (audioCallbackCount % 200 == 0) {
                    LOG_DEBUG("🎵 音频解码正常: 第%d次回调, 当前音轨=%d", audioCallbackCount, audioIndex);
                }
                decoder->audioCallback(decoder->pcmBuffer, audioDataSize, timestamp);
            }
            // 移除双音轨跳过的无意义日志，这是正常行为
        }
    } while (0);
    return ret;
}

ErrorCode decodePacket(AVPacket *pkt, int *decodedLen) {
    int ret = 0;
    int isVideo = 0;
    int audioIndex = 0;
    AVCodecContext *codecContext = NULL;

    if (pkt == NULL || decodedLen == NULL) {
        LOG_ERROR("decodePacket invalid param.");
        return kErrorCode_Invalid_Param;
    }

    *decodedLen = 0;

    if (pkt->stream_index == decoder->videoStreamIdx) {
        codecContext = decoder->videoCodecContext;
        isVideo = 1;
    } else if (pkt->stream_index == decoder->audioStreamIdx) {
        codecContext = decoder->audioCodecContext;
        isVideo = 0;
        audioIndex = 1;
    } else if (pkt->stream_index == decoder->audioStreamIdx2) {
        codecContext = decoder->audioCodecContext2;
        isVideo = 0;
        audioIndex = 2;
    } else {
        return kErrorCode_Invalid_Data;
    }

    ret = avcodec_send_packet(codecContext, pkt);
    if (ret < 0) {
        char error_buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error_buffer, AV_ERROR_MAX_STRING_SIZE);
        LOG_ERROR("Error sending a packet for decoding: %s (错误码: %d, 0x%x)", 
                  error_buffer, ret, (unsigned int)ret);
        return kErrorCode_FFmpeg_Error;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(codecContext, decoder->avFrame);
        if (ret == AVERROR(EAGAIN)) {
            return kErrorCode_Success;
        } else if (ret == AVERROR_EOF) {
            return kErrorCode_Eof;
        } else if (ret < 0) {
            LOG_ERROR("Error during decoding %d.", ret);
            return kErrorCode_FFmpeg_Error;
        } else {
            int r = isVideo ? processDecodedVideoFrame(decoder->avFrame) : processDecodedAudioFrame(decoder->avFrame,audioIndex);
            if (r == kErrorCode_Old_Frame) {
                return r;
            }
        }
    }

    *decodedLen = pkt->size;
    return kErrorCode_Success;
}
#define kDownloaderCtrl_Pause 1
#define kDownloaderCtrl_Resume 0

void downloaderCtrl(int ctrl){
    if(decoder == NULL) {
        LOG_ERROR("downloaderCtrl decoder NULL.");
        return;
    }
    if(decoder->downloaderCtrlCallback == NULL) {
        LOG_ERROR("downloaderCtrlCallback NULL.");
        return;
    }
    decoder->downloaderCtrlCallback(ctrl);
}
int readFormBuffer(unsigned char *data, int len) {
    int ret = 0;
    do {
        if (decoder == NULL) {
            break;
        }
        if(data == NULL || len <= 0) {
            break;
        }
        LOG_DEBUG("readCallback: gotStreamInfo=%d, 从buffer中读取数据 readPos=%lld, len=%d", decoder->gotStreamInfo, decoder->fileReadPos, len);

        // 处理seek逻辑，确定当前的读取位置
        int64_t readPos = decoder->fileReadPos;
        if (decoder->seek_pos >= 0) {
            readPos = decoder->seek_pos;
            LOG_DEBUG("readCallback: seek_pos=%lld, 设置当前读取位置", readPos);
        }

        // 检查请求的数据是否位于headBuffer中
        if (decoder->headBuffer != NULL && decoder->headBufferSize > 0 && 
            readPos >= decoder->headOffset && 
            readPos < decoder->headOffset + decoder->headBufferSize) {
            
            // 计算在headBuffer中的偏移量
            int bufferOffset = readPos - decoder->headOffset;
            // 计算可以从headBuffer中读取多少数据
            int availableInHead = decoder->headBufferSize - bufferOffset;
            int canReadLen = MIN(len, availableInHead);
            
            LOG_INFO("readCallback: 从头部缓存读取数据，开始位置=%lld，缓冲区偏移=%d，读取长度=%d", 
                     readPos, bufferOffset, canReadLen);
            
            // 复制数据
            memcpy(data, decoder->headBuffer + bufferOffset, canReadLen);
            
            // 更新读取位置
            if (decoder->seek_pos >= 0) {
                decoder->seek_pos = -1; // 重置seek标志
            }
            decoder->fileReadPos = readPos + canReadLen;
            
            // LOG_DEBUG("readCallback: 从头部缓存读取%d字节，当前读取位置更新为%lld", 
            //          canReadLen, decoder->fileReadPos);
            
            ret = canReadLen;
            break;
        }else{
            LOG_DEBUG("readCallback: 头部缓存中没有数据，继续读取尾部缓存");
        }
        
        // 检查请求的数据是否位于tailBuffer中
        if (decoder->tailBuffer != NULL && decoder->tailBufferSize > 0 && 
            readPos >= decoder->tailOffset && 
            readPos < decoder->tailOffset + decoder->tailBufferSize) {
            
            // 计算在tailBuffer中的偏移量
            int bufferOffset = readPos - decoder->tailOffset;
            // 计算可以从tailBuffer中读取多少数据
            int availableInTail = decoder->tailBufferSize - bufferOffset;
            int canReadLen = MIN(len, availableInTail);
            
            LOG_INFO("readCallback: 从尾部缓存读取数据，开始位置=%lld, 缓冲区偏移=%d, 读取长度=%d", 
                     readPos, bufferOffset, canReadLen);
            
            // 复制数据
            memcpy(data, decoder->tailBuffer + bufferOffset, canReadLen);
            
            // 更新读取位置
            if (decoder->seek_pos >= 0) {
                decoder->seek_pos = -1; // 重置seek标志
            }
            decoder->fileReadPos = readPos + canReadLen;
            // LOG_DEBUG("readCallback: 从尾部缓存成功读取%d字节，当前读取位置更新为%lld", 
            //          canReadLen, decoder->fileReadPos);
            
            ret = canReadLen;
            break;
        }else{
            LOG_DEBUG("readCallback: 尾部缓存中没有数据，继续读取FIFO");
        }
    } while (0);
    LOG_DEBUG("readFormBuffer ret %d.", ret);
    return ret;
}

int readCallback(void *opaque, uint8_t *data, int len) {
    // LOG_DEBUG("readCallback %d.", len);
    int32_t ret = -1;
    do {
        if (decoder == NULL || decoder->fifo == NULL) {
            break;
        }

        if (data == NULL || len <= 0) {
            break;
        }
        if(decoder->gotStreamInfo == 0){
            ret = readFormBuffer(data, len);
            if(ret > 0) {
                break;
            }
            // ✅ 修复：headBuffer读完后返回EOF，避免无限循环
            // 对于packet输出模式，FFmpeg只需要读取headBuffer就能完成流分析
            LOG_INFO("readCallback: headBuffer已读完，设置EOF");
            decoder->readEof = 1;
            ret = AVERROR_EOF;  // 返回EOF让FFmpeg停止读取
            break;
        }else {
            int usedSpace = av_fifo_size(decoder->fifo);
            if (usedSpace < kMinFifoSize) {
                downloaderCtrl(kDownloaderCtrl_Resume);
            }
            // if (decoder->seek_pos >= 0) {
            //     // LOG_DEBUG("test readCallback 等待seek完成 %lld >= 0.", decoder->seek_pos);
            //     ret = (EAGAIN);
            //     break;
            // }
            // 如果已经获取到流信息，则从FIFO中读取数据
            if (usedSpace <= 0) {
                if(decoder->readEof == 1){
                    //文件数据读取完了
                    ret = 0;
                    break;
                }
                LOG_WARN("readCallback availableBytes <= 0.");
                ret = readFormBuffer(data, len);
                if(ret > 0) {
                    LOG_DEBUG("readCallback 从BUFFER中读取数据成功 %d.", ret);
                    break;
                }
                ret = (EAGAIN);
                break;
            }

            int canReadLen = MIN(usedSpace, len);
            av_fifo_generic_read(decoder->fifo, data, canReadLen, NULL);
            ret = canReadLen;
            decoder->fileReadPos += canReadLen;
            // LOG_DEBUG("readCallback 从FIFO中读取数据成功 %d.", ret);
            // printFifoRate();
            break;
        }
    } while (0);
    // LOG_DEBUG("readCallback ret %d. EAGAIN == %d.", ret, EAGAIN);
    return ret;
}

int64_t seekCallback(void *opaque, int64_t offset, int whence) {
    int64_t ret = -1;
    int64_t pos = -1;

    // LOG_DEBUG("seekCallback %lld %d.", offset, whence);
    do {
        if (decoder == NULL) {
            break;
        }

        if (whence == AVSEEK_SIZE) {
            ret = decoder->fileSize;
            break;
        }

        if (whence != SEEK_END && whence != SEEK_SET && whence != SEEK_CUR) {
            break;
        }

        if(decoder->gotStreamInfo == 1){
            LOG_ERROR("seekCallback: gotStreamInfo=%d, 不能进行seek操作.", decoder->gotStreamInfo);
            return -1;
        }

        if(whence == SEEK_SET) {
            decoder->seek_pos = offset;
        }else if(whence == SEEK_CUR) {
            decoder->seek_pos = decoder->fileReadPos + offset;
        }else if(whence == SEEK_END) {
            decoder->seek_pos = decoder->fileSize + offset;
        }
        ret = decoder->seek_pos;
        // if(decoder->gotStreamInfo == 0){
        //     LOG_DEBUG("seekCallback: gotStreamInfo=%d, 从buffer中读取数据 offset=%lld whence=%d", decoder->gotStreamInfo, offset, whence);
        // }
    } while (0);
    
    LOG_DEBUG("seekCallback offset %lld whence %d, return %d.", offset, whence, ret);
    return ret;
}

int ls_readCallback(void *opaque, uint8_t *data, int len) {
    // LOG_DEBUG("readCallback %d.", len);
    int32_t ret = -1;
    do {
        if (decoder == NULL || decoder->fifo == NULL) {
            break;
        }

        if (data == NULL || len <= 0) {
            break;
        }
        if(decoder->gotStreamInfo == 0){
            ret = readFormBuffer(data, len);
            if(ret > 0) {
                break;
            }
        }else {
            int usedSpace = av_fifo_size(decoder->fifo);
            if (usedSpace < kMinFifoSize) {
                downloaderCtrl(kDownloaderCtrl_Resume);
            }
            // if (decoder->seek_pos >= 0) {
            //     // LOG_DEBUG("test readCallback 等待seek完成 %lld >= 0.", decoder->seek_pos);
            //     ret = (EAGAIN);
            //     break;
            // }
            // 如果已经获取到流信息，则从FIFO中读取数据
            if (usedSpace <= 0) {
                if(decoder->readEof == 1){
                    //文件数据读取完了
                    ret = 0;
                    break;
                }
                LOG_WARN("readCallback availableBytes <= 0.");
                ret = readFormBuffer(data, len);
                if(ret > 0) {
                    LOG_DEBUG("readCallback 从BUFFER中读取数据成功 %d.", ret);
                    break;
                }
                ret = (EAGAIN);
                break;
            }

            int canReadLen = MIN(usedSpace, len);
            av_fifo_generic_read(decoder->fifo, data, canReadLen, NULL);
            ret = canReadLen;
            decoder->fileReadPos += canReadLen;
            // LOG_DEBUG("readCallback 从FIFO中读取数据成功 %d.", ret);
            // printFifoRate();
            break;
        }
    } while (0);
    // LOG_DEBUG("readCallback ret %d. EAGAIN == %d.", ret, EAGAIN);
    return ret;
}

int64_t ls_seekCallback(void *opaque, int64_t offset, int whence) {
    int64_t ret = -1;
    int64_t pos = -1;

    // LOG_DEBUG("seekCallback %lld %d.", offset, whence);
    do {
        if (decoder == NULL) {
            break;
        }

        if (whence == AVSEEK_SIZE) {
            ret = (decoder->fileSize - decoder->lsStartOffset)/2;
            break;
        }

        if (whence != SEEK_END && whence != SEEK_SET && whence != SEEK_CUR) {
            break;
        }

        if(decoder->gotStreamInfo == 1){
            LOG_ERROR("seekCallback: gotStreamInfo=%d, 不能进行seek操作.", decoder->gotStreamInfo);
            return -1;
        }

        if(whence == SEEK_SET) {
            decoder->seek_pos = offset;
        }else if(whence == SEEK_CUR) {
            decoder->seek_pos = decoder->fileReadPos + offset;
        }else if(whence == SEEK_END) {
            decoder->seek_pos = decoder->fileSize + offset;
        }
        ret = decoder->seek_pos;
        // if(decoder->gotStreamInfo == 0){
        //     LOG_DEBUG("seekCallback: gotStreamInfo=%d, 从buffer中读取数据 offset=%lld whence=%d", decoder->gotStreamInfo, offset, whence);
        // }
    } while (0);
    
    LOG_DEBUG("seekCallback offset %lld whence %d, return %d.", offset, whence, ret);
    return ret;
}

#if THUNDERSTONE_DECRTPT_SUPPORT
static inline int ts_header_check(unsigned char *buf, int size)
{
    int ret;

    // decoder->tsDecryptCheck = 1;

    if (size < ENCRYPT_CHUNK_SIZE + ENCRYPT_HEAD_SIZE) {
        LOG_ERROR("ts_header_check: size < ENCRYPT_CHUNK_SIZE + ENCRYPT_HEAD_SIZE");
        return -1;
    }

    decoder->tsDecrypt = tsInitDecrypt();
    if (!decoder->tsDecrypt) {
        LOG_ERROR("ts_header_check: tsInitDecrypt failed");
        return -2;
    }
    
    ret = tsCheckDecrypt(decoder->tsDecrypt, buf, ENCRYPT_HEAD_SIZE);
    if(ret < 0) { //not thunder media
        tsDeinitDecrypt(decoder->tsDecrypt);
        decoder->tsDecrypt = NULL;
        LOG_ERROR("ts_header_check: not thunder media");
        return -3;
    }

    LOG_INFO("ts_header_check: is thunder media");

    return 0;
}
#endif


//////////////////////////////////Export methods////////////////////////////////////////
ErrorCode initDecoder(int size, int logLv, int enableDecryption) {
    LOG_INFO("initDecoder start (enableDecryption=%d)", enableDecryption);
    ErrorCode ret = 0;
    do {
        //Log level.
        setLogLevel(logLv);
        LOG_INFO("initDecoder logLevel %d.", logLv);
        if (decoder != NULL) {
            // LOG_DEBUG("initDecoder decoder is not NULL.");
            break;
        }
        decoder = (WebDecoder *)av_mallocz(sizeof(WebDecoder));
        decoder->audioSwitch = 1;
        LOG_INFO("setFileSize %d.",size);
        decoder->fileSize = size;
        decoder->gotStreamInfo = 0;
        decoder->fifoSize = kDefaultFifoSize;
        decoder->fifo = av_fifo_alloc(decoder->fifoSize);
        decoder->dataOffset = 0;
        decoder->readEof = 0;
        decoder->auth_status = 0; // 初始化鉴权状态为未鉴权
#if THUNDERSTONE_DECRTPT_SUPPORT
        decoder->enableDecryption = enableDecryption;
        decoder->tsDecrypt = NULL;  // 稍后根据enableDecryption决定是否初始化
        decoder->alignFifo = av_fifo_alloc(kDefaultFifoSize);

        if (!enableDecryption) {
            LOG_WARN("⚠️ 解密已禁用 - 将直接播放加密数据（会花屏但验证IO通路）");
        } else {
            // ✅ 临时测试：启用解密时自动设置鉴权状态为成功
            // TODO: 生产环境需要真实的Thunder鉴权流程
            extern int g_auth_status;
            g_auth_status = 1;
            LOG_WARN("⚠️ [测试模式] 鉴权状态已强制设置为成功 (g_auth_status=1)");
            LOG_WARN("⚠️ 生产环境需要实现真实的Thunder鉴权流程");
        }
#endif
    } while (0);
    LOG_INFO("initDecoder success ret %d.", ret);
    return kErrorCode_Success;
}

// ErrorCode setFileSize(int size) {
//     if(decoder == NULL){
//         initDecoder(LEVEL_DEBUG);
//     }

//     return kErrorCode_Success;
// }

ErrorCode uninitDecoder() {
    if (decoder != NULL) {
        LOG_INFO("uninitDecoder.");

#if THUNDERSTONE_DECRTPT_SUPPORT
        if (decoder->tsDecrypt) {
            tsDeinitDecrypt(decoder->tsDecrypt);
            decoder->tsDecrypt = NULL;
            // decoder->tsDecryptCheck = 0;
        }
#endif

        if (decoder->fifo != NULL) {
            av_fifo_freep(&decoder->fifo);
            decoder->fifo = NULL;
        }

        if (decoder->alignFifo != NULL) {
            av_fifo_freep(&decoder->alignFifo);
            decoder->alignFifo = NULL;
        }

        if (decoder->headBuffer != NULL) {
            av_free(decoder->headBuffer);
            decoder->headBuffer = NULL;
            decoder->headBufferSize = 0;
            decoder->headOffset = 0;
        }

        if (decoder->tailBuffer != NULL) {
            av_free(decoder->tailBuffer);
            decoder->tailBuffer = NULL;
            decoder->tailBufferSize = 0;
            decoder->tailOffset = 0;
        }
        if(decoder->lsLyricsBuf != NULL){
            free(decoder->lsLyricsBuf);
            decoder->lsLyricsBuf = NULL;
        }

        av_freep(&decoder);
        decoder = NULL;
    }

    av_log_set_callback(NULL);

    LOG_INFO("uninitDecoder success.");
    return kErrorCode_Success;
}



void setDiscardAudioStream(){
    if(decoder == NULL){
        LOG_ERROR("setDiscardAudioStream decoder is NULL");
        return;
    }
    if(decoder->avformatContext == NULL){
        LOG_ERROR("setDiscardAudioStream decoder->avformatContext is NULL");
        return;
    }

    if(decoder->seeking){
        if(decoder->audioCodecContext){
            decoder->avformatContext->streams[decoder->audioStreamIdx]->discard = AVDISCARD_ALL;
        }
        if(decoder->audioCodecContext2){
            decoder->avformatContext->streams[decoder->audioStreamIdx2]->discard = AVDISCARD_ALL;
        }
    }else{
        if(decoder->audioCodecContext2){
            if(decoder->audioSwitch == 2){
                decoder->avformatContext->streams[decoder->audioStreamIdx]->discard = AVDISCARD_ALL;
                decoder->avformatContext->streams[decoder->audioStreamIdx2]->discard = AVDISCARD_DEFAULT;
            }else{
               decoder->avformatContext->streams[decoder->audioStreamIdx]->discard = AVDISCARD_DEFAULT;
                decoder->avformatContext->streams[decoder->audioStreamIdx2]->discard = AVDISCARD_ALL;
            }
        }else{
            if(decoder->audioCodecContext){
                decoder->avformatContext->streams[decoder->audioStreamIdx]->discard = AVDISCARD_DEFAULT;
            }
        }
    }
}

ErrorCode openDecoder(int mediaType,int *paramArray, int paramCount, 
    long videoCallback, 
    long audioCallback, 
    long downloaderCtrlCallback) {
    ErrorCode ret = kErrorCode_Success;
    int r = 0;
    int i = 0;
    // 参数数组扩展为10个元素，用于存放额外的编解码器信息
    int params[14] = { 0 };
    // 定义字符串缓冲区，用于存储编解码器和格式信息
    static char videoCodecNameBuffer[256] = {0};
    static char pixFmtNameBuffer[256] = {0};
    static char audioCodecNameBuffer[256] = {0};
    static char audioCodecNameBuffer2[256] = {0};
    
    if(mediaType == MEDIA_TYPE_LS || mediaType == MEDIA_TYPE_ULS){
        LOG_INFO("openDecoder mediaType is ls media.");
        char headBuf[48] = {};
        decoder->lsStartOffset = 48;
        readCallback(NULL,headBuf,48);
        LOG_INFO("openDecoder headBuf %s.",headBuf);

        if(!strstr(headBuf,"THUNDERSTONE_MUSIC v1.0")){
            LOG_ERROR("openDecoder headBuf check ls head string error.");
            return kErrorCode_Invalid_Data;
        }
        decoder->lsStartOffset += sizeof(decoder->ls_files);
        readCallback(NULL,decoder->ls_files,sizeof(decoder->ls_files));
        for (size_t i = 0; i < 3; i++){
            LOG_INFO("file[%d] name %s",i,decoder->ls_files[i].fileName);
            LOG_INFO("file[%d] fileLen %d",i,decoder->ls_files[i].fileLen);
            LOG_INFO("file[%d] fileOffset %d",i,decoder->ls_files[i].fileOffset);
        }
        int lyricsLen = decoder->ls_files[0].fileLen;
        decoder->lsLyricsBuf = malloc(lyricsLen);
        if(decoder->lsLyricsBuf == NULL){
            LOG_ERROR("openDecoder malloc lyricsBuf error.");
            return kErrorCode_Invalid_Data;
        }
        memset(decoder->lsLyricsBuf,0,lyricsLen);
        decoder->lsStartOffset += lyricsLen;
        readCallback(NULL,decoder->lsLyricsBuf,lyricsLen);
        // LOG_INFO("lsLyricsBuf == %s",decoder->lsLyricsBuf);
        LOG_INFO("lsStartOffset == %d",decoder->lsStartOffset);
        // return kErrorCode_Invalid_Data;
        decoder->mediaType = MEDIA_TYPE_LS;
    }

    LOG_INFO("reset tsDataDecryptSeek to 0.");
    tsDataDecryptSeek(decoder->tsDecrypt, 0);
    do {
        // LOG_INFO("openDecoder mediaType is MEDIA_TYPE_TS.");

        av_register_all();
        avcodec_register_all();

        av_log_set_callback(ffmpegLogCallback);
        
        decoder->avformatContext = avformat_alloc_context();
        decoder->customIoBuffer = (unsigned char*)av_mallocz(kCustomIoBufferSize);
        decoder->videoCallback = (VideoCallback)videoCallback;
        decoder->audioCallback = (AudioCallback)audioCallback;
        decoder->downloaderCtrlCallback = (DownloaderCtrlCallback)downloaderCtrlCallback;

        AVIOContext* ioContext;
        if(decoder->mediaType == MEDIA_TYPE_LS){
            ioContext = avio_alloc_context(
                decoder->customIoBuffer,
                kCustomIoBufferSize,
                0,
                NULL,
                ls_readCallback,
                NULL,
                ls_seekCallback);
            if (ioContext == NULL) {
                ret = kErrorCode_FFmpeg_Error;
                LOG_ERROR("avio_alloc_context failed.");
                break;
            }
        }else{
            ioContext = avio_alloc_context(
                decoder->customIoBuffer,
                kCustomIoBufferSize,
                0,
                NULL,
                readCallback,
                NULL,
                seekCallback);
            if (ioContext == NULL) {
                ret = kErrorCode_FFmpeg_Error;
                LOG_ERROR("avio_alloc_context failed.");
                break;
            }
        }

        decoder->avformatContext->pb = ioContext;
        decoder->avformatContext->flags = AVFMT_FLAG_CUSTOM_IO;

        r = avformat_open_input(&decoder->avformatContext, NULL, NULL, NULL);
        if (r != 0) {
            ret = kErrorCode_FFmpeg_Error;
            char err_info[32] = { 0 };
            av_strerror(ret, err_info, 32);
            LOG_ERROR("avformat_open_input failed %d %s.", ret, err_info);
            break;
        }
        
        LOG_INFO("avformat_open_input success.");

        r = avformat_find_stream_info(decoder->avformatContext, NULL);
        if (r != 0) {
            ret = kErrorCode_FFmpeg_Error;
            LOG_ERROR("av_find_stream_info failed %d.", ret);
            break;
        }
        decoder->gotStreamInfo = 1;

        r = openCodecContext(
            decoder->avformatContext,
            AVMEDIA_TYPE_VIDEO,
            0,
            &decoder->videoStreamIdx,
            &decoder->videoCodecContext);
        if (r != 0) {
            ret = kErrorCode_FFmpeg_Error;
            LOG_ERROR("Open video codec context failed %d.", ret);
            break;
        }

        LOG_INFO("Open video codec context success, video stream index %d %x.",
            decoder->videoStreamIdx, (unsigned int)decoder->videoCodecContext);

        // 保存视频编解码器名称和像素格式名称到静态缓冲区
        strncpy(videoCodecNameBuffer, avcodec_get_name(decoder->videoCodecContext->codec_id), 255);
        strncpy(pixFmtNameBuffer, av_get_pix_fmt_name(decoder->videoCodecContext->pix_fmt), 255);
        
        LOG_INFO("Video stream index:%d codec_id:%s pix_fmt:%s resolution:%d*%d.",
            decoder->videoStreamIdx,
            videoCodecNameBuffer,
            pixFmtNameBuffer,
            decoder->videoCodecContext->width,
            decoder->videoCodecContext->height);

        r = openCodecContext(
            decoder->avformatContext,
            AVMEDIA_TYPE_AUDIO,
            1,
            &decoder->audioStreamIdx,
            &decoder->audioCodecContext);
        if (r != 0) {
            // ✅ 不中断：允许仅视频模式（音频流可能不存在）
            LOG_ERROR("Could not find audio stream");
            decoder->audioCodecContext = NULL;
            decoder->audioStreamIdx = -1;
        }

        if(decoder->avformatContext->nb_streams > 2){
            r = openCodecContext(
                decoder->avformatContext,
                AVMEDIA_TYPE_AUDIO,
                2,
                &decoder->audioStreamIdx2,
                &decoder->audioCodecContext2);
            if (r != 0) {
                // ✅ 不中断：允许仅视频模式（第二音频流可能不存在）
                LOG_ERROR("Could not find audio stream 2");
                decoder->audioCodecContext2 = NULL;
                decoder->audioStreamIdx2 = -1;
            }else{
                strncpy(audioCodecNameBuffer2, avcodec_get_name(decoder->audioCodecContext2->codec_id), 255);
            }
        }

        for (i = 0; i < decoder->avformatContext->nb_streams; i++) {
            if(i == decoder->videoStreamIdx){
                decoder->avformatContext->streams[i]->discard = AVDISCARD_DEFAULT;
            }else{
                decoder->avformatContext->streams[i]->discard = AVDISCARD_ALL;
            }
        }

        setDiscardAudioStream();

        // ✅ 仅在音频流存在时处理音频信息
        if (decoder->audioCodecContext != NULL) {
            // 保存音频编解码器名称到静态缓冲区
            strncpy(audioCodecNameBuffer, avcodec_get_name(decoder->audioCodecContext->codec_id), 255);

            LOG_INFO("Open audio codec context success, audio stream index %d %x.",
                decoder->audioStreamIdx, (unsigned int)decoder->audioCodecContext);

            LOG_INFO("Audio stream index:%d codec_id:%s sample_fmt:%d channel:%d, sample rate:%d.",
                decoder->audioStreamIdx,
                audioCodecNameBuffer,
                decoder->audioCodecContext->sample_fmt,
                decoder->audioCodecContext->channels,
                decoder->audioCodecContext->sample_rate);
        }
        if(decoder->audioStreamIdx2 > 0 && decoder->audioCodecContext2){
            LOG_INFO("Open audio2 codec context success, audio stream index %d %x.",
                decoder->audioStreamIdx2, (unsigned int)decoder->audioCodecContext2);
            LOG_INFO("Audio stream2 index:%d codec_id:%s sample_fmt:%d channel:%d, sample rate:%d.",
                decoder->audioStreamIdx2,
                audioCodecNameBuffer2,
                decoder->audioCodecContext2->sample_fmt,
                decoder->audioCodecContext2->channels,
                decoder->audioCodecContext2->sample_rate);
        }
        // decoder->fileWritePos = 0;
        decoder->fileReadPos = 0;
        decoder->seek_pos = 0;
        // av_seek_frame(decoder->avformatContext, -1, 0, AVSEEK_FLAG_BACKWARD);

        /* For RGB Renderer(2D WebGL).
        decoder->swsCtx = sws_getContext(
            decoder->videoCodecContext->width,
            decoder->videoCodecContext->height,
            decoder->videoCodecContext->pix_fmt, 
            decoder->videoCodecContext->width,
            decoder->videoCodecContext->height,
            AV_PIX_FMT_RGB32,
            SWS_BILINEAR, 
            0, 
            0, 
            0);
        if (decoder->swsCtx == NULL) {
            LOG_ERROR("sws_getContext failed.");
            ret = kErrorCode_FFmpeg_Error;
            break;
        }
        */
        
        decoder->videoSize = avpicture_get_size(
            decoder->videoCodecContext->pix_fmt,
            decoder->videoCodecContext->width,
            decoder->videoCodecContext->height);

        decoder->videoBufferSize = 3 * decoder->videoSize;
        decoder->yuvBuffer = (unsigned char *)av_mallocz(decoder->videoBufferSize);
        decoder->avFrame = av_frame_alloc();
        
        params[0] = 1000 * (decoder->avformatContext->duration + 5000) / AV_TIME_BASE;
        params[1] = decoder->videoCodecContext->pix_fmt;
        params[2] = decoder->videoCodecContext->width;
        params[3] = decoder->videoCodecContext->height;
        // ✅ 音频参数：仅在音频流存在时填充
        if (decoder->audioCodecContext != NULL) {
            params[4] = decoder->audioCodecContext->sample_fmt;
            params[5] = decoder->audioCodecContext->channels;
            params[6] = decoder->audioCodecContext->sample_rate;
        } else {
            params[4] = 0;
            params[5] = 0;
            params[6] = 0;
        }
        if(decoder->audioCodecContext2){
            params[7] = decoder->audioCodecContext2->sample_fmt;
            params[8] = decoder->audioCodecContext2->channels;
            params[9] = decoder->audioCodecContext2->sample_rate;
        }else{
            params[7] = 0;
            params[8] = 0;
            params[9] = 0;
        }
        // 第10、11、12、13个参数保存字符串缓冲区的地址
        params[10] = (int)(intptr_t)videoCodecNameBuffer;
        params[11] = (int)(intptr_t)pixFmtNameBuffer;
        params[12] = (int)(intptr_t)audioCodecNameBuffer;
        params[13] = (int)(intptr_t)audioCodecNameBuffer2;

        // ✅ 音频格式转换：仅在音频流存在时处理
        if (decoder->audioCodecContext != NULL) {
            enum AVSampleFormat sampleFmt = decoder->audioCodecContext->sample_fmt;
            if (av_sample_fmt_is_planar(sampleFmt)) {
                const char *packed = av_get_sample_fmt_name(sampleFmt);
                params[4] = av_get_packed_sample_fmt(sampleFmt);
            }
        }
        if(decoder->audioCodecContext2){
             enum AVSampleFormat sampleFmt2 = decoder->audioCodecContext2->sample_fmt;
            if (av_sample_fmt_is_planar(sampleFmt2)) {
                const char *packed = av_get_sample_fmt_name(sampleFmt2);
                params[7] = av_get_packed_sample_fmt(sampleFmt2);
            }
        }

        if (paramArray != NULL && paramCount > 0) {
            // 复制所有参数，包括额外的编解码器信息
            int copyCount = paramCount > 14 ? 14 : paramCount;
            for (int i = 0; i < copyCount; ++i) {
                paramArray[i] = params[i];
            }
        }

        LOG_INFO("Decoder opened, duration %ds, picture size %d.", params[0], decoder->videoSize);
    } while (0);

    if (ret != kErrorCode_Success && decoder != NULL) {
        av_freep(&decoder);
    }
    return ret;
}

ErrorCode closeDecoder() {
    ErrorCode ret = kErrorCode_Success;
    do {
        if (decoder == NULL || decoder->avformatContext == NULL) {
            break;
        }

        if (decoder->videoCodecContext != NULL) {
            closeCodecContext(decoder->avformatContext, decoder->videoCodecContext, decoder->videoStreamIdx);
            decoder->videoCodecContext = NULL;
            LOG_INFO("Video codec context closed.");
        }

        if (decoder->audioCodecContext != NULL) {
            closeCodecContext(decoder->avformatContext, decoder->audioCodecContext, decoder->audioStreamIdx);
            decoder->audioCodecContext = NULL;
            LOG_INFO("Audio codec context closed.");
        }
        if (decoder->audioCodecContext2 != NULL) {
            closeCodecContext(decoder->avformatContext, decoder->audioCodecContext2, decoder->audioStreamIdx2);
            decoder->audioCodecContext2 = NULL;
            LOG_INFO("Audio2 codec context closed.");
        }

        AVIOContext *pb = decoder->avformatContext->pb;
        if (pb != NULL) {
            if (pb->buffer != NULL) {
                av_freep(&pb->buffer);
                decoder->customIoBuffer = NULL;
            }
            av_freep(&decoder->avformatContext->pb);
            LOG_INFO("IO context released.");
        }

        avformat_close_input(&decoder->avformatContext);
        decoder->avformatContext = NULL;
        LOG_INFO("Input closed.");

        if (decoder->yuvBuffer != NULL) {
            av_freep(&decoder->yuvBuffer);
            decoder->yuvBuffer = NULL;
        }

        if (decoder->pcmBuffer != NULL) {
            av_freep(&decoder->pcmBuffer);
            decoder->pcmBuffer = NULL;
        }
        
        if (decoder->avFrame != NULL) {
            av_freep(&decoder->avFrame);
            decoder->avFrame = NULL;
        }
        LOG_INFO("All buffer released.");
    } while (0);
    return ret;
}

/**
 *                 int mRealSegPos = (offset - ENCRYPT_HEAD_SIZE)/ENCRYPT_CHUNK_SIZE;
                // LOG_INFO("thunderstone en seek mRealSegPos %d", mRealSegPos);
                tsDataDecryptSeek(decoder->tsDecrypt, mRealSegPos);
                tsDataDecrypt(decoder->tsDecrypt, buff, size);
 */

static int alignFifoWrite(int offset, unsigned char *buff, int size) {
    int ret = 0;
    if(decoder == NULL){
        LOG_ERROR("alignFifoWrite decoder is NULL");
        return -1;
    }
    if(decoder->fifo == NULL){
        LOG_ERROR("alignFifoWrite decoder->fifo is NULL");
        return -1;
    }
    if(decoder->tsDecrypt == NULL){
        ret = av_fifo_generic_write(decoder->fifo, buff, size, NULL);
        return ret;
    }
    int alignFifoSize = av_fifo_size(decoder->alignFifo);
    if(alignFifoSize == 0){
        int mRealSegPos = (offset - ENCRYPT_HEAD_SIZE)/ENCRYPT_CHUNK_SIZE;
        tsDataDecryptSeek(decoder->tsDecrypt, mRealSegPos);
    }
    int alignFifoSpace = av_fifo_space(decoder->alignFifo);
    if(alignFifoSpace < size){
        // 计算需要额外增加的空间
        int additional_space = size - alignFifoSpace + 512*1024; // 额外多分配512KB空间作为缓冲
        LOG_INFO("alignFifoWrite alignFifo空间不足，尝试扩容，当前剩余空间: %d，需要空间: %d，将增加: %d字节", 
                  alignFifoSpace, size, additional_space);
        
        // 使用av_fifo_grow扩容alignFifo
        int grow_ret = av_fifo_grow(decoder->alignFifo, additional_space);
        if (grow_ret < 0) {
            LOG_ERROR("alignFifoWrite alignFifo扩容失败，错误码: %d", grow_ret);
            return -1;
        }
        
        LOG_INFO("alignFifoWrite alignFifo扩容成功");
        
        // 重新检查空间是否足够
        alignFifoSpace = av_fifo_space(decoder->alignFifo);
        if(alignFifoSpace < size) {
            LOG_ERROR("alignFifoWrite 扩容后空间仍不足，alignFifoSpace %d < size %d", alignFifoSpace, size);
            return -1;
        }
    }
    av_fifo_generic_write(decoder->alignFifo, buff, size, NULL);
    alignFifoSize = av_fifo_size(decoder->alignFifo);
    int alignSize = alignFifoSize - alignFifoSize%ENCRYPT_CHUNK_SIZE;
    if(alignSize > 0){
        av_fifo_generic_read(decoder->alignFifo, decoder->alignBuffer, alignSize, NULL);
        // if(ret != alignSize){
        //     LOG_ERROR("alignFifoWrite av_fifo_generic_read ret %d != alignSize %d.", ret, alignSize);
        //     return -1;
        // }
        // tsDataDecryptSeek(decoder->tsDecrypt, 0);
        tsDataDecrypt(decoder->tsDecrypt, decoder->alignBuffer, alignSize);
        ret = av_fifo_generic_write(decoder->fifo, decoder->alignBuffer, alignSize, NULL);
    }else{
        ret = 0;
    }
    return ret;
}

int sendData(int offset, unsigned char *buff, int size, int type) {
    // LOG_DEBUG("decoder.c sendData %ld %d type %d.", offset, size, type);
    int ret = 0;
    do {
        if (buff == NULL || size == 0) {
            ret = -2;
            break;
        }

        // ✅ 修复：type=1的stream data应该总是写入FIFO，不管gotStreamInfo状态
        //    原来的逻辑导致openDecoder之前的stream data被丢弃
        if(type == 1){
            // gotStreamInfo==1时才做offset检查
            if(decoder->gotStreamInfo == 1 && decoder->dataOffset != offset){
                av_fifo_reset(decoder->fifo);
                av_fifo_reset(decoder->alignFifo);
                LOG_INFO("sendData 重置FIFO, 数据偏移量不匹配, offset %d, size %d, decoder->dataOffset %d.", offset, size, decoder->dataOffset);
            }
            decoder->dataOffset = offset + size;
            // if(decoder->seek_pos >= 0) {
            //     av_fifo_reset(decoder->fifo);
            //     if(decoder->seek_pos == offset){
            //         decoder->fileReadPos = offset;
            //         decoder->seek_pos = -1;
            //         LOG_DEBUG("fffff sendData 等待seek完成,写入seek后的数据. offset %d, size %d.", offset, size);
            //         ret = av_fifo_generic_write(decoder->fifo, buff, size, NULL);
            //         break;
            //     }else{
            //         LOG_WARN("fffff decoder->seek_pos %d != offset %d, size %d.", decoder->seek_pos, offset, size);
            //         ret = -100;
            //         break;
            //     }
            // }
            if(decoder->fileSize > 0 && offset + size >= decoder->fileSize){
                decoder->readEof = 1;
            }else{
                //有可能有seek的时候,offset就重新算起了
                decoder->readEof = 0;
            }
            int leftSpace = av_fifo_space(decoder->fifo);
            if(leftSpace < size){
                // 计算需要额外增加的空间
                int additional_space = size - leftSpace + 512*1024; // 额外多分配512KB空间作为缓冲
                LOG_INFO("sendData FIFO空间不足，尝试扩容，当前剩余空间: %d，需要空间: %d，将增加: %d字节", 
                          leftSpace, size, additional_space);
                
                // 使用av_fifo_grow扩容FIFO
                int grow_ret = av_fifo_grow(decoder->fifo, additional_space);
                if (grow_ret < 0) {
                    LOG_ERROR("sendData FIFO扩容失败，错误码: %d", grow_ret);
                    ret = kErrorCode_Fifo_Full;
                    break;
                }
                
                // 更新FIFO总大小
                decoder->fifoSize += additional_space;
                LOG_INFO("sendData FIFO扩容成功，新大小: %d字节", decoder->fifoSize);
                
                // 重新检查空间是否足够
                leftSpace = av_fifo_space(decoder->fifo);
                if(leftSpace < size) {
                    LOG_ERROR("sendData 扩容后空间仍不足，leftSpace %d < size %d", leftSpace, size);
                    ret = kErrorCode_Fifo_Full;
                    break;
                }
            }
            
            // 在进行解密前检查鉴权状态
#if THUNDERSTONE_DECRTPT_SUPPORT
            if (decoder->tsDecrypt && decoder->enableDecryption) {
                // ✅ 只有在启用解密时才检查鉴权状态
                decoder->auth_status = get_auth_status();
                if (decoder->auth_status != 1) {
                    LOG_ERROR("鉴权失败或未鉴权，无法进行解密操作");
                    return -100; // 返回特定错误码表示鉴权问题
                }

                // 继续正常的解密操作...
            }
#endif
            
            if(decoder->tsDecrypt){
                // 8KB对齐
                ret = alignFifoWrite(offset, buff, size);
                // LOG_INFO("thunderstone en tdDecrypt->mRealSegPos %d", decoder->tsDecrypt->mRealSegPos);
            }else{
                ret = av_fifo_generic_write(decoder->fifo, buff, size, NULL);
            }
            int usedSpace = av_fifo_size(decoder->fifo);
            if (usedSpace > kMaxFifoSize) {
                downloaderCtrl(kDownloaderCtrl_Pause);
            }
            // printFifoRate();
            break;
        }

        // 处理头部数据
        if (type == 0) {
            // 释放旧的头部缓冲区
            if (decoder->headBuffer != NULL) {
                av_free(decoder->headBuffer);
            }

            // 分配新的头部缓冲区
            decoder->headBuffer = (unsigned char*)av_mallocz(size);
            if (decoder->headBuffer == NULL) {
                LOG_ERROR("Failed to allocate head buffer");
                ret = -3;
                break;
            }

            // ✅ 检查是否启用解密
            if(decoder->enableDecryption && ts_header_check(buff, size) == 0){
                // 完全照搬软解方案
                memcpy(decoder->headBuffer, buff + ENCRYPT_HEAD_SIZE, size - ENCRYPT_HEAD_SIZE);
                decoder->headBufferSize = size - ENCRYPT_HEAD_SIZE;
                decoder->headOffset = offset;
                LOG_INFO("Head data saved, size: %d, offset: %d",
                        decoder->headBufferSize, decoder->headOffset);
                ret = decoder->headBufferSize;
                // ✅ 关键发现：headBuffer解密不需要成功！真正的解密在alignFifoWrite
                // 但为了和软解方案保持一致，仍然尝试解密（会失败但不影响播放）
                tsDataDecryptSeek(decoder->tsDecrypt, 0);

                // 计算8KB对齐的大小（和tailBuffer处理一样）
                int dataSize = size - ENCRYPT_HEAD_SIZE;  // 去掉magic header
                int decryptSize = dataSize - (dataSize % ENCRYPT_CHUNK_SIZE);  // 8KB对齐

                LOG_INFO("Head decrypt: dataSize=%d, decryptSize=%d (aligned)", dataSize, decryptSize);

                if (decryptSize > 0) {
                    // ✅ 按软解正确逻辑：解密整个headBuffer
                    // headBuffer = buff + 512 (去掉magic header)，全部是需要解密的TS数据
                    // Thunder加密格式：[0-511]=magic header, [512-...]=加密的segment 0开始
                    int decryptRet = tsDataDecrypt(decoder->tsDecrypt, decoder->headBuffer, decryptSize);
                    if(decryptRet){
                        LOG_WARN("Decrypt head data failed: %d", decryptRet);
                    }else{
                        LOG_INFO("Decrypt head data ok: %d bytes", decryptSize);
                    }
                }

                // 无论解密成功与否，都继续（headBuffer主要用于缓存）

                // ✅ 关键修复：只写入解密后的部分，而不是整个headBuffer
                // 因为只有8KB对齐的部分被解密了，剩余的未对齐部分还是加密的
                int writeSize = decryptSize;  // 只写入解密的8KB
                if (writeSize > 0) {
                    LOG_INFO("Writing decrypted header data to FIFO for libmedia, size: %d (only decrypted part)", writeSize);
                    int writeRet = av_fifo_generic_write(decoder->fifo, decoder->headBuffer, writeSize, NULL);
                    if (writeRet < 0) {
                        LOG_ERROR("Failed to write decrypted header to FIFO: %d", writeRet);
                    } else {
                        LOG_INFO("Decrypted header data written to FIFO successfully");
                    }
                }

                // ⚠️ 未对齐的剩余部分(headBuffer[8192-15252])还是加密的
                // 需要保存到alignFifo中，等待后续stream数据凑够8KB后一起解密
                int remainSize = dataSize - decryptSize;  // 未对齐的剩余部分
                if (remainSize > 0) {
                    LOG_INFO("Saving remaining %d bytes to alignFifo (unaligned, still encrypted)", remainSize);
                    av_fifo_generic_write(decoder->alignFifo, decoder->headBuffer + decryptSize, remainSize, NULL);
                }

                // 更新dataOffset
                decoder->dataOffset = offset + size;
            }else{
                // ✅ 解密已禁用或非加密文件：直接保存原始数据
                memcpy(decoder->headBuffer, buff, size);
                decoder->headBufferSize = size;
                decoder->headOffset = offset;
                LOG_INFO("Head data saved (no decryption), size: %d, offset: %d", size, offset);
                ret = size;

                // ✅ 明文文件也要更新dataOffset
                decoder->dataOffset = offset + size;

                // ✅ 新增：将header数据也写入FIFO，这样ThunderWASMBridge.read()能读取到完整TS流
                LOG_INFO("Writing header data to FIFO for libmedia direct read, size: %d", size);
                int writeRet = av_fifo_generic_write(decoder->fifo, buff, size, NULL);
                if (writeRet < 0) {
                    LOG_ERROR("Failed to write header to FIFO: %d", writeRet);
                } else {
                    LOG_INFO("Header data written to FIFO successfully");
                }
            }
            break;
        }
        if (type == 100) {
            // 释放旧的尾部缓冲区
            if (decoder->tailBuffer != NULL) {
                av_free(decoder->tailBuffer);
            }
            if(decoder->tsDecrypt){
                int mRealSegPos = (offset - ENCRYPT_HEAD_SIZE)/ENCRYPT_CHUNK_SIZE;
                tsDataDecryptSeek(decoder->tsDecrypt, mRealSegPos);
                int decryptSize = size - size%ENCRYPT_CHUNK_SIZE;
                if(tsDataDecrypt(decoder->tsDecrypt, buff, decryptSize)){
                    LOG_DEBUG("Decrypt tail data err.");
                }else{
                    LOG_DEBUG("Decrypt tail data ok.");
                }
            }

            decoder->tailBuffer = (unsigned char*)av_mallocz(size);
            if (decoder->tailBuffer == NULL) {
                LOG_ERROR("Failed to allocate tail buffer");
                ret = -3;
                break;
            }
            memcpy(decoder->tailBuffer, buff, size);
            decoder->tailBufferSize = size;
            decoder->tailOffset = offset;
            LOG_DEBUG("Tail data saved, size: %d, offset: %d, buffer: %p", 
                     size, offset, decoder->tailBuffer);
            ret = size;
            break;
        }
    } while (0);
    return ret;
}

ErrorCode seekTo(double timestamp_in_seconds) {
    if (decoder == NULL || decoder->avformatContext == NULL) {
        return kErrorCode_Invalid_State;
    }
    
    if(decoder->fifo){
        av_fifo_reset(decoder->fifo);
    }
    if(decoder->alignFifo){
        av_fifo_reset(decoder->alignFifo);
    }

    decoder->seekTimestamp = timestamp_in_seconds;
    decoder->seeking = 1;

    //视频流 只解码关键帧
    decoder->avformatContext->streams[decoder->videoStreamIdx]->discard = AVDISCARD_NONKEY;
    setDiscardAudioStream();
    return kErrorCode_Success;
}

ErrorCode decodeOnePacket() {
    ErrorCode ret	= kErrorCode_Success;
    int decodedLen	= 0;
    int r			= 0;

    AVPacket packet;
    av_init_packet(&packet);
    do {
        if (decoder == NULL) {
            ret = kErrorCode_Invalid_State;
            break;
        }
        if(decoder->fifo == NULL){
            ret = kErrorCode_Invalid_State;
            break;
        }
        if (decoder->readEof == 0 && av_fifo_size(decoder->fifo) <= kMinDecoderSize) {
            ret = kErrorCode_Invalid_State;
            downloaderCtrl(kDownloaderCtrl_Resume);
            break;
        }

        packet.data = NULL;
        packet.size = 0;

        r = av_read_frame(decoder->avformatContext, &packet);
        if (r == AVERROR_EOF) {
            ret = kErrorCode_Eof;
            break;
        }
        if (packet.stream_index == decoder->videoStreamIdx && decoder->seeking == 1) {
            if(packet.flags & AV_PKT_FLAG_KEY) {
                LOG_DEBUG("get i frame....");
                decoder->seeking = 0;
                decoder->avformatContext->streams[decoder->videoStreamIdx]->discard = AVDISCARD_DEFAULT;
                setDiscardAudioStream();
            }else{
                // LOG_DEBUG("finding i frame....");
                continue;
            }
        }


        if (r < 0 || packet.size == 0) {
            break;
        }

        do {
            ret = decodePacket(&packet, &decodedLen);
            if (ret != kErrorCode_Success) {
                break;
            }

            if (decodedLen <= 0) {
                break;
            }

            packet.data += decodedLen;
            packet.size -= decodedLen;
        } while (packet.size > 0);
    } while (0);
    av_packet_unref(&packet);
    return ret;
}

// 添加音轨切换函数声明
ErrorCode switchAudioTrack() {
    if(decoder == NULL){
        return kErrorCode_Invalid_State;
    }
    if(decoder->audioSwitch == 1){
        decoder->audioSwitch = 2;
    }else{
        decoder->audioSwitch = 1;
    }
    setDiscardAudioStream();
    // 这里暂时不实现具体功能，等待后续实现
    LOG_INFO("switchAudioTrack called, but not implemented yet.");
    return kErrorCode_Success;
}

// ✅ 新增：读取一个packet并通过回调输出（用于libmedia硬解）
int readOnePacket() {
    if (decoder == NULL) {
        LOG_ERROR("readOnePacket: decoder is NULL");
        return -1;
    }

    if (decoder->avformatContext == NULL) {
        LOG_ERROR("readOnePacket: avformatContext is NULL");
        return -2;
    }

    if (decoder->packetCallback == NULL) {
        LOG_ERROR("readOnePacket: packetCallback is NULL");
        return -3;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    // 从FFmpeg读取一个packet
    int ret = av_read_frame(decoder->avformatContext, &packet);
    if (ret < 0) {
        if (ret == AVERROR_EOF) {
            LOG_INFO("readOnePacket: EOF reached");
        } else {
            LOG_ERROR("readOnePacket: av_read_frame failed, ret=%d", ret);
        }
        return ret;
    }

    // 判断流类型
    int stream_type = -1;
    if (packet.stream_index == decoder->videoStreamIdx) {
        stream_type = 0;  // video
    } else if (packet.stream_index == decoder->audioStreamIdx) {
        stream_type = 1;  // audio
    } else {
        // 其他流，跳过
        av_packet_unref(&packet);
        return 0;
    }

    // 调用回调输出packet
    decoder->packetCallback(
        stream_type,
        packet.data,
        packet.size,
        packet.pts,
        packet.dts,
        packet.flags
    );

    av_packet_unref(&packet);
    return 0;
}

// ✅ 新增：设置packet回调
void setPacketCallback(void *callback) {
    if (decoder == NULL) {
        LOG_ERROR("setPacketCallback: decoder is NULL");
        return;
    }
    decoder->packetCallback = (PacketCallback)callback;
    LOG_INFO("setPacketCallback: callback set to %p", callback);
}

// ✅ 新增：获取stream信息的导出函数
int getVideoStreamIndex() {
    if (decoder == NULL || decoder->avformatContext == NULL) {
        return -1;
    }
    return decoder->videoStreamIdx;
}

int getAudioStreamIndex() {
    if (decoder == NULL || decoder->avformatContext == NULL) {
        return -1;
    }
    return decoder->audioStreamIdx;
}

int getVideoCodecId() {
    if (decoder == NULL || decoder->avformatContext == NULL || decoder->videoStreamIdx < 0) {
        return -1;
    }
    return decoder->avformatContext->streams[decoder->videoStreamIdx]->codecpar->codec_id;
}

int getAudioCodecId() {
    if (decoder == NULL || decoder->avformatContext == NULL || decoder->audioStreamIdx < 0) {
        return -1;
    }
    return decoder->avformatContext->streams[decoder->audioStreamIdx]->codecpar->codec_id;
}

int getVideoWidth() {
    if (decoder == NULL || decoder->avformatContext == NULL || decoder->videoStreamIdx < 0) {
        return 0;
    }
    return decoder->avformatContext->streams[decoder->videoStreamIdx]->codecpar->width;
}

int getVideoHeight() {
    if (decoder == NULL || decoder->avformatContext == NULL || decoder->videoStreamIdx < 0) {
        return 0;
    }
    return decoder->avformatContext->streams[decoder->videoStreamIdx]->codecpar->height;
}

int getAudioSampleRate() {
    if (decoder == NULL || decoder->avformatContext == NULL || decoder->audioStreamIdx < 0) {
        return 0;
    }
    return decoder->avformatContext->streams[decoder->audioStreamIdx]->codecpar->sample_rate;
}

int getAudioChannels() {
    if (decoder == NULL || decoder->avformatContext == NULL || decoder->audioStreamIdx < 0) {
        return 0;
    }
    // 兼容旧版本FFmpeg，使用channels而不是ch_layout
    return decoder->avformatContext->streams[decoder->audioStreamIdx]->codecpar->channels;
}

// ✅ 新增：直接从FIFO读取原始TS流（供ThunderWASMBridge使用）
// 这样libmedia能收到TS流而不是packet
int readFromFIFO(unsigned char *buffer, int size) {
    if (decoder == NULL || decoder->fifo == NULL) {
        LOG_ERROR("readFromFIFO: decoder or fifo is NULL");
        return -1;
    }

    if (buffer == NULL || size <= 0) {
        LOG_ERROR("readFromFIFO: invalid buffer or size");
        return -2;
    }

    // 获取FIFO中可读数据量
    int availableSize = av_fifo_size(decoder->fifo);

    if (availableSize <= 0) {
        // 检查是否EOF
        if (decoder->readEof == 1) {
            return 0;  // EOF
        }
        return 0;  // 暂无数据，但不是EOF
    }

    // 读取数据
    int readSize = MIN(availableSize, size);
    av_fifo_generic_read(decoder->fifo, buffer, readSize, NULL);
    decoder->fileReadPos += readSize;

    LOG_DEBUG("readFromFIFO: read %d bytes from FIFO (available: %d)", readSize, availableSize);
    return readSize;
}

// ✅ 新增：获取FIFO当前使用量（用于流控）
int getFIFOSize() {
    if (decoder == NULL || decoder->fifo == NULL) {
        return 0;
    }
    return av_fifo_size(decoder->fifo);
}

#ifdef __cplusplus
}
#endif
