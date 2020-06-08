#include "joynet.h"
#include "mempool.h"

int joynetSetNoBlocking(int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        debug_msg("error: fail to fcntl(F_GETFL).");
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        debug_msg("error: fail to fcntl(F_SETFL).");
        return -1;
    }
    return 0;
}

int joynetSetTcpNoDelay(int fd)
{
    int yes = 1;
    if (-1 == setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes))) {
        debug_msg("error: fail to setsockopt(TCP_NODELAY), errno[%s].", strerror(errno));
        return -1;
    }
    return 0;
}

int joynetSetTcpKeepAlive(int fd)
{
    int yes = 1;
    if (-1 == setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes))) {
        debug_msg("error: fail to setsockopt(SO_KEEPALIVE), errno[%s].", strerror(errno));
        return -1;
    }
    return 0;
}

int joynetSetAddrReuse(int fd)
{
    int yes = 1;
    if (-1 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) {
        debug_msg("error: fail to setsockopt(SO_REUSEADDR), errno[%s].", strerror(errno));
        return -1;
    }
    return 0;
}

int joynetSetSendBufSize(int sockfd, unsigned int bufsize)
{
    return setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
}

int joynetSetRecvBufSize(int sockfd, unsigned int bufsize)
{
    return setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
}

int joynetSend(int fd, const char *buf, int len, int to)
{
    if (NULL == buf || len <= 0) {
        debug_msg("invalid param, buf[%p], len[%d].", buf, len);
        return -1;
    }
    int slen = send(fd, buf, len, to);
    if (slen < 0) {
        /*
        ** EAGAIN, EWOULDBLOCK 非阻塞情况下发送缓存已满
        ** send超时为0, 可以不处理EINTR中断错误
        */
        if (EINTR == errno || EAGAIN == errno || EWOULDBLOCK == errno) {
            return 0;
        }
        debug_msg("error: send errno[%s].", strerror(errno));
        return -1;
    } else if (0 == slen) {
        debug_msg("error: connect already closed.");
        return -1;
    }
    return slen;
}

int joynetRecv(int fd, char *buf, int len, int to)
{
    if (NULL == buf || len <= 0) {
        debug_msg("invalid param, buf[%p], len[%d].", buf, len);
        return -1;
    }
    int rlen = recv(fd, buf, len, to);
    if (rlen < 0) {
        /*
        ** 对非阻塞socket而言，EAGAIN不是一种错误。在VxWorks和Windows上，EAGAIN的名字叫做EWOULDBLOCK
        ** 另外，如果出现EINTR即errno为4，错误描述Interrupted system call，操作也应该继续
        ** recv超时为0, 可以不处理EINTR中断错误
        */
        if (EINTR == errno || EAGAIN == errno || EWOULDBLOCK == errno) {
            return 0;
        }
        debug_msg("error: recv errno[%s].", strerror(errno));
        return -1;
    } else if (0 == rlen) {
        debug_msg("error: connect already closed.");
        return -1;
    }
    return rlen;
}

int joynetSendBuf(struct JoyConnectNode* node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p].", node);
        return -1;
    }

    return joyBlockSendData(node->cfd, node->procid);
}

int joynetRecvBuf(struct JoyConnectNode* node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p].", node);
        return -1;
    }

    if (kJoynetStatusShakeHand == node->status) {
        int leftroom = kJoynetShakeBufSize - node->shakebufpos;
        if (leftroom <= 0) {
            return 0;
        }
        int rlen = joynetRecv(node->cfd, node->shakebuf + node->shakebufpos, leftroom, 0);
        if (rlen < 0) {
            debug_msg("error: fail to recv buf.");
            return -1;
        }
        node->shakebufpos += rlen;
    } else {
        return joyBlockRecvData(node->cfd, node->procid);
    }
    return 0;
}

int joynetWriteSendPkg(struct JoyConnectNode *node, struct JoyBlockRWBuf *wbuf)
{
    if (NULL == node || NULL == wbuf) {
        debug_msg("error: invalid param, node[%p], wbuf[%p].", node, wbuf);
        return -1;
    }

    return joyBlockWriteSendPkg(node->procid, wbuf);
}

int joynetReadRecvPkg(struct JoyConnectNode *node, struct JoyBlockRWBuf *rbuf)
{
    if (NULL == node || NULL == rbuf) {
        debug_msg("error: invalid param, node[%p], rbuf[%p].", node, rbuf);
        return -1;
    }

    return joyBlockReadRecvPkg(node->procid, rbuf);
}

int joynetReleaseRecvBuf(struct JoyConnectNode *node, struct JoyBlockRWBuf *rbuf)
{
    if (NULL == node || NULL == rbuf) {
        debug_msg("error: invalid param, node[%p], rbuf[%p].", node, rbuf);
        return -1;
    }

    return joyBlockReleaseRecvBuf(node->procid, rbuf);
}

int joynetMakePkgHead(struct JoynetHead *pkghead, const char *buf, int len, int srcid, int dstid, int dstnid)
{
    if (NULL == pkghead || NULL == buf || len <= 0) {
        debug_msg("error: invalid param, pkghead[%p], buf[%p], len[%d]", pkghead, buf, len);
        return -1;
    }
    pkghead->headlen = sizeof(*pkghead);
    pkghead->bodylen = len;
    pkghead->srcid = srcid;
    pkghead->dstid = dstid;
    pkghead->dstnid = dstnid;
    pkghead->md5 = 0;
    // debug_msg("pkghead, head len[%d], body len[%d]", pkghead->headlen, pkghead->bodylen);
    return 0;
}

struct JoyConnectNode *joynetGetConnectNodeByPos(struct JoyConnectPool *cp, int pos)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].\n", cp);
        return NULL;
    }

    struct JoyConnectNode *node = (struct JoyConnectNode *)memPoolGetBlockByPos(cp, pos);
    if (NULL == node) {
        debug_msg("error: fail to get node, pos[%d]", pos);
        return NULL;
    }

    return node;
}

struct JoyConnectNode *joynetGetConnectNodeByFD(struct JoyConnectPool *cp, int cfd)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].\n", cp);
        return NULL;
    }

    int tmppos = memPoolGetFristUsedPos(cp);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(cp, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return NULL;
        }
        if (cfd == node->cfd) {
            return node;
        }
        tmppos = memPoolGetNextUesdPos(cp, tmppos);
    }

    return NULL;
}

struct JoyConnectNode *joynetGetConnectNodeByID(struct JoyConnectPool *cp, int id)
{
    if (NULL == cp || id < 0 || kJoynetMaxProcID <= id) {
        debug_msg("error: invalid param, cp[%p], id[%d].\n", cp, id);
        return NULL;
    }

    if (cp->nodeidx[id] < 0){
        debug_msg("error: fail to get node, procid[%d]", id);
        return NULL;
    }

    return joynetGetConnectNodeByPos(cp, cp->nodeidx[id]);
}

int joynetReleaseConnectNode(struct JoyConnectPool *cp, int cfd)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].\n", cp);
        return -1;
    }

    struct JoyConnectNode *delnode = NULL;
    int tmppos = memPoolGetFristUsedPos(cp);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(cp, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }
        if (cfd == node->cfd) {
            delnode = node;
            break;
        }
        tmppos = memPoolGetNextUesdPos(cp, tmppos);
    }

    if (NULL == delnode) {
        debug_msg("error: fail to get node, cfd[%d]", cfd);
        return -1;
    }

    memPoolReleaseBlock(cp, tmppos);

    if (0 < delnode->procid) {
        joyBlockReleaseBlockChain(delnode->procid);
    }
    cp->nodeidx[delnode->procid] = -1;

    return 0;
}

struct JoyConnectNode *joynetAllocConnectNode(struct JoyConnectPool *cp, int cfd, int *pos)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].\n", cp);
        return NULL;
    }

    struct JoyConnectNode *tmpnode = joynetGetConnectNodeByFD(cp, cfd);
    if (NULL != tmpnode) {
        debug_msg("error: node already exist.");
        return NULL;;
    }

    int allocpos = memPoolAllocBlock(cp);
    if (allocpos < 0) {
        debug_msg("error: fail to alloc pos.");
        return NULL;
    }
    struct JoyConnectNode *node = joynetGetConnectNodeByPos(cp, allocpos);
    if (NULL == node) {
        debug_msg("error: fail to get node, pos[%d]", allocpos);
        return NULL;
    }

    time_t tick;
    time(&tick);
    bzero(node, sizeof(struct JoyConnectNode));
    node->cfd = cfd;
    node->createtick = tick;
    if (NULL != pos) {
        *pos = allocpos;
    }

    return node;
}

int joynetInit(struct JoyConnectPool **cp, struct JoyBlockConfig conf, int nodeNum)
{
    if (NULL == cp || nodeNum <= 0) {
        debug_msg("error: invalid param, cp[%p], nodeNum[%d]", cp, nodeNum);
        return -1;
    }

    size_t msize = memPoolCalSize(sizeof(struct JoyConnectPool), sizeof(struct JoyConnectNode), nodeNum);
    void *poolbase = malloc(msize);
    if (NULL == poolbase) {
        debug_msg("error: fail to malloc, size[%ld]", msize);
        return -1;
    }
    memPoolInit(poolbase, sizeof(struct JoyConnectPool), sizeof(struct JoyConnectNode), nodeNum);

    *cp = (struct JoyConnectPool *)(memPoolGetHead(poolbase));
    memset((*cp)->nodeidx, -1, sizeof((*cp)->nodeidx));

    return joyBlockInit(conf);
}

int joynetGetNextUsedPos(struct JoyConnectPool *cp, int pos)
{
    if (NULL == cp) {
        debug_msg("error: invalid cp[%p]", cp);
        return -1;
    }

    if (pos < 0) {
        return memPoolGetFristUsedPos(cp);
    }

    return memPoolGetNextUesdPos(cp, pos);
}
