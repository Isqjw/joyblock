#include "joynet.h"

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
        int leftroom = kShakeBufSize - node->shakebufpos;
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

int joynetGetConnectNodePosByFD(struct JoyConnectPool *cp, int cfd)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].\n", cp);
        return -1;
    }
    for (int i = 0; i < cp->nodes; ++i) {
        if (cfd == cp->node[i].cfd) {
            return i;
        }
    }
    return -1;
}

// 待扩展, 支持多个相同id
// TODO chenhu: 当是Server端, 使用zoneid作为下标索引,提高效率O(1)
int joynetGetConnectNodePosByID(struct JoyConnectPool *cp, int id)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].\n", cp);
        return -1;
    }
    for (int i = 0; i < cp->nodes; ++i) {
        if (id == cp->node[i].procid) {
            return i;
        }
    }
    return -1;
}

int joynetDelConnectNode(struct JoyConnectPool *cp, int cfd)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].\n", cp);
        return -1;
    }
    int pos = joynetGetConnectNodePosByFD(cp, cfd);
    if (pos < 0) {
        return -1;
    }

    struct JoyConnectNode *delnode = cp->node + pos;
    if (0 < delnode->procid) {
        joyBlockReleaseBlockChain(delnode->procid);
    }
    bzero(cp->node + pos, sizeof(struct JoyConnectNode));
    if (cp->nodes - 1 == pos) {
        //pass
    } else {
        int movesize = (cp->nodes - pos - 1) * sizeof(struct JoyConnectNode);
        memmove(cp->node + pos, cp->node + pos + 1, movesize);
    }
    cp->nodes--;
    return 0;
}

int joynetInsertConnectNode(struct JoyConnectPool *cp, int cfd)
{
    if (NULL == cp) {
        debug_msg("error: invalid param cp[%p].\n", cp);
        return -1;
    }
    int pos = joynetGetConnectNodePosByFD(cp, cfd);
    if (0 <= pos) {
        debug_msg("error: node already exist.");
        return -1;
    }
    int lastpos = cp->nodes;
    if (kEpollMaxFDs <= lastpos) {
        debug_msg("error: connect pool is full.");
        return -1;
    }

    time_t tick;
    time(&tick);
    bzero(cp->node + lastpos, sizeof(struct JoyConnectNode));
    cp->node[lastpos].cfd = cfd;
    cp->node[lastpos].createtick = tick;
    cp->nodes++;
    return lastpos;
}
