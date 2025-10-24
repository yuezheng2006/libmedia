// Rijndael.h: interface for the Rijndael class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _C_RIJNDAEL_H
#define _C_RIJNDAEL_H

typedef int BOOL;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

typedef unsigned char byte;

typedef struct _CRijndael {
    int Nk, Nr, round;
    byte (*State)[4], *w[4], *key[4];
} CRijndael;

void initCRijndael(CRijndael *p);

void releaseCRijndael(CRijndael *p);

BOOL SetVariable(CRijndael *p, int k, int r, const char *skey);

void Encryption(CRijndael *p);

void Decryption(CRijndael *p);

#endif
