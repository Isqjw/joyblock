#include "mempool.h"

static char *memPoolGetPoolBase_(void *poolptr)
{
    return ((char *)poolptr - sizeof(struct MemPool));
}

static char *memPoolGetBlokBase_(char *poolbase)
{
    struct MemPool *pool = (struct MemPool *)poolbase;

    return poolbase + sizeof(struct MemPool) + pool->memPoolHeadSize;
}

static char *memPoolGetBlockByPos_(char *poolbase, int pos)
{
    if (NULL == poolbase) {
        debug_msg("error: invalid param, poolbase[%p] pos[%d]", poolbase, pos);
        return NULL;
    }

    struct MemPool *pool = (struct MemPool *)poolbase;
    if (pos < 0 || pos >= pool->blockNum) {
        debug_msg("error: invalid pos[%d].", pos);
        return NULL;
    }

    char *block = memPoolGetBlokBase_(poolbase);

    return block + pos * pool->blockSize;
}

size_t memPoolCalSize(size_t memPoolHeadSize, size_t blockSize, int blockNum)
{
    size_t poolsize = sizeof(struct MemPool) + memPoolHeadSize + (sizeof(struct MemBlock) + blockSize) * blockNum;

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

    return block + sizeof(struct MemBlock);
}

int memPoolGetFristUsedPos(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPool *pool = (struct MemPool *)poolbase;
    return pool->firstUsedPos;
}

int memPoolGetLastUsedPos(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPool *pool = (struct MemPool *)poolbase;
    return pool->lastUsedPos;
}

int memPoolGetNextUesdPos(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemBlock *block = (struct MemBlock *)memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return -1;
    }

    return block->nextUsedPos;
}

int memPoolGetPreUesdPos(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemBlock *block = (struct MemBlock *)memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return -1;
    }

    return block->preUsedPos;
}

char *memPoolGetNextUsedBlock(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return NULL;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemBlock *block = (struct MemBlock *)memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return NULL;
    }

    if (block->nextUsedPos < 0) {
        return NULL;
    }

    return memPoolGetBlockByPos(poolbase, block->nextUsedPos);
}

char *memPoolGetPreUsedBlock(void *poolptr, int pos)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolptr[%p]", poolptr);
        return NULL;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemBlock *block = (struct MemBlock *)memPoolGetBlockByPos_(poolbase, pos);
    if (NULL == block){
        debug_msg("error: fail to get block by pos[%d]", pos);
        return NULL;
    }

    if (block->preUsedPos < 0) {
        return NULL;
    }

    return memPoolGetBlockByPos(poolbase, block->preUsedPos);
}

int memPoolInit(void *poolbase, size_t memPoolHeadSize, size_t blockSize, int blockNum)
{
    if (NULL == poolbase || memPoolHeadSize < 0 || blockSize <= 0 || blockNum <= 0) {
        debug_msg("invalid param, pool[%p], memPoolHeadSize[%ld] size[%ld], num[%d]",
                poolbase, memPoolHeadSize, blockSize, blockNum);
        return -1;
    }

    struct MemPool *pool = (struct MemPool *)poolbase;
    pool->memPoolHeadSize = memPoolHeadSize;
    pool->blockSize = blockSize + sizeof(struct MemBlock);
    pool->blockNum = blockNum;
    pool->firstUsedPos = -1;
    pool->lastUsedPos = -1;
    pool->firstAvailPos = 0;
    for (int pos = 0; pos < pool->blockNum; ++pos) {
        struct MemBlock *block = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase, pos);
        block->isused = 0;
        block->preUsedPos = -1;
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
    struct MemPool *pool = (struct MemPool *)poolbase;
    int allocpos = pool->firstAvailPos;
    struct MemBlock *allocBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase, allocpos);
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
        struct MemBlock *tailBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase,
                pool->lastUsedPos);
        tailBlock->nextUsedPos = allocpos;
    }

    allocBlock->preUsedPos = pool->lastUsedPos;
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
    struct MemPool *pool = (struct MemPool *)poolbase;
    if (pos < 0 || pos >= pool->blockNum) {
        debug_msg("error: invalid pos[%d].", pos);
        return -1;
    }

    struct MemBlock *releaseBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase, pos);
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
        struct MemBlock *lastAvailBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase,
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
        pool->lastUsedPos = releaseBlock->preUsedPos;
    }

    if (0 <= releaseBlock->preUsedPos) {
        struct MemBlock *lastUsedBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase,
                releaseBlock->preUsedPos);
        if (NULL == lastUsedBlock) {
            debug_msg("error: fail to get block by pos[%d]", releaseBlock->preUsedPos);
            return -1;
        }
        lastUsedBlock->nextUsedPos = releaseBlock->nextUsedPos;
    }

    if (0 <= releaseBlock->nextUsedPos) {
        struct MemBlock *nextUsedBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase,
                releaseBlock->nextUsedPos);
        if (NULL == nextUsedBlock) {
            debug_msg("error: fail to get block by pos[%d]", releaseBlock->preUsedPos);
            return -1;
        }
        nextUsedBlock->preUsedPos = releaseBlock->preUsedPos;
    }

    return 0;
}

char *memPoolGetHead(void *poolbase)
{
    if (NULL == poolbase) {
        debug_msg("error: invalid param, poolbase[%p]", poolbase);
        return NULL;
    }

    return (char *)poolbase + sizeof(struct MemPool);
}

size_t memPoolGetBlockSize(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param , poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPool *pool = (struct MemPool *)poolbase;

    return pool->blockSize;
}

int memPoolGetBlockNum(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param , poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPool *pool = (struct MemPool *)poolbase;

    return pool->blockNum;
}

int memPoolAvailable(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param , poolptr[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPool *pool = (struct MemPool *)poolbase;

    return 0 <= pool->firstAvailPos ? 1 : 0;
}

int memPoolGetUsedNum(void *poolptr)
{
    if (NULL == poolptr) {
        debug_msg("error: invalid param, poolbase[%p]", poolptr);
        return -1;
    }

    char *poolbase = memPoolGetPoolBase_(poolptr);
    struct MemPool *pool = (struct MemPool *)poolbase;

    int nextUesdNum = 0;
    int tmppos = pool->firstUsedPos;
    while(0 <= tmppos) {
        struct MemBlock *tmpBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase, tmppos);
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
    struct MemPool *pool = (struct MemPool *)poolbase;

    int nextUesdNum = 0;
    int tmppos = pool->firstUsedPos;
    while(0 <= tmppos) {
        struct MemBlock *tmpBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase, tmppos);
        if (NULL == tmpBlock) {
            debug_msg("error: fail to get block by pos[%d]", tmppos);
            return -1;
        }
        nextUesdNum++;
        tmppos = tmpBlock->nextUsedPos;
    }

    int preUsedNum = 0;
    tmppos = pool->lastUsedPos;
    while(0 <= tmppos) {
        struct MemBlock *tmpBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase, tmppos);
        if (NULL == tmpBlock) {
            debug_msg("error: fail to get block by pos[%d]", tmppos);
            return -1;
        }
        preUsedNum++;
        tmppos = tmpBlock->preUsedPos;
    }

    if (preUsedNum != nextUesdNum) {
        debug_msg("error: invalid data, preUsedNum[%d], nextUesdNum[%d]", preUsedNum, nextUesdNum);
        return -1;
    }

    int availNum = 0;
    tmppos = pool->firstAvailPos;
    while(0 <= tmppos) {
        struct MemBlock *tmpBlock = (struct MemBlock *)memPoolGetBlockByPos_((char *)poolbase, tmppos);
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

