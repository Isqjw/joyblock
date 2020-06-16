#ifndef __JOY_BLOCK_H
#define __JOY_BLOCK_H

#include "joyconst.h"

struct JoyBlockConfig {
    int blockNum;       //block的数量
    int blockSize;      //单个block的大小
    int blockChainLen;  //block链表的长度
};

struct JoyBlock {
    int nextUsedPos;

    int dataHead;
    int dataTail;
    char data[1];       // 每个block4M,非循环队列,具体大小可以配置
};

// 缓存链表
struct JoyBlockChain {
    int sendhead;   // block链表头
    int recvhead;   // block链表头
};

struct JoyBlockMem {
    // block链表索引(zoneid作为下标)
    int blockSize;
    int maxBlockChainLen;
    struct JoyBlockChain blockChains[kJoynetMaxProcID];
};


#ifdef __cplusplus
extern "C" {
#endif

int joyBlockInit(struct JoyBlockConfig cfg, int shmkey);

int joyBlockWriteSendPkg(int procid, struct JoynetRWBuf *wbuf);
int joyBlockWriteRecvPkg(int procid, struct JoynetRWBuf *wbuf);

int joyBlockReadSendPkg(int procid, struct JoynetRWBuf *rbuf);
int joyBlockReadRecvPkg(int procid, struct JoynetRWBuf *rbuf);

int joyBlockReleaseSendBuf(int procid, struct JoynetRWBuf *rbuf);
int joyBlockReleaseRecvBuf(int procid, struct JoynetRWBuf *rbuf);

// int joyBlockSendData(int fd, int procid);
// int joyBlockRecvData(int fd, int procid);
int joyBlockReleaseBlockChain(int procid);
int joyBlockHaveData(int procid);

int joyBlockMemCheck();

#ifdef __cplusplus
}
#endif

#endif
