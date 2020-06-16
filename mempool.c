#include "mempool.h"

static char *memPoolGetPoolBase_(void *poolptr)
{
    return ((char *)poolptr - sizeof(struct MemPoolHead));
}

static char *memPoolGetBlockBase_(char *poolbase)
{
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;

    return poolbase + sizeof(struct MemPoolHead) + pool->customPoolHeadSize;
}

static char *memPoolGetBlockByPos_(char *poolbase, int pos)
{
    if (NULL == poolbase) {
        debug_msg("error: invalid param, poolbase[%p] pos[%d]", poolbase, pos);
        return NULL;
    }

    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;
    if (pos < 0 || pos >= pool->blockNum) {
        debug_msg("error: invalid pos[%d].", pos);
        return NULL;
    }

    char *block = memPoolGetBlockBase_(poolbase);

    return block + pos * pool->blockSize;
}

size_t memPoolCalSize(size_t customPoolHeadSize, size_t blockSize, int blockNum)
{
    size_t poolsize = sizeof(struct MemPoolHead) + customPoolHeadSize + (sizeof(struct MemBlockHead) + blockSize) * blockNum;

    return poolsize;
}

char *memPoolGetBlockByPos(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return NULL;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    char *block = memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return NULL;
    }

    return block + sizeof(struct MemBlockHead);
}

int memPoolGetFirstUsedPos(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;
    return pool->firstUsedPos;
}

int memPoolGetLastUsedPos(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;
    return pool->lastUsedPos;
}

int memPoolGetNextUsedPos(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemBlockHead *block = (struct MemBlockHead *)memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return -1;
    }

    return block->nextUsedPos;
}

int memPoolGetPrevUsedPos(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemBlockHead *block = (struct MemBlockHead *)memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return -1;
    }

    return block->prevUsedPos;
}

char *memPoolGetNextUsedBlock(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return NULL;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemBlockHead *block = (struct MemBlockHead *)memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return NULL;
    }

    if (block->nextUsedPos < 0) {
        return NULL;
    }

    return memPoolGetBlockByPos(poolbase, block->nextUsedPos);
}

char *memPoolGetprevUsedBlock(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return NULL;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemBlockHead *block = (struct MemBlockHead *)memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return NULL;
    }

    if (block->prevUsedPos < 0) {
        return NULL;
    }

    return memPoolGetBlockByPos(poolbase, block->prevUsedPos);
}

int memPoolInit(void *poolbase, size_t customPoolHeadSize, size_t blockSize, int blockNum)
{
    if (NULL == poolbase || customPoolHeadSize < 0 || blockSize <= 0 || blockNum <= 0) {
        debug_msg("invalid param, pool[%p], customPoolHeadSize[%ld] size[%ld], num[%d]",
                poolbase, customPoolHeadSize, blockSize, blockNum);
        return -1;
    }

    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;
    pool->customPoolHeadSize = customPoolHeadSize;
    pool->blockSize = blockSize + sizeof(struct MemBlockHead);
    pool->blockNum = blockNum;
    pool->firstUsedPos = -1;
    pool->lastUsedPos = -1;
    pool->firstAvailPos = 0;
    for (int pos = 0; pos < pool->blockNum; ++pos) {
        struct MemBlockHead *block = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase, pos);
        block->isused = 0;
        block->prevUsedPos = -1;
        block->nextUsedPos = -1;
        if (pos < pool->blockNum - 1) {
            block->nextAvailPos = pos + 1;
        } else {
            pool->lastAvailPos = pos;
            block->nextAvailPos = -1;
        }
    }

    return 0;
}

// 申请之后注意初始化
int memPoolAllocBlock(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolbase[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;
    if (pool->firstAvailPos < 0) {
        debug_msg("error: no more block to alloc");
        return -1;
    }

    int allocpos = pool->firstAvailPos;
    struct MemBlockHead *allocBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase, allocpos);
    if (NULL == allocBlock) {
        debug_msg("error: fail to get block by pos[%d]", allocpos);
        return -1;
    }

    if (pool->firstAvailPos == pool->lastAvailPos) {
        pool->firstAvailPos = -1;
        pool->lastAvailPos = -1;
    } else {
        pool->firstAvailPos = allocBlock->nextAvailPos;
    }
    allocBlock->isused = 1;

    // 维护链表
    if (pool->firstUsedPos < 0) {
        pool->firstUsedPos = allocpos;
    }

    if (0 <= pool->lastUsedPos) {
        struct MemBlockHead *tailBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase,
                pool->lastUsedPos);
        tailBlock->nextUsedPos = allocpos;
    }

    allocBlock->prevUsedPos = pool->lastUsedPos;
    allocBlock->nextUsedPos = -1;
    pool->lastUsedPos = allocpos;

    return allocpos;
}

int memPoolReleaseBlock(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolbase[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;
    if (pos < 0 || pos >= pool->blockNum) {
        debug_msg("error: invalid pos[%d].", pos);
        return -1;
    }

    struct MemBlockHead *releaseBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase, pos);
    if (NULL == releaseBlock) {
        debug_msg("error: fail to get block by pos[%d]", pos);
        return -1;
    }

    if (0 == releaseBlock->isused) {
        debug_msg("error: fail to release block which had not uesd, pos[%d]", pos);
        return -1;
    }

    if (-1 == pool->lastAvailPos) {
        pool->firstAvailPos = pos;
        pool->lastAvailPos = pos;
    } else {
        struct MemBlockHead *lastAvailBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase,
                pool->lastAvailPos);
        if (NULL == lastAvailBlock) {
            debug_msg("error: fail to get block by pos[%d]", pool->lastAvailPos);
            return -1;
        }

        lastAvailBlock->nextAvailPos = pos;
        pool->lastAvailPos = pos;
    }

    releaseBlock->nextAvailPos = -1;

    // 维护链表
    if (pos == pool->firstUsedPos) {
        pool->firstUsedPos = releaseBlock->nextUsedPos;
    }

    if (pos == pool->lastUsedPos) {
        pool->lastUsedPos = releaseBlock->prevUsedPos;
    }

    if (0 <= releaseBlock->prevUsedPos) {
        struct MemBlockHead *lastUsedBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase,
                releaseBlock->prevUsedPos);
        if (NULL == lastUsedBlock) {
            debug_msg("error: fail to get block by pos[%d]", releaseBlock->prevUsedPos);
            return -1;
        }
        lastUsedBlock->nextUsedPos = releaseBlock->nextUsedPos;
    }

    if (0 <= releaseBlock->nextUsedPos) {
        struct MemBlockHead *nextUsedBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase,
                releaseBlock->nextUsedPos);
        if (NULL == nextUsedBlock) {
            debug_msg("error: fail to get block by pos[%d]", releaseBlock->prevUsedPos);
            return -1;
        }
        nextUsedBlock->prevUsedPos = releaseBlock->prevUsedPos;
    }

    return 0;
}

char *memPoolGetHead(void *poolbase)
{
    if (NULL == poolbase) {
        debug_msg("error: invalid param, poolbase[%p]", poolbase);
        return NULL;
    }

    return (char *)poolbase + sizeof(struct MemPoolHead);
}

size_t memPoolGetBlockSize(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param , poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;

    return pool->blockSize;
}

int memPoolGetBlockNum(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param , poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;

    return pool->blockNum;
}

int memPoolAvailable(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param , poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;

    return 0 <= pool->firstAvailPos ? 1 : 0;
}

int memPoolGetUsedNum(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolbase[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;

    int nextUesdNum = 0;
    int tmppos = pool->firstUsedPos;
    while(0 <= tmppos) {
        struct MemBlockHead *tmpBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase, tmppos);
        if (NULL == tmpBlock) {
            debug_msg("error: fail to get block by pos[%d]", tmppos);
            return -1;
        }
        nextUesdNum++;
        tmppos = tmpBlock->nextUsedPos;
    }

    return nextUesdNum;
}

/*
 * 保护逻辑:
 * 验证内存池数据是否正确
 */
int memPoolCheck(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolbase[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPoolHead *pool = (struct MemPoolHead *)poolbase;

    int nextUesdNum = 0;
    int tmppos = pool->firstUsedPos;
    while(0 <= tmppos) {
        struct MemBlockHead *tmpBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase, tmppos);
        if (NULL == tmpBlock) {
            debug_msg("error: fail to get block by pos[%d]", tmppos);
            return -1;
        }
        nextUesdNum++;
        tmppos = tmpBlock->nextUsedPos;
    }

    int prevUsedNum = 0;
    tmppos = pool->lastUsedPos;
    while(0 <= tmppos) {
        struct MemBlockHead *tmpBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase, tmppos);
        if (NULL == tmpBlock) {
            debug_msg("error: fail to get block by pos[%d]", tmppos);
            return -1;
        }
        prevUsedNum++;
        tmppos = tmpBlock->prevUsedPos;
    }

    if (prevUsedNum != nextUesdNum) {
        debug_msg("error: invalid data, prevUsedNum[%d], nextUesdNum[%d]", prevUsedNum, nextUesdNum);
        return -1;
    }

    int availNum = 0;
    tmppos = pool->firstAvailPos;
    while(0 <= tmppos) {
        struct MemBlockHead *tmpBlock = (struct MemBlockHead *)memPoolGetBlockByPos_((char *)poolbase, tmppos);
        if (NULL == tmpBlock) {
            debug_msg("error: fail to get block by pos[%d]", tmppos);
            return -1;
        }
        availNum++;
        tmppos = tmpBlock->nextAvailPos;
    }

    if (availNum + nextUesdNum != pool->blockNum) {
        debug_msg("error: invalid block num, availNum[%d], usedNum[%d], totalNum[%d]",
                availNum, nextUesdNum, pool->blockNum);
    }

    debug_msg("debug: availNum[%d], usedNum[%d], totalNum[%d]", availNum, nextUesdNum, pool->blockNum);

    return 0;
}

