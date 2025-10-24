#ifndef TS_DECRYPT_H
#define TS_DECRYPT_H
#ifdef __cplusplus
extern "C" {
#endif
void TDDebugFunctionEx(char *file, char *function, int line, char *message);
void *tsInitDecrypt(void);
void tsDeinitDecrypt(void *handle);
int tsCheckDecrypt(void *handle, unsigned char *buffer, int size);
int tsDataDecrypt(void *handle, unsigned char *buffer, int size);
int tsDataDecryptSeek(void *handle, unsigned long pos);

#ifdef __cplusplus
};
#endif
#endif //TS_DECRYPT_H