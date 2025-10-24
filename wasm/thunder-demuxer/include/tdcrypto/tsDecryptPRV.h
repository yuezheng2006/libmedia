//
// Created by zhangshichuan on 2018/1/10.
//

#ifndef TDCRYPTOINC_DECRYPT_H
#define TDCRYPTOINC_DECRYPT_H

#include "d3des.h"
#include "CRijndael.h"
#include <stddef.h>
#include <stdio.h>

//#if _MSC_VER
//#define snprintf _snprintf
//#endif

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef short INT16;
typedef unsigned long U32INT;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef int BOOL;

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE  0
#endif

enum {
    DES = 0,
    AES
};



#define EN 0
#define DE 1

typedef struct _CryptographInfo {
    UINT8 sParam[4];//reserved  **0x00--**0x03
    UINT8 sSeqmentSize;//Log the size of cryptograph seqment  **0x04
    UINT8 sDataMode;//Cryptograph seqment mode  **0x05 
    UINT8 sDataModeInfo;//Specifed the cryptograph seqment mode infomation  **0x06
    UINT8 sEncryptMode;//Specifed the encrypt method  **0x07
    UINT8 sPrimaryKeyIndex;//The primary key index of self  **0x08
    UINT8 sStartPos;//The start position of encrypted segment **0x09
    U32INT sSourceFileSize;//The size of source file(didn't encrypted) **0x0a-0x0d                
    //  **0x0e - 0x33 don't tie up 
    UINT8 EncryptSign[12];//If this file has encrypted ,the sign would be write in address 0x34-0x3f **0x34-0x3f
} CryptographInfo, *pCryptographInfo;

typedef struct _DecryptInfoBlock {
    BOOL isEncrypt;
    unsigned char uCryptMethod;
    unsigned char uPrimaryKeyIndex;
    unsigned char uLocalKey[8];
    unsigned char uSegStartPos;
    unsigned char uSegMode;
    unsigned char uSegStep;
    unsigned char uSegSize;
    unsigned long uSegSizeUnit;
} DecryptInfoBlock, *pDecryptInfoBlock;

typedef struct _CryptService {
    char szBuffer0[100];
    char szBuffer1[100];
    char szBuffer2[100];
    char szBuffer3[100];
    char szBuffer4[100];
    char szBuffer5[100];
    char szBuffer6[100];
    char szBuffer7[100];
    char szBuffer8[100];
    char szBuffer9[100];
    char szBufferA[100];
    char szBufferB[100];
    char szBufferC[100];
    char szBufferD[100];
    char szBufferE[100];
    char szBufferF[100];

    UINT8 *lBuffer;

    int m_nCryptMethod;
    pCryptographInfo pCryptInfo;//Cryptograph info of file which has Encrypted
    UINT8 *sDesKey;//key of file encrypt
    UINT16 keymode;
    U32INT bBufSize;//
    CRijndael m_AES;
} CryptService;

void initCryptService(CryptService *p);

void releaseCryptService(CryptService *p);

UINT8 *GetDesKey(CryptService *p);

void SetDesKey(CryptService *p, UINT8 *tmpdeskey);

UINT16 GetDesMode(CryptService *p);

void SetDesMode(CryptService *p, short mode);

BOOL SetDesBuf(CryptService *p, U32INT bufsize);

U32INT GetBufSize(CryptService *p);

void ReleaseDesBuf(CryptService *p);

pCryptographInfo GetEncryptInfoHead(CryptService *p);

UINT8 *MakeEncryptInfoHead(CryptService *p, pCryptographInfo pCryptInfo, UINT8 *ploaclKey,
                           UINT8 *hHeadBlock);

UINT8 *desBuf(CryptService *p, UINT8 *inBuf, U32INT inBufSize);

void *En2Data(CryptService *ptDecrypt, DecryptInfoBlock *pDeInfoBlk, unsigned char * EnData,  unsigned int nDataLen, unsigned int * nNextSegPos);

pDecryptInfoBlock GetDeHeadInfo(CryptService *p, unsigned char *nEnHeadBlock, pDecryptInfoBlock ptmpDeInfo);

int SetCryptMethod(CryptService *p, int nMethod);


#endif //TDCRYPTOINC_DECRYPT_H
