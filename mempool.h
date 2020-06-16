#ifndef __MEM_POOL_H
#define __MEM_POOL_H

#include "debug.h"

// memblock head
struct MemBlockHead {
    int nextAvailPos;   // 下一个可用节点
    int nextUsedPos;    // 下一个已用节点
    int prevUsedPos;    // 上一个已用节点
    char isused;        // 是否被使用
};

struct MemPoolHead {
    size_t blockSize;           // 单个block大小
    size_t customPoolHeadSize;     // 内存池用户头大小
    int blockNum;

    int firstAvailPos;          // 第一个可用节点
    int lastAvailPos;           // 最后一个可用节点
    int firstUsedPos;           // 第一个已用节点
    int lastUsedPos;            // 最后一个已用节点
};

struct MemBlockList {
    struct MemBlockHead blocks[1];
};

#ifdef __cplusplus
extern "C" {
#endif

size_t memPoolCalSize(size_t customPoolHeadSize, size_t blockSize, int blockNum);
char *memPoolGetBlockByPos(void *poolptr, int pos);
int memPoolGetLastUsedPos(void *poolptr);
int memPoolGetFirstUsedPos(void *poolptr);
int memPoolGetNextUsedPos(void *poolptr, int pos);
int memPoolGetPrevUsedPos(void *poolptr, int pos);
char *memPoolGetNextUsedBlock(void *poolptr, int pos);
char *memPoolGetprevUsedBlock(void *poolptr, int pos);
int memPoolInit(void *poolbase, size_t customPoolHeadSize, size_t blockSize, int blockNum);
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
