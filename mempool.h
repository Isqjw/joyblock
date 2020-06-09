#ifndef __MEM_POOL_H
#define __MEM_POOL_H

#include "debug.h"

struct MemBlock {
    int nextAvailPos;   // 下一个可用节点
    int nextUsedPos;    // 下一个已用节点
    int preUsedPos;     // 上一个已用节点
    char isused;        // 是否被使用
};

struct MemPool {
    size_t blockSize;           // 单个block大小
    size_t memPoolHeadSize;     // 内存池用户头大小
    int blockNum;

    int firstAvailPos;          // 第一个可用节点
    int lastAvailPos;           // 最后一个可用节点
    int firstUsedPos;           // 第一个已用节点
    int lastUsedPos;            // 最后一个已用节点
};

struct MemBlockList {
    struct MemBlock blocks[1];
};

#ifdef __cplusplus
extern "C" {
#endif

size_t memPoolCalSize(size_t memPoolHeadSize, size_t blockSize, int blockNum);
char *memPoolGetBlockByPos(void *poolptr, int pos);
int memPoolGetLastUsedPos(void *poolptr);
int memPoolGetFristUsedPos(void *poolptr);
int memPoolGetNextUesdPos(void *poolptr, int pos);
int memPoolGetPreUesdPos(void *poolptr, int pos);
char *memPoolGetNextUsedBlock(void *poolptr, int pos);
char *memPoolGetPreUsedBlock(void *poolptr, int pos);
int memPoolInit(void *poolbase, size_t memPoolHeadSize, size_t blockSize, int blockNum);
int memPoolAllocBlock(void *poolptr);
int memPoolReleaseBlock(void *poolptr, int pos);
char *memPoolGetHead(void *poolbase);
int memPoolGetBlockNum(void *poolptr);
size_t memPoolGetBlockSize(void *poolptr);
int memPoolAvailable(void *poolptr);
int memPoolCheck(void *poolptr);
int memPoolGetUsedNum(void *poolptr);

#ifdef __cplusplus
}
#endif

#endif
