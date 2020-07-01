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

static int joyBlockAlloc_()
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
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

    return allocpos;
}

static int joyBlockRelease_(int pos)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (0 != memPoolReleaseBlock(g_blockmem, pos)) {
        debug_msg("error: fail to release block, pos[%d]", pos);
        return -1;
    }

    return 0;
}

static int JoyBlockTraverseChain_(int head, int *tailpos, int *chainlen)
{
    if (NULL == tailpos || NULL == chainlen) {
        debug_msg("error: invalid param, tailpos[%p], chainlen[%p]", tailpos, chainlen);
        return -1;
    }

    if (head < 0) {
        *tailpos = -1;
        *chainlen = 0;
        return 0;
    }

    int curpos = head;
    int nextpos = head;
    int count = 0;

    while(0 <= nextpos) {
        struct JoyBlock *tmpblock = joyBlockGetBlockByPos_(nextpos);
        if (NULL == tmpblock) {
            debug_msg("error: fail to get block by pos[%d].", nextpos);
            return -1;
        }
        count++;
        curpos = nextpos;
        nextpos = tmpblock->nextUsedPos;
    }

    *chainlen = count;
    *tailpos = curpos;

    return 0;
}

static int joyBlockWritePkg_(int *head, struct JoynetRWBuf *wbuf, int chainLenLimit)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == head || NULL == wbuf || chainLenLimit <= 0) {
        debug_msg("error: invalid param, head[%p], wbuf[%p], chainLenLimit[%d]", head, wbuf, chainLenLimit);
        return -1;
    }

    int totallen = wbuf->len[0] + wbuf->len[1];
    if (totallen <= 0 || g_blockmem->cfg.blockSize < totallen) {
        debug_msg("error: invalid data total len[%d], len1[%d], len2[%d]", totallen, wbuf->len[0], wbuf->len[1]);
        return 0;
    }

    int tailpos;
    int chainlen;
    if (0 != JoyBlockTraverseChain_(*head, &tailpos, &chainlen)) {
        debug_msg("error: fail to traverse block chain, head[%d]", *head);
        return -1;
    }

    if (0 == chainlen) {
        if (1 != memPoolAvailable(g_blockmem)) {
            debug_msg("warn: have not available block to alloc");
            return 0;
        }

        int allocpos = joyBlockAlloc_();
        if (allocpos < 0) {
            debug_msg("error: fail to alloc block.");
            return -1;
        }

        *head = allocpos;
        tailpos = allocpos;
        chainlen = 1;
    }

    struct JoyBlock *tailBlock = joyBlockGetBlockByPos_(tailpos);
    if (NULL == tailBlock) {
        debug_msg("erro: fail to get block by pos[%d]", tailpos);
        return -1;
    }

    int leftroom = g_blockmem->cfg.blockSize - tailBlock->dataTail;
    if (leftroom < totallen) {
        if (chainLenLimit <= chainlen) {
            debug_msg("error: write buffer is full, chain len[%d], chain head[%d]", chainlen, *head);
            return 0;
        }

        if (1 != memPoolAvailable(g_blockmem)) {
            debug_msg("warn: write buffer is full, and have not available block, chain head[%d]", *head);
            return 0;
        }

        int allocpos = joyBlockAlloc_();
        if (allocpos < 0) {
            debug_msg("error: fail to alloc block.");
            return -1;
        }

        struct JoyBlock *newBlock = joyBlockGetBlockByPos_(allocpos);
        if (NULL == newBlock) {
            debug_msg("fail to get block by pos[%d]", allocpos);
            return -1;
        }

        tailBlock->nextUsedPos = allocpos;
        tailBlock = newBlock;
        tailpos = allocpos;
        chainlen++;
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

static int joyBlockReleaseReadBuf_(int *head, struct JoynetRWBuf *rbuf)
{
    if (NULL == g_blockmem) {
        debug_msg("error: g_blockmem is NULL.");
        return -1;
    }

    if (NULL == head || NULL == rbuf) {
        debug_msg("error: invalid param, head[%p], rbuf[%p]", head, rbuf);
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

        struct JoyBlock *headBlock = joyBlockGetBlockByPos_(*head);
        if (NULL == headBlock) {
            debug_msg("error: fail to get block by pos[%d]", *head);
            return -1;
        }

        if (headBlock->dataTail < headBlock->dataHead + rbuf->len[i]) {
            debug_msg("error: impossible block data, data head[%d], data tail[%d]",
                    headBlock->dataHead, headBlock->dataTail);
            return -1;
        }

        headBlock->dataHead += rbuf->len[i];
        if (headBlock->dataHead == headBlock->dataTail) {
            int curhead = *head;
            *head = headBlock->nextUsedPos;
            if (0 != joyBlockRelease_(curhead)) {
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
    g_blockmem->recvhead = -1;
    memset(g_blockmem->blockChains, -1, sizeof(g_blockmem->blockChains));

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

    int rlen = joyBlockReadPkg_(g_blockmem->recvhead, rbuf);

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

    int rlen = joyBlockReadPkg_(blockChain->sendhead, rbuf);

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
        int headpos = g_blockmem->blockChains[i].sendhead;
        if (-1 == headpos) {
            continue;
        }

        struct JoyBlock *headBlock = joyBlockGetBlockByPos_(headpos);
        if (NULL == headBlock) {
            debug_msg("error: fail to get block by pos[%d].", headpos);
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

        int tailpos;
        int chainlen;
        if (0 != JoyBlockTraverseChain_(headpos, &tailpos, &chainlen)) {
            debug_msg("error: fail to traverse block chain, headpos[%d]", headpos);
            return -1;
        }
        usedBlockNum += chainlen;
    }
    debug_msg("debug: send num[%d]", usedBlockNum);

    int headpos = g_blockmem->recvhead;
    if (0 <= headpos) {
        struct JoyBlock *headBlock = joyBlockGetBlockByPos_(headpos);
        if (NULL == headBlock) {
            debug_msg("error: fail to get block by pos[%d].", headpos);
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

        int tailpos;
        int chainlen;
        if (0 != JoyBlockTraverseChain_(headpos, &tailpos, &chainlen)) {
            debug_msg("error: fail to traverse block chain, headpos[%d]", headpos);
            return -1;
        }
        usedBlockNum += chainlen;
        debug_msg("debug: recv num[%d]", chainlen);
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

