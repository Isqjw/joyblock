#include "joyblock.h"
#include "debug.h"
#include "mempool.h"
//#include "joynet.h"

#include <stdlib.h>
#include <stddef.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>


static struct JoyBlockMem *g_blockmem = NULL;

static struct JoyBlock *joyBlockGetBlockByPos_(int pos)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return NULL;
    }

    return (struct JoyBlock *)memPoolGetBlockByPos(g_blockmem, pos);
}

static int joyBlockAlloc_(struct JoyChainHead *chead)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == chead) {
        debug_msg("error: invalid param, chead[%p]", chead);
        return -1;
    }

    int allocpos = memPoolAllocBlock(g_blockmem);
    if (allocpos < 0) {
        debug_msg("error: fail to alloc block.");
        return -1;
    }

    struct JoyBlock *allocBlock = joyBlockGetBlockByPos_(allocpos);
    if (NULL == allocBlock) {
        debug_msg("error: fail to get block by pos[%d]", allocpos);
        return -1;
    }

    memset(allocBlock, 0, sizeof(*allocBlock));
    allocBlock->nextUsedPos = -1;

    if (-1 == chead->head) {
        chead->head = allocpos;
    } else {
        struct JoyBlock *tailBlock = joyBlockGetBlockByPos_(chead->tail);
        tailBlock->nextUsedPos = allocpos;
    }
    chead->tail = allocpos;
    chead->len += 1;

    return allocpos;
}

static int joyBlockRelease_(struct JoyChainHead *chead, int pos)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == chead || pos < 0 || chead->head < 0) {
        debug_msg("error: invalid param, chead[%p], pos[%d], chain head[%d]", chead, pos, chead->head);
        return -1;
    }

    int curpos = chead->head;
    struct JoyBlock *prevBlock = joyBlockGetBlockByPos_(curpos);
    struct JoyBlock *curBlock = prevBlock;

    while(NULL != curBlock) {
        if (curpos == pos) {
            break;
        }

        curpos = curBlock->nextUsedPos;
        prevBlock = curBlock;
        curBlock = joyBlockGetBlockByPos_(curpos);
    }

    if (NULL == curBlock) {
        debug_msg("error: fail to find block by pos[%d]", pos);
        return -1;
    }

    if (chead->head == pos) {
        if (chead->head == chead->tail) {
            chead->head = -1;
            chead->tail = -1;
        } else {
            chead->head = curBlock->nextUsedPos;
        }
    } else {
        prevBlock->nextUsedPos = curBlock->nextUsedPos;
    }
    chead->len -= 1;

    if (0 != memPoolReleaseBlock(g_blockmem, pos)) {
        debug_msg("error: fail to release block, pos[%d]", pos);
        return -1;
    }

    return 0;
}

static int joyBlockWritePkg_(struct JoyChainHead *chead, struct JoynetRWBuf *wbuf, int chainLenLimit)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == chead || NULL == wbuf || chainLenLimit <= 0) {
        debug_msg("error: invalid param, chead[%p], wbuf[%p], chainLenLimit[%d]", chead, wbuf, chainLenLimit);
        return -1;
    }

    int totallen = wbuf->len[0] + wbuf->len[1];
    if (totallen <= 0 || g_blockmem->cfg.blockSize < totallen) {
        debug_msg("error: invalid data total len[%d], len1[%d], len2[%d]", totallen, wbuf->len[0], wbuf->len[1]);
        return 0;
    }

    if (0 == chead->len) {
        if (1 != memPoolAvailable(g_blockmem)) {
            debug_msg("warn: have not available block to alloc");
            return 0;
        }

        int allocpos = joyBlockAlloc_(chead);
        if (allocpos < 0) {
            debug_msg("error: fail to alloc block.");
            return -1;
        }
    }

    struct JoyBlock *tailBlock = joyBlockGetBlockByPos_(chead->tail);
    if (NULL == tailBlock) {
        debug_msg("erro: fail to get block by pos[%d]", chead->tail);
        return -1;
    }

    int leftroom = g_blockmem->cfg.blockSize - tailBlock->dataTail;
    if (leftroom < totallen) {
        if (chainLenLimit <= chead->len) {
            debug_msg("error: write buffer is full, chain len[%d], chain head[%d]", chead->len, chead->head);
            return 0;
        }

        if (1 != memPoolAvailable(g_blockmem)) {
            debug_msg("warn: write buffer is full, and have not available block, chain head[%d]", chead->head);
            return 0;
        }

        int allocpos = joyBlockAlloc_(chead);
        if (allocpos < 0) {
            debug_msg("error: fail to alloc block.");
            return -1;
        }

        tailBlock = joyBlockGetBlockByPos_(allocpos);
        if (NULL == tailBlock) {
            debug_msg("fail to get block by pos[%d]", allocpos);
            return -1;
        }
    }

    for (int i = 0; i < 2; ++i) {
        memcpy(tailBlock->data + tailBlock->dataTail, wbuf->buf[i], wbuf->len[i]);
        tailBlock->dataTail += wbuf->len[i];
    }

    return totallen;
}

static int joyBlockReadPkg_(int head, struct JoynetRWBuf *rbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == rbuf) {
        debug_msg("error: invalid param, rbuf[%p]", rbuf);
        return -1;
    }

    if (head < 0) {
        return 0;
    }

    struct JoyBlock *headBlock = joyBlockGetBlockByPos_(head);
    if (NULL == headBlock) {
        debug_msg("error: fail to get block by pos[%d]", head);
        return -1;
    }

    struct JoynetHead pkghead;
    int pkgHeadSize = sizeof(pkghead);
    int datalen = headBlock->dataTail - headBlock->dataHead;

    // read pkghead
    if (datalen < pkgHeadSize) {
        int nextpos = headBlock->nextUsedPos;
        if (nextpos < 0) {
            return 0;
        }

        struct JoyBlock *nextBlock = joyBlockGetBlockByPos_(nextpos);
        if (NULL == nextBlock) {
            debug_msg("error: fail to get block by pos[%d]", nextpos);
            return -1;
        }

        if (nextBlock->dataTail < (pkgHeadSize - datalen)) {
            return 0;
        }

        memcpy((&pkghead), headBlock->data + headBlock->dataHead, datalen);
        memcpy((char *)(&pkghead) + datalen, nextBlock->data + nextBlock->dataHead, pkgHeadSize - datalen);
    } else {
        memcpy(&pkghead, headBlock->data + headBlock->dataHead, pkgHeadSize);
    }

    // read pkg
    int pkgsize = pkghead.headlen + pkghead.bodylen;
    if (datalen < pkgsize) {
        int nextpos = headBlock->nextUsedPos;
        if (nextpos < 0) {
            return 0;
        }

        struct JoyBlock *nextBlock = joyBlockGetBlockByPos_(nextpos);
        if (NULL == nextBlock) {
            debug_msg("error: fail to get block by pos[%d]", nextpos);
            return -1;
        }

        if (nextBlock->dataTail < (pkgsize - datalen)) {
            return 0;
        }

        rbuf->buf[0] = headBlock->data + headBlock->dataHead;
        rbuf->len[0] = datalen;
        rbuf->buf[1] = nextBlock->data + nextBlock->dataHead;
        rbuf->len[1] = pkgsize - datalen;
    } else {
        rbuf->buf[0] = headBlock->data + headBlock->dataHead;
        rbuf->len[0] = pkgsize;
    }

    return pkgsize;
}

static int joyBlockReleaseReadBuf_(struct JoyChainHead *chead, struct JoynetRWBuf *rbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == chead || NULL == rbuf) {
        debug_msg("error: invalid param, chain[%p], rbuf[%p]", chead, rbuf);
        return -1;
    }

    if (rbuf->len[0] <= 0) {
        debug_msg("warn: release read buf size[0]");
        return 0;
    }

    for (int i = 0; i < 2; ++i) {
        if (rbuf->len[i] <= 0) {
            continue;
        }

        struct JoyBlock *headBlock = joyBlockGetBlockByPos_(chead->head);
        if (NULL == headBlock) {
            debug_msg("error: fail to get block by pos[%d]", chead->head);
            return -1;
        }

        if (headBlock->dataTail < headBlock->dataHead + rbuf->len[i]) {
            debug_msg("error: impossible block data, data head[%d], data tail[%d]",
                    headBlock->dataHead, headBlock->dataTail);
            return -1;
        }

        headBlock->dataHead += rbuf->len[i];
        if (headBlock->dataHead == headBlock->dataTail) {
            int curhead = chead->head;
            if (0 != joyBlockRelease_(chead, curhead)) {
                debug_msg("error: fail to release block, pos[%d]", curhead);
                return -1;
            }
        }
    }

    return 0;
}

#if 0
static void *joyBlockAttachShm_(int shmkey, size_t msize)
{
    int shmid = shmget(shmkey, msize, 0644);
    if (shmid < 0) {
        debug_msg("warn: shmget fail, shmkey[%d], size[%ld], errno[%s]", shmkey, msize, strerror(errno));
        shmid = shmget(shmkey, msize, IPC_CREAT | 0664);
        if (shmid < 0) {
            debug_msg("warn: shmget create fail, shmkey[%d], size[%ld], errno[%s]", shmkey, msize, strerror(errno));
            return NULL;
        }
    }

    void *poolbase = shmat(shmid, NULL, 0);
    if ((void *)-1 == poolbase) {
        debug_msg("error: shmat fail, shmid[%d], errno[%s]", shmid, strerror(errno));
        return NULL;
    }

    return poolbase;
}
#endif

int joyBlockInit(struct JoyBlockConfig cfg)
{
    if (cfg.blockNum <= 0 || cfg.blockSize <= 0 || cfg.blockChainLen <= 0 ) {
        debug_msg("error: invalid block cfg value, num[%d], size[%d], \
                chainlen[%d], ", cfg.blockNum, cfg.blockSize, cfg.blockChainLen);
        return -1;
    }

    // blockSize小于临时缓存Size将导致比blockSize还大的包永远无法写入block,永远占用tmpBuf
    if (cfg.blockSize <= kJoynetTempBufSize) {
        debug_msg("error: block size[%d] small than tmp buf size[%d].", cfg.blockSize, kJoynetTempBufSize);
        return -1;
    }

    size_t msize = memPoolCalSize(sizeof(struct JoyBlockMem), sizeof(struct JoyBlock) + cfg.blockSize, cfg.blockNum);
    if (msize <= 0) {
        debug_msg("error: invalid pool size[%ld]", msize);
        return -1;
    }

    void *poolbase = malloc(msize);
    /* void *poolbase = joyBlockAttachShm_(cfg.shmkey, msize); */
    if (NULL == poolbase) {
        debug_msg("error: fail to malloc pool.");
        return -1;
    }

    memPoolInit(poolbase, sizeof(struct JoyBlockMem), sizeof(struct JoyBlock) + cfg.blockSize, cfg.blockNum);
    g_blockmem = (struct JoyBlockMem *)memPoolGetHead(poolbase);
    g_blockmem->cfg = cfg;
    g_blockmem->recvhead.head = -1;
    g_blockmem->recvhead.tail = -1;
    g_blockmem->recvhead.len = 0;
    for (int i = 0; i < kJoynetMaxProcID; ++i) {
        struct JoyChainHead *chead = &(g_blockmem->blockChains[i].sendhead);
        chead->head = -1;
        chead->tail = -1;
        chead->len = 0;
    }

    return 0;
}

int joyBlockWriteSendPkg(int procid, struct JoynetRWBuf *wbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid || NULL == wbuf) {
        debug_msg("error: invalid param, procid[%d], wbuf[%p]", procid, wbuf);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blockmem->blockChains + procid;
    int rv = joyBlockWritePkg_(&blockChain->sendhead, wbuf, g_blockmem->cfg.blockChainLen);

    return rv;
}

int joyBlockWriteRecvPkg(struct JoynetRWBuf *wbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if ( NULL == wbuf) {
        debug_msg("error: invalid param, wbuf[%p]", wbuf);
        return -1;
    }

    int rv = joyBlockWritePkg_(&g_blockmem->recvhead, wbuf, g_blockmem->cfg.blockNum);

    return rv;
}

int joyBlockReadRecvPkg(struct JoynetRWBuf *rbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == rbuf) {
        debug_msg("error: invalid param, rbuf[%p]", rbuf);
        return -1;
    }

    int rlen = joyBlockReadPkg_(g_blockmem->recvhead.head, rbuf);

    return rlen;
}

int joyBlockReadSendPkg(int procid, struct JoynetRWBuf *rbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid || NULL == rbuf) {
        debug_msg("error: invalid param, procid[%d], rbuf[%p]", procid, rbuf);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blockmem->blockChains + procid;

    int rlen = joyBlockReadPkg_(blockChain->sendhead.head, rbuf);

    return rlen;
}

int joyBlockReleaseRecvBuf(struct JoynetRWBuf *rbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == rbuf) {
        debug_msg("error: invalid param, rbuf[%p]", rbuf);
        return -1;
    }

    return joyBlockReleaseReadBuf_(&g_blockmem->recvhead, rbuf);
}

int joyBlockReleaseSendBuf(int procid, struct JoynetRWBuf *rbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid || NULL == rbuf) {
        debug_msg("error: invalid param, procid[%d], rbuf[%p]", procid, rbuf);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blockmem->blockChains + procid;

    return joyBlockReleaseReadBuf_(&blockChain->sendhead, rbuf);
}

/* int joyBlockReleaseBlockChain(int procid)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid) {
        debug_msg("error: invalid param, procid[%d]", procid);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blockmem->blockChains + procid;
    int heads[2] = { blockChain->sendhead, g_blockmem->recvhead };
    for (int i = 0; i < 2; ++i) {
        int curpos = heads[i];
        int nextpos = curpos;
        while(0 <= nextpos) {
            struct JoyBlock *tmpblock = joyBlockGetBlockByPos_(nextpos);
            if (NULL == tmpblock) {
                debug_msg("error: fail to get block by pos[%d].", nextpos);
                return -1;
            }
            curpos = nextpos;
            nextpos = tmpblock->nextUsedPos;

            if (0 != joyBlockRelease_(curpos)) {
                debug_msg("error: fail to release block, pos[%d]", curpos);
                return -1;
            }
        }
    }

    memset(blockChain, -1, sizeof(*blockChain));
    g_blockmem->recvhead = -1;

    return 0;
} */

/*
 * 保护逻辑:
 * 验证内存池数据是否正确
 */
int joyBlockMemCheck()
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (0 != memPoolCheck(g_blockmem)) {
        debug_msg("error: fail to check pool data.");
        return -1;
    }

    int usedBlockNum = 0;
    for (int i = 0; i < kJoynetMaxProcID; ++i) {
        struct JoyChainHead *chead = &(g_blockmem->blockChains[i].sendhead);
        if (-1 == chead->head) {
            continue;
        }

        struct JoyBlock *headBlock = joyBlockGetBlockByPos_(chead->head);
        if (NULL == headBlock) {
            debug_msg("error: fail to get block by pos[%d].", chead->head);
            return -1;
        }

        if (headBlock->dataTail <= headBlock->dataHead) {
            debug_msg("error: invalid data, head[%d], tail[%d].", headBlock->dataHead, headBlock->dataTail);
            return -1;
        }

        if (g_blockmem->cfg.blockSize < headBlock->dataTail) {
            debug_msg("error: invalid data, tail[%d], size[%d].", headBlock->dataTail, g_blockmem->cfg.blockSize);
            return -1;
        }

        usedBlockNum += chead->len;
    }
    debug_msg("debug: send num[%d]", usedBlockNum);

    struct JoyChainHead *chead = &g_blockmem->recvhead;
    if (0 <= chead->head) {
        struct JoyBlock *headBlock = joyBlockGetBlockByPos_(chead->head);
        if (NULL == headBlock) {
            debug_msg("error: fail to get block by pos[%d].", chead->head);
            return -1;
        }

        if (headBlock->dataTail <= headBlock->dataHead) {
            debug_msg("error: invalid data, head[%d], tail[%d].", headBlock->dataHead, headBlock->dataTail);
            return -1;
        }

        if (g_blockmem->cfg.blockSize < headBlock->dataTail) {
            debug_msg("error: invalid data, tail[%d], size[%d].", headBlock->dataTail, g_blockmem->cfg.blockSize);
            return -1;
        }

        usedBlockNum += chead->len;
        debug_msg("debug: recv num[%d]", chead->len);
    }

    int memUsedBlockNum = memPoolGetUsedNum(g_blockmem);
    if (memUsedBlockNum != usedBlockNum) {
        debug_msg("error: invalid block used num, [%d], [%d]", usedBlockNum, memUsedBlockNum);
        return -1;
    }

    return 0;
}

// 使用率, 万分比
int joyBlockGetUsage()
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (g_blockmem->cfg.blockNum <= 0) {
        debug_msg("error: invalid block num[%d]", g_blockmem->cfg.blockNum);
        return -1;
    }

    int memUsedBlockNum = memPoolGetUsedNum(g_blockmem);
    return (memUsedBlockNum * 10000 / g_blockmem->cfg.blockNum);
}

int joyBlockGetUsedNum()
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    return  memPoolGetUsedNum(g_blockmem);
}

/* int main()
{
    struct JoyBlockConfig cfg = { 10, 30, 5 };

    joyBlockInit(cfg);
    joyBlockDisplayList_();
    for (int i = 0; i < 5; ++i) {
        int pos = joyBlockAlloc_();
        debug_msg("debug: alloc pos[%d].", pos);
    }

    joyBlockDisplayList_();

    for (int i = 9; i >= 0; i = i - 2) {
        joyBlockRelease_(i);
        debug_msg("debug: release pos[%d].", i);
    }

    joyBlockDisplayList_();

    for (int i = 0; i < 3; ++i) {
        int pos = joyBlockAlloc_();
        debug_msg("debug: alloc pos[%d].", pos);
    }

    joyBlockDisplayList_();

    int procid = 1;
    struct JoynetHead pkg;
    memset(&pkg, 0, sizeof(pkg));
    pkg.headlen = sizeof(pkg);
    joyBlockWriteSendBuf(procid, (char *)&pkg, pkg.headlen);
    pkg.dstnid = 1;
    joyBlockWriteSendBuf(procid, (char *)&pkg, pkg.headlen);


    for (int j = 0; j < 2; j++) {
        struct JoynetRWBuf rbuf;
        joyBlockReadPkg_(0, &rbuf);
        char *buf = (char *)alloca(rbuf.len[0] + rbuf.len[1]);
        if (0 < rbuf.len[0]) {
            memcpy(buf, rbuf.buf[0], rbuf.len[0]);
        }
        if (0 < rbuf.len[1]) {
            memcpy(buf + rbuf.len[0], rbuf.buf[1], rbuf.len[1]);
        }

        struct JoynetHead *pkghead = (struct JoynetHead *)buf;
        debug_msg("recv head, msgtype[%d], headlen[%d], bodylen[%d], srcid[%d], dstid[%d], md5[%d].", \
                pkghead->msgtype, pkghead->headlen, pkghead->bodylen, pkghead->srcid, pkghead->dstid, pkghead->md5);
        joyBlockReleaseReadBuf_(0, &rbuf);
    }


    joyBlockListCheck();

    return 0;
}
 */

