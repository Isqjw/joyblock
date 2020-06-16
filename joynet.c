#include "joynet.h"

int joynetClose(int fd)
{
    if (0 != close(fd)) {
        debug_msg("error: fail to close fd[%d], errno[%s]", fd, strerror(errno));
        return -1;
    }
    debug_msg("debug: close fd[%d].", fd);

    return 0;
}

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

    while(1) {
        int leftroom = kMaxProtoPackBufSize - node->sendlen;
        if (leftroom <= 0) {
            break;
        }

        struct JoynetRWBuf rbuf;
        memset(&rbuf, 0, sizeof(rbuf));
        int rlen = joyBlockReadSendPkg(node->procid, &rbuf);
        if (rlen < 0) {
            debug_msg("error: fail to read send pkg, procid[%d]", node->procid);
            return -1;
        } else {
            if (leftroom <= rlen || 0 == rlen) {
                break;
            }
        }

        for (int i = 0; i < 2; ++i) {
            memcpy(node->sendbuf + node->sendlen, rbuf.buf[i], rbuf.len[i]);
            node->sendlen += rbuf.len[i];
        }

        joyBlockReleaseSendBuf(node->procid, &rbuf);
    }


    if (0 < node->sendlen) {
        int slen = joynetSend(node->cfd, node->sendbuf, node->sendlen, 0);
        if (slen < 0) {
            debug_msg("error: fail to send buf, fd[%d]", node->cfd);
            return -1;
        }

        if (slen != node->sendlen) {
            memmove(node->sendbuf, node->sendbuf + slen, node->sendlen - slen);
        }
        node->sendlen -= slen;

        return slen;
    }

    return 0;
    /* return joyBlockSendData(node->cfd, node->procid); */
}

int joynetRecvBuf(struct JoyConnectNode* node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p].", node);
        return -1;
    }

    int leftroom = kMaxProtoPackBufSize - node->recvlen;
    if (leftroom <= 0) {
        return 0;
    }

    int rlen = joynetRecv(node->cfd, node->recvbuf + node->recvlen, leftroom, 0);
    if (rlen < 0) {
        debug_msg("error: fail to recv buf.");
        return -1;
    }
    node->recvlen += rlen;

    int curpos = 0;
    int pkgHeadSize = sizeof(struct JoynetHead);
    while (pkgHeadSize <= node->recvlen) {
        struct JoynetHead *pkghead = (struct JoynetHead *)(node->recvbuf + curpos);
        int pkglen = pkghead->headlen + pkghead->bodylen;
        if (node->recvlen < pkglen) {
            break;
        }

        if (kJoynetStatusShakeHand == node->status) {
            if (kJoynetMsgTypeShake != pkghead->msgtype) {
                debug_msg("error: invalid msg type[%d], node status[%d]", pkghead->msgtype, node->status);
                return -1;
            }

            if (0 != node->shakelen) {
                debug_msg("error: shake hand buf not empty, len[%d]", node->shakelen);
                return -1;
            }

            memcpy(node->shakebuf, node->recvbuf + curpos, pkglen);
            node->shakelen = pkglen;
        } else {
            struct JoynetRWBuf wbuf;
            memset(&wbuf, 0, sizeof(wbuf));
            wbuf.buf[0] = node->recvbuf + curpos;
            wbuf.len[0] = pkglen;

            if (pkglen != joyBlockWriteRecvPkg(node->procid, &wbuf)) {
                debug_msg("warn: fail to write recv pkg.");
                break;
            }
        }

        node->recvlen -= pkglen;
        curpos += pkglen;
    }

    if (0 < curpos) {
        memmove(node->recvbuf + curpos, node->recvbuf, node->recvlen);
    }

    return rlen;

    /* if (kJoynetStatusShakeHand == node->status) {
       int leftroom = kJoynetShakeBufSize - node->shakelen;
       if (leftroom <= 0) {
       return 0;
       }
       int rlen = joynetRecv(node->cfd, node->shakebuf + node->shakelen, leftroom, 0);
       if (rlen < 0) {
       debug_msg("error: fail to recv buf.");
       return -1;
       }
       node->shakelen += rlen;
       } else {
       return joyBlockRecvData(node->cfd, node->procid);
       }

       reurn 0;
       */
}

int joynetWriteSendPkg(int procid, struct JoynetRWBuf *wbuf)
{
    if (procid < 0 || kJoynetMaxProcID <= procid || NULL == wbuf) {
        debug_msg("error: invalid param, procid[%d], wbuf[%p].", procid, wbuf);
        return -1;
    }

    return joyBlockWriteSendPkg(procid, wbuf);
}

int joynetReadRecvPkg(struct JoyConnectNode *node, struct JoynetRWBuf *rbuf)
{
    if (NULL == node || NULL == rbuf) {
        debug_msg("error: invalid param, node[%p], rbuf[%p].", node, rbuf);
        return -1;
    }

    return joyBlockReadRecvPkg(node->procid, rbuf);
}

int joynetReleaseRecvBuf(struct JoyConnectNode *node, struct JoynetRWBuf *rbuf)
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

    return 0;
}

struct JoyConnectNode *joynetGetConnectNodeByPos(struct JoyConnectPool *cp, int pos)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].", cp);
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
        debug_msg("error: invalid param cp[%p].", cp);
        return NULL;
    }

    int tmppos = memPoolGetFirstUsedPos(cp);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(cp, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return NULL;
        }
        if (cfd == node->cfd) {
            return node;
        }
        tmppos = memPoolGetNextUsedPos(cp, tmppos);
    }

    return NULL;
}

struct JoyConnectNode *joynetGetConnectNodeByID(struct JoyConnectPool *cp, int id)
{
    if (NULL == cp || id < 0 || kJoynetMaxProcID <= id) {
        debug_msg("error: invalid param, cp[%p], id[%d].", cp, id);
        return NULL;
    }

    if (cp->nodeidx[id] < 0){
        // debug_msg("error: fail to get node, procid[%d]", id);
        return NULL;
    }

    return joynetGetConnectNodeByPos(cp, cp->nodeidx[id]);
}

int joynetCloseConnectNode(struct JoyConnectPool *cp, struct JoyConnectNode *node)
{
    if (NULL == cp || NULL == node) {
        debug_msg("error: invalid param, cp[%p], node[%p].", cp, node);
        return -1;
    }

    if (0 < node->cfd) {
        joynetClose(node->cfd);
    }

    if (NULL != node->shakebuf) {
        free(node->shakebuf);
        node->shakebuf = NULL;
    }

    if (NULL != node->recvbuf) {
        free(node->recvbuf);
        node->recvbuf = NULL;
    }
    node->recvlen = 0;

    if (NULL != node->sendbuf) {
        free(node->sendbuf);
        node->sendbuf = NULL;
    }
    node->sendlen = 0;

    node->cfd = 0;
    node->status = kJoynetStatusClosed;

    if (0 == joyBlockHaveData(node->procid)) {
        memPoolReleaseBlock(cp, node->pos);
        cp->nodeidx[node->procid] = -1;
    }

    return 0;
}

struct JoyConnectNode *joynetAllocConnectNode(struct JoyConnectPool *cp, int cfd)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].", cp);
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
    node->pos = allocpos;
    node->cfd = cfd;
    node->createtick = tick;

    node->shakebuf = (char *)malloc(kJoynetShakeBufSize);
    if (NULL == node->shakebuf) {
        debug_msg("error: fail to malloc shake buf.");
        return NULL;
    }

    node->sendbuf = (char *)malloc(kMaxProtoPackBufSize);
    if (NULL == node->sendbuf) {
        debug_msg("error: fail to malloc send buf.");
        return NULL;
    }

    node->recvbuf = (char *)malloc(kMaxProtoPackBufSize);
    if (NULL == node->recvbuf) {
        debug_msg("error: fail to malloc recv buf.");
        return NULL;
    }

    return node;
}

int joynetInit(struct JoyConnectPool **cp, JoyRecvCallBack *cmap, struct JoyBlockConfig conf, int nodeNum, int shmkey)
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
    memcpy((*cp)->cmap, cmap, sizeof((*cp)->cmap));

    return joyBlockInit(conf, shmkey);
}

int joynetSetNodeProcid(struct JoyConnectPool *cp, struct JoyConnectNode *node, int procid)
{
    if (NULL == cp || NULL == node || procid < 0 || kJoynetMaxProcID <= procid) {
        debug_msg("error: invalid param, cp[%p], node[%p], procid[%d].", cp, node, procid);
        return -1;
    }

    struct JoyConnectNode *oldNode = joynetGetConnectNodeByID(cp, procid);
    if (NULL == oldNode) {
        node->procid = procid;
        cp->nodeidx[procid] = node->pos;
        return 0;
    }

    if (kJoynetStatusStop != oldNode->status) {
        debug_msg("error: same procid node already exist, procid[%d]", procid);
        return -1;
    }

    node->procid = procid;
    cp->nodeidx[procid] = node->pos;
    memPoolReleaseBlock(cp, node->pos);

    return 0;
}

int joynetGetNextUsedPos(struct JoyConnectPool *cp, int pos)
{
    if (NULL == cp) {
        debug_msg("error: invalid cp[%p]", cp);
        return -1;
    }

    if (pos < 0) {
        return memPoolGetFirstUsedPos(cp);
    }

    return memPoolGetNextUsedPos(cp, pos);
}

int joynetGetNodeNum(struct JoyConnectPool *cp)
{
    if (NULL == cp) {
        debug_msg("error: invalid cp[%p]", cp);
        return -1;
    }

    return memPoolGetUsedNum(cp);
}

int joynetTraverseNode(struct JoyConnectPool *cp, JoyNodeCallBack callback)
{
    int tmppos = joynetGetNextUsedPos(cp, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(cp, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }
        tmppos = joynetGetNextUsedPos(cp, tmppos);
        callback(node);
    }

    return 0;
}

JoyRecvCallBack joynetGetMsgCallBackFunc(struct JoyConnectPool *cp, enum JoynetMsgType type)
{
    if (NULL == cp) {
        debug_msg("error: invalid cp[%p]", cp);
        return NULL;
    }

    if (type < kJoynetMsgTypeMsg || kJoynetMsgTypeMax <= type) {
        debug_msg("error: invalid msg type[%d]", type);
        return NULL;
    }

    return cp->cmap[type];
}
