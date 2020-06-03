#include "joyblock.h"
#include "joynet.h"

#include <stddef.h>


static struct JoyBufferBlockList *g_blocklist = NULL;

static int joyBlockGetSize_(struct JoyBlockConfig cfg)
{
    /* int blocksize = offsetof(struct JoyBufferBlock, data) + cfg.blockSize; */
    int blocksize = sizeof(struct JoyBufferBlock) + cfg.blockSize;   //这样计算, 比实际多了一个字节
    int poolsize = offsetof(struct JoyBufferBlockList, blocks) + blocksize * cfg.blockNum;

    return poolsize;
}

static struct JoyBufferBlock *joyBlockGetBlockByPos_(int pos)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return NULL;
    }

    if (pos < 0) {
        debug_msg("error: invalid pos[%d]", pos);
        return NULL;
    }

    char *tmpptr = (char *)g_blocklist->blocks;
    int blocksize = sizeof(struct JoyBufferBlock) + g_blocklist->config.blockSize;
    char *blockptr = (tmpptr + pos * blocksize);

    return (struct JoyBufferBlock *)blockptr;
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

    struct JoyBufferBlock *headBlock = joyBlockGetBlockByPos_(head);
    if (NULL == headBlock) {
        debug_msg("error: fail to get block by pos[%d].", head);
        return -1;
    }
    int curpos = head;
    int nextpos = headBlock->nextUsedPos;

    int count = 1;
    while(0 <= nextpos) {
        struct JoyBufferBlock *tmpblock = joyBlockGetBlockByPos_(nextpos);
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

int joyBlockInit(struct JoyBlockConfig cfg)
{
    if (cfg.blockNum <= 0 || cfg.blockSize <= 0 || cfg.blockChainLen <= 0 ) {
        debug_msg("error: invalid block cfg value, num[%d], size[%d], \
                chainlen[%d], ", cfg.blockNum, cfg.blockSize, cfg.blockChainLen);
        return -1;
    }

    int poolsize = joyBlockGetSize_(cfg);
    if (poolsize <= 0) {
        debug_msg("error: invalid pool size[%d]", poolsize);
        return -1;
    }

    g_blocklist = (struct JoyBufferBlockList *)malloc(poolsize);
    if (NULL == g_blocklist) {
        debug_msg("error: fail to malloc pool.");
        return -1;
    }

    g_blocklist->config = cfg;
    g_blocklist->firstAvailPos = 0;
    for (int pos = 0; pos < g_blocklist->config.blockNum; ++pos) {
        struct JoyBufferBlock *block = joyBlockGetBlockByPos_(pos);

        memset(block, 0, sizeof(*block));
        if (pos < g_blocklist->config.blockNum - 1) {
            block->nextAvailPos = pos + 1;
        } else {
            g_blocklist->lastAvailPos = pos;
            block->nextAvailPos = -1;
        }
    }

    memset(g_blocklist->blockChains, -1, sizeof(g_blocklist->blockChains));

    return 0;
}

static int joyBlockAlloc_()
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (-1 == g_blocklist->firstAvailPos) {
        debug_msg("error: cant to alloc more block.");
        return -1;
    }

    int allocpos = g_blocklist->firstAvailPos;
    struct JoyBufferBlock *firstAvailblock = joyBlockGetBlockByPos_(allocpos);
    if (NULL == firstAvailblock) {
        debug_msg("error: fail to get first avail block by pos[%d].", allocpos);
        return -1;
    }

    if (g_blocklist->firstAvailPos == g_blocklist->lastAvailPos) {
        g_blocklist->firstAvailPos = -1;
        g_blocklist->lastAvailPos = -1;
    } else {
        g_blocklist->firstAvailPos = firstAvailblock->nextAvailPos;
    }

    memset(firstAvailblock, 0, sizeof(*firstAvailblock));
    firstAvailblock->isTake = 1;
    firstAvailblock->nextUsedPos = -1;

    return allocpos;
}

static int joyBlockRelease_(int pos)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (pos < 0 || pos >= g_blocklist->config.blockNum) {
        debug_msg("error: invalid pos[%d].", pos);
        return -1;
    }

    struct JoyBufferBlock *releaseblock = joyBlockGetBlockByPos_(pos);
    if (NULL == releaseblock) {
        debug_msg("error: fail to get release block by pos[%d].", pos);
        return -1;
    }

    if (0 == releaseblock->isTake) {
        debug_msg("error: fail to release block which have not took, pos[%d]", pos);
        return -1;
    }

    int lastpos = g_blocklist->lastAvailPos;
    if (-1 == lastpos) {
        if (-1 != g_blocklist->firstAvailPos) {
            debug_msg("error: impossible first avail pos[%d].", g_blocklist->firstAvailPos);
            return -1;
        }
        g_blocklist->firstAvailPos = pos;
        g_blocklist->lastAvailPos = pos;
    } else {
        struct JoyBufferBlock *lastAvailblock = joyBlockGetBlockByPos_(lastpos);
        if (NULL == lastAvailblock) {
            debug_msg("error: fail to get last avail block by pos[%d].", lastpos);
            return -1;
        }

        lastAvailblock->nextAvailPos = pos;
        g_blocklist->lastAvailPos = pos;
    }

    /* // 申请的时候会清, 这里就不清了 */
    /* memset(releaseblock, 0, sizeof(*releaseblock)); */
    releaseblock->nextAvailPos = -1;

    return 0;
}

static int joyBlockWritePkg_(int *head, struct JoyBlockRWBuf *wbuf)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (NULL == head || NULL == wbuf) {
        debug_msg("error: invalid param, head[%p], wbuf[%p]", head, wbuf);
        return -1;
    }

    int totallen = wbuf->len[0] + wbuf->len[1];
    if (totallen <= 0 || g_blocklist->config.blockSize < totallen) {
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
        if (g_blocklist->firstAvailPos < 0) {
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

    struct JoyBufferBlock *tailBlock = joyBlockGetBlockByPos_(tailpos);
    if (NULL == tailBlock) {
        debug_msg("erro: fail to get block by pos[%d]", tailpos);
        return -1;
    }

    int leftroom = g_blocklist->config.blockSize - tailBlock->dataTail;
    if (leftroom < totallen) {
        if (g_blocklist->config.blockNum <= chainlen) {
            debug_msg("error: write buffer is full, chain len[%d], chain head[%d]", chainlen, *head);
            return -1;
        }

        if (g_blocklist->firstAvailPos < 0) {
            debug_msg("warn: write buffer is full, and have not available block, chain head[%d]", *head);
            return 0;
        }

        int allocpos = joyBlockAlloc_();
        if (allocpos < 0) {
            debug_msg("error: fail to alloc block.");
            return -1;
        }

        struct JoyBufferBlock *newBlock = joyBlockGetBlockByPos_(allocpos);
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

/* static int joyBlockReadBuf_(int *head, char *buf, int len)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (NULL == head || NULL == buf || len <= 0 || g_blocklist->config.blockSize < len) {
        debug_msg("error: invalid param, head[%p], buf[%p], len[%d]", head, buf, len);
        return -1;
    }

    if (*head < 0) {
        debug_msg("debug: have no data to read.");
        return 0;
    }

    struct JoyBufferBlock *headBlock = joyBlockGetBlockByPos_(*head);
    if (NULL == headBlock) {
        debug_msg("error: fail to get block by pos[%d]", *head);
        return -1;
    }

    int datalen = headBlock->dataTail - headBlock->dataHead;
    if (datalen < len) {
        int nextpos = headBlock->nextUsedPos;
        if (nextpos < 0) {
            debug_msg("error: have not enough data to read, head[%d], datalen[%d], readlen[%d]", *head, datalen, len);
            return -1;
        }

        struct JoyBufferBlock *nextBlock = joyBlockGetBlockByPos_(nextpos);
        if (NULL == nextBlock) {
            debug_msg("error: fail to get block by pos[%d]", nextpos);
            return -1;
        }

        if (nextBlock->dataTail < (len - datalen)) {
            debug_msg("error: next block have not enough data to read, pos[%d]", nextpos);
            return -1;
        }

        memcpy(buf, headBlock->data + headBlock->dataHead, datalen);
        int curhead = *head;
        *head = headBlock->nextUsedPos;
        if (0 != joyBlockRelease_(curhead)) {
            debug_msg("error: fail to release block, pos[%d]", curhead);
            return -1;
        }
        memcpy(buf, nextBlock->data + headBlock->dataHead, len - datalen);
        nextBlock->dataHead += len - datalen;
        headBlock = nextBlock;
    } else {
        memcpy(buf, headBlock->data + headBlock->dataHead, len);
        headBlock->dataHead += len;
    }

    if (headBlock->dataHead == headBlock->dataTail) {
        int curhead = *head;
        *head = headBlock->nextUsedPos;
        if (0 != joyBlockRelease_(curhead)) {
            debug_msg("error: fail to release block, pos[%d]", curhead);
            return -1;
        }
    }

    return 0;
} */

static int joyBlockReadPkg_(int head, struct JoyBlockRWBuf *rbuf)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (NULL == rbuf) {
        debug_msg("error: invalid param, rbuf[%p]", rbuf);
        return -1;
    }

    if (head < 0) {
        return 0;
    }

    struct JoyBufferBlock *headBlock = joyBlockGetBlockByPos_(head);
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

        struct JoyBufferBlock *nextBlock = joyBlockGetBlockByPos_(nextpos);
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
    int pkgsize = pkgHeadSize + pkghead.bodylen;
    if (datalen < pkgsize) {
        int nextpos = headBlock->nextUsedPos;
        if (nextpos < 0) {
            return 0;
        }

        struct JoyBufferBlock *nextBlock = joyBlockGetBlockByPos_(nextpos);
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

static int joyBlockReleaseReadBuf_(int *head, struct JoyBlockRWBuf *rbuf)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
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

        struct JoyBufferBlock *headBlock = joyBlockGetBlockByPos_(*head);
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

int joyBlockWriteSendPkg(int procid, struct JoyBlockRWBuf *wbuf)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid || NULL == wbuf) {
        debug_msg("error: invalid param, procid[%d], wbuf[%p]", procid, wbuf);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blocklist->blockChains + procid;
    int rv = joyBlockWritePkg_(&blockChain->sendhead, wbuf);

    return rv;
}

int joyBlockSendData(int fd, int procid)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid) {
        debug_msg("error: invalid param, procid[%d]", procid);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blocklist->blockChains + procid;
    if (blockChain->sendhead < 0) {
        return 0;
    }

    struct JoyBufferBlock *headBlock = joyBlockGetBlockByPos_(blockChain->sendhead);
    if (NULL == headBlock) {
        debug_msg("error: fail to get block by pos[%d]", blockChain->sendhead);
        return -1;
    }

    int datalen = headBlock->dataTail - headBlock->dataHead;
    if (datalen <= 0) {
        debug_msg("error: invalid send data len[%d]", datalen);
        return -1;
    }

    int slen = joynetSend(fd, headBlock->data + headBlock->dataHead, datalen, 0);
    if (slen < 0) {
        debug_msg("error: fail to send buf.");
        return -1;
    }
    headBlock->dataHead += slen;

    if (headBlock->dataHead == headBlock->dataTail) {
        int curhead = blockChain->sendhead;
        blockChain->sendhead = headBlock->nextUsedPos;
        if (0 != joyBlockRelease_(curhead)) {
            debug_msg("error: fail to release block, pos[%d]", curhead);
            return -1;
        }
    }

    return slen;
}

int joyBlockReadRecvPkg(int procid, struct JoyBlockRWBuf *rbuf)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid || NULL == rbuf) {
        debug_msg("error: invalid param, procid[%d], rbuf[%p]", procid, rbuf);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blocklist->blockChains + procid;

    int rlen = joyBlockReadPkg_(blockChain->recvhead, rbuf);

    return rlen;
}

int joyBlockReleaseRecvBuf(int procid, struct JoyBlockRWBuf *rbuf)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid || NULL == rbuf) {
        debug_msg("error: invalid param, procid[%d], rbuf[%p]", procid, rbuf);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blocklist->blockChains + procid;

    return joyBlockReleaseReadBuf_(&blockChain->recvhead, rbuf);
}

int joyBlockRecvData(int fd, int procid)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid) {
        debug_msg("error: invalid param, procid[%d]", procid);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blocklist->blockChains + procid;

    int tailpos;
    int chainlen;
    if (0 != JoyBlockTraverseChain_(blockChain->recvhead, &tailpos, &chainlen)) {
        debug_msg("error: fail to traverse block chain, head[%d]", blockChain->recvhead);
        return -1;
    }

    if (0 == chainlen) {
        if (g_blocklist->firstAvailPos < 0) {
            debug_msg("warn: have not available block to alloc");
            return 0;
        }

        int allocpos = joyBlockAlloc_();
        if (allocpos < 0) {
            debug_msg("error: fail to alloc block.");
            return -1;
        }

        blockChain->recvhead = allocpos;
        tailpos = allocpos;
        chainlen = 1;
    }

    struct JoyBufferBlock *tailBlock = joyBlockGetBlockByPos_(tailpos);
    if (NULL == tailBlock) {
        debug_msg("error: fail to get block by pos[%d]", tailpos);
        return -1;
    }

    int leftroom = g_blocklist->config.blockSize - tailBlock->dataTail;
    if (leftroom <= 0) {
        if (g_blocklist->config.blockNum <= chainlen) {
            debug_msg("warn: recv buffer is full, chain len[%d], chain head[%d]", chainlen, blockChain->recvhead);
            return 0;
        }

        if (g_blocklist->firstAvailPos < 0) {
            debug_msg("warn: recv buffer is full, and have not available block, chain head[%d]", blockChain->recvhead);
            return 0;
        }

        int allocpos = joyBlockAlloc_();
        if (allocpos < 0) {
            debug_msg("error: fail to alloc block.");
            return -1;
        }

        struct JoyBufferBlock *newBlock = joyBlockGetBlockByPos_(allocpos);
        if (NULL == newBlock) {
            debug_msg("fail to get block by pos[%d]", allocpos);
            return -1;
        }

        tailBlock->nextUsedPos = allocpos;
        tailBlock = newBlock;
        tailpos = allocpos;
        chainlen++;
        leftroom = g_blocklist->config.blockSize - tailBlock->dataTail;
    }

    int rlen = joynetRecv(fd, tailBlock->data + tailBlock->dataTail, leftroom, 0);
    if (rlen < 0) {
        debug_msg("error: fail to recv buf.");
        return -1;
    }

    static int totalRecvLen = 0;
    totalRecvLen += rlen;

    tailBlock->dataTail += rlen;

    return rlen;
}

int joyBlockReleaseBlockChain(int procid)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid) {
        debug_msg("error: invalid param, procid[%d]", procid);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blocklist->blockChains + procid;
    int heads[2] = { blockChain->sendhead, blockChain->recvhead };
    for (int i = 0; i < 2; ++i) {
        int curpos = heads[i];
        int nextpos = curpos;
        while(0 <= nextpos) {
            struct JoyBufferBlock *tmpblock = joyBlockGetBlockByPos_(nextpos);
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

    return 0;
}

/* int joyBlockGetSendBufLeftRoom(int procid)
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    if (procid < 0 || kJoynetMaxProcID <= procid) {
        debug_msg("error: invalid param, procid[%d]", procid);
        return -1;
    }

    struct JoyBlockChain *blockChain = g_blocklist->blockChains + procid;

    int tailpos;
    int chainlen;
    if (0 != JoyBlockTraverseChain_(blockChain->sendhead, &tailpos, &chainlen)) {
        debug_msg("error: fail to traverse block chain, head[%d]", blockChain->sendhead);
        return -1;
    }

    if (chainlen < g_blocklist->config.blockNum && 0 <= g_blocklist->firstAvailPos) {
        return g_blocklist->config.blockSize;
    } else {
        if (0 == chainlen) {
            return 0;
        }

        struct JoyBufferBlock *tailBlock = joyBlockGetBlockByPos_(tailpos);
        if (NULL == tailBlock) {
            debug_msg("error: fail to get block by pos[%d]", tailpos);
            return -1;
        }
        return g_blocklist->config.blockSize - tailBlock->dataTail;
    }

    return 0;
} */

/*
 * 保护逻辑:
 * 验证内存池数据是否正确
 */
int joyBlockListCheck()
{
    if (NULL == g_blocklist) {
        debug_msg("error: g_blocklist is NULL.");
        return -1;
    }

    int usedBlockNum = 0;
    for (int i = 0; i < kJoynetMaxProcID; ++i) {
        int twohead[2] = { g_blocklist->blockChains[i].sendhead, g_blocklist->blockChains[i].recvhead };
        for (int j = 0; j < 2; ++j) {
            int headpos = twohead[j];
            if (-1 == headpos) {
                continue;
            }

            struct JoyBufferBlock *headBlock = joyBlockGetBlockByPos_(headpos);
            if (NULL == headBlock) {
                debug_msg("error: fail to get block by pos[%d].", headpos);
                return -1;
            }

            if (headBlock->dataTail <= headBlock->dataHead) {
                debug_msg("error: invalid data, head[%d], tail[%d].", headBlock->dataHead, headBlock->dataTail);
                return -1;
            }

            if (g_blocklist->config.blockSize < headBlock->dataTail) {
                debug_msg("error: invalid data, tail[%d], size[%d].", headBlock->dataTail, g_blocklist->config.blockSize);
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
    }

    int availBlockNum = 0;
    int tmppos = g_blocklist->firstAvailPos;
    while(0 <= tmppos) {
        struct JoyBufferBlock *tmpBlock = joyBlockGetBlockByPos_(tmppos);
        if (NULL == tmpBlock) {
            debug_msg("error: fail to get block by pos[%d].", tmppos);
            return -1;
        }
        availBlockNum++;
        tmppos = tmpBlock->nextAvailPos;
    }

    debug_msg("debug: availBlockNum[%d], usedBlockNum[%d], allblockNum[%d]", availBlockNum, usedBlockNum, 
            g_blocklist->config.blockNum);
    if (g_blocklist->config.blockNum != usedBlockNum + availBlockNum) {
        debug_msg("error: invalid data, used block num[%d], avail block num[%d], total block num[%d]",
                usedBlockNum, availBlockNum, g_blocklist->config.blockNum);
        return -1;
    }

    return 0;
}

static int joyBlockDisplayList_()
{
    debug_msg("=======================================");
    debug_msg("debug: first avail pos[%d].", g_blocklist->firstAvailPos);
    debug_msg("debug: last avail pos[%d].", g_blocklist->lastAvailPos);

    for (int i = 0; i < g_blocklist->config.blockNum; ++i) {
        struct JoyBufferBlock *block = joyBlockGetBlockByPos_(i);
        if (NULL == block) {
            debug_msg("error: fail to get block by pos[%d].", i);
            continue;
        }

        debug_msg("=======================================");
        debug_msg("block->nextAvailPos[%d]", block->nextAvailPos);
        debug_msg("=======================================");
    }

    return 0;
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
        struct JoyBlockRWBuf rbuf;
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
