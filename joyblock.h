#ifndef __JOY_BLOCK_H
#define __JOY_BLOCK_H

#include "debug.h"
#include "joynet.h"

#include <stdlib.h>

#define kMaxBlockChainNum  65536

#ifdef __cplusplus

extern "C" {
#endif

struct JoyBlockConfig {
    int blockNum;       //block的数量
    int blockSize;      //单个block的大小
    int blockChainLen;  //block链表的长度
};

struct JoyBufferBlock {
    int nextAvailPos;   // 下一个可用节点位置
    int nextUsedPos;    // 下一个已占用的block位置

    char isTake;        // 是否被占用

    int dataHead;
    int dataTail;
    char data[1];       // 每个block4M,非循环队列,具体大小可以配置
};

// 缓存链表
struct JoyBlockChain {
    int sendhead;   // block链表头
    int recvhead;   // block链表头
};

/*
 * 处理读取数据时, 数据在两个block的两头的情况
 * 绝大数情况都只用到第一个buf
 */
struct JoyBlockRWBuf {
    int len[2];
    char *buf[2];
};

struct JoyBufferBlockList {
    struct JoyBlockConfig config;

    int firstAvailPos;
    int lastAvailPos;

    // block链表索引(zoneid作为下标)
    struct JoyBlockChain blockChains[kMaxBlockChainNum];

    struct JoyBufferBlock blocks[1];   // 具体大小可以配置
};

int joyBlockInit(struct JoyBlockConfig cfg);
int joyBlockWriteSendPkg(int procid, struct JoyBlockRWBuf *wbuf);
int joyBlockReadRecvPkg(int procid, struct JoyBlockRWBuf *rbuf);
int joyBlockReleaseRecvBuf(int procid, struct JoyBlockRWBuf *rbuf);
int joyBlockSendData(int fd, int procid);
int joyBlockRecvData(int fd, int procid);
int joyBlockReleaseBlockChain(int procid);
// int joyBlockGetSendBufLeftRoom(int procid);

int joyBlockListCheck();

#ifdef __cplusplus
}
#endif

#endif
