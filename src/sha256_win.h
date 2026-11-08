// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SO5_SHA256_WIN_H
#define SO5_SHA256_WIN_H
#include <windows.h>
#include <bcrypt.h>
static int so5_image_sha256(const void *data, size_t size, char output[65]) {
    BCRYPT_ALG_HANDLE algorithm=NULL; BCRYPT_HASH_HANDLE hash=NULL;
    unsigned char *object=NULL,digest[32]; DWORD length=0,got=0; int ok=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&length,sizeof(length),&got,0)<0)goto done;
    object=(unsigned char *)HeapAlloc(GetProcessHeap(),0,length);
    if(!object||BCryptCreateHash(algorithm,&hash,object,length,NULL,0,0)<0)goto done;
    while(size) {
        ULONG chunk=(ULONG)(size>0x40000000u?0x40000000u:size);
        if(BCryptHashData(hash,(PUCHAR)data,chunk,0)<0)goto done;
        data=(const unsigned char *)data+chunk;size-=chunk;
    }
    if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0)goto done;
    for(unsigned i=0;i<32;++i){const char *hex="0123456789abcdef";output[i*2]=hex[digest[i]>>4];output[i*2+1]=hex[digest[i]&15];}
    output[64]=0;ok=1;
done:
    if(hash)BCryptDestroyHash(hash);
    if(object)HeapFree(GetProcessHeap(),0,object);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok)output[0]=0;
    return ok;
}
#endif
