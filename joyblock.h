#ifndef __JOY_BLOCK_H
#define __JOY_BLOCK_H

#include "joyconst.h"

struct JoyBlockConfig {
    int blockNum;       //block的数量
    int blockSize;      //单个block的大小
    int blockChainLen;  //block链表的长度
    int shmkey;         // 共享内存key
};

struct JoyBlock {
    int nextUsedPos;

    int dataHead;
    int dataTail;
    char data[1];       // 每个block4M,非循环队列,具体大小可以配置
};

struct JoyChainHead {
    int head;
    int tail;
    int len;
};

// 缓存链表
struct JoyBlockChain {
    struct JoyChainHead sendhead; // block链表头
};

struct JoyBlockMem {
    struct JoyBlockConfig cfg;
    struct JoyChainHead recvhead; // block链表头(所有node共用一个接收缓存)
    // block链表索引(zoneid作为下标)
    struct JoyBlockChain blockChains[kJoynetMaxProcID];
};


#ifdef __cplusplus
extern "C" {
#endif

int joyBlockInit(struct JoyBlockConfig cfg);

int joyBlockWriteSendPkg(int procid, struct JoynetRWBuf *wbuf);
int joyBlockWriteRecvPkg(struct JoynetRWBuf *wbuf);

int joyBlockReadSendPkg(int procid, struct JoynetRWBuf *rbuf);
int joyBlockReadRecvPkg(struct JoynetRWBuf *rbuf);

int joyBlockReleaseSendBuf(int procid, struct JoynetRWBuf *rbuf);
int joyBlockReleaseRecvBuf(struct JoynetRWBuf *rbuf);

// int joyBlockSendData(int fd, int procid);
// int joyBlockRecvData(int fd, int procid);
// int joyBlockReleaseBlockChain(int procid);

int joyBlockMemCheck();
int joyBlockGetUsage();
int joyBlockGetUsedNum();

#ifdef __cplusplus
}
#endif

#endif
