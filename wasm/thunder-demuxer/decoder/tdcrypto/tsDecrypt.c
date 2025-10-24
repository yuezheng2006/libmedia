#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/timeb.h>
#include <dlfcn.h>
#include "libavutil/log.h"
#include "tsDecryptPRV.h"
#include "tsDecrypt.h"
#include "../log.h"

typedef struct TDDecrypt {
    CryptService cryptService;
    DecryptInfoBlock FileHeadInfo;
    unsigned int mRealSegPos;
} TDDecrypt;

#define FREEOBJ(obj) if(obj){free(obj); obj = NULL;}

#define HEAD_SIZE 512
#define PACK_SIZE (8 * 1024)

void TDDebugFunctionEx(char *file, char *function, int line, char *message) {

}

void *tsInitDecrypt(void)
{
    TDDecrypt *tdDecrypt = (TDDecrypt *)malloc(sizeof(TDDecrypt));
    if (!tdDecrypt) {
        av_log(NULL, AV_LOG_ERROR,"thunderstone malloc failed.");
        return NULL;
    }

    memset(tdDecrypt, 0, sizeof(TDDecrypt));
    initCryptService(&tdDecrypt->cryptService);

    return (void *)tdDecrypt;
}

void tsDeinitDecrypt(void *handle)
{
    TDDecrypt *tdDecrypt = (TDDecrypt *)handle;
    if(!tdDecrypt)
        return;

    releaseCryptService(&tdDecrypt->cryptService);
    FREEOBJ(tdDecrypt);
    av_log(NULL, AV_LOG_INFO,"thunderstone encrypt is over.");

    return;
}

int tsCheckDecrypt(void *handle, unsigned char *buffer, int size)
{
    TDDecrypt *tdDecrypt = (TDDecrypt *)handle;
    if(!tdDecrypt)
        return -1;

    if(size != HEAD_SIZE)
        return -2;

    GetDeHeadInfo(&tdDecrypt->cryptService, (unsigned char *) buffer, &tdDecrypt->FileHeadInfo);
    // GetDeHeadInfoVideo(buffer, &tdDecrypt->FileHeadInfo, &tdDecrypt->cryptService);
    av_log(NULL, AV_LOG_INFO,"thunderstone encrypt is %d ", tdDecrypt->FileHeadInfo.isEncrypt);

    if (tdDecrypt->FileHeadInfo.isEncrypt > 0)
        return 0;
    return -3;
}

int tsDataDecrypt(void *handle, unsigned char *buffer, int size)
{
    TDDecrypt *tdDecrypt = (TDDecrypt *)handle;
    if(!tdDecrypt)
        return -1;
    
    if(size % PACK_SIZE)
        return -2;

    // LOG_INFO("thunderstone en data %d", size);

    // LOG_INFO("tsDataDecrypt:111 mRealSegPos %d", tdDecrypt->mRealSegPos);

    void* ret = En2Data(&(tdDecrypt->cryptService), &(tdDecrypt->FileHeadInfo), (unsigned char *) (buffer),
    size, &(tdDecrypt->mRealSegPos));

    // En2DataVideo(buffer, &(tdDecrypt->FileHeadInfo), size,
    //              &(tdDecrypt->mRealSegPos), &(tdDecrypt->cryptService));
    // LOG_INFO("tsDataDecrypt:222 mRealSegPos %d ret %p", tdDecrypt->mRealSegPos,ret);
    return 0;
}

int tsDataDecryptSeek(void *handle, unsigned long pos)
{
    TDDecrypt *tdDecrypt = (TDDecrypt *)handle;
    if (!tdDecrypt)
        return -1;

    tdDecrypt->mRealSegPos = pos;

    return 0 ;
}

// This was removed from POSIX 2008.
// int ftime(struct timeb* tb)
// {
//     struct timeval  tv;
//     struct timezone tz;

//     if (gettimeofday(&tv, &tz) < 0)
//         return -1;

//     tb->time    = tv.tv_sec;
//     tb->millitm = (tv.tv_usec + 500) / 1000;

//     if (tb->millitm == 1000) {
//         ++tb->time;
//         tb->millitm = 0;
//     }

//     tb->timezone = tz.tz_minuteswest;
//     tb->dstflag  = tz.tz_dsttime;

//     return 0;
// }