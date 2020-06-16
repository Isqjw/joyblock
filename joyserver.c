#include "joyserver.h"

#include <arpa/inet.h>
#include <sys/epoll.h>


static struct JoyServer joyServer;

static int joyServerCloseTcp_(struct JoyConnectNode *node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p].", node);
        return -1;
    }

    if (node->cfd == joyServer.lfd) {
        debug_msg("error: invalid close fd[%d]", node->cfd);
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = node->cfd;
    if (0 != epoll_ctl(joyServer.efd, EPOLL_CTL_DEL, node->cfd, &ev)) {
        debug_msg("error: fail to epoll_ctl(EPOLL_CTL_DEL), fd[%d], errno[%s]", node->cfd, strerror(errno));
    }

    if (0 != joynetCloseConnectNode(joyServer.cpool, node)) {
        debug_msg("error: fail to del node, fd[%d].", node->cfd);
        return -1;
    }

    return 0;
}

int joyServerInit(JoyRecvCallBack *cmap, struct JoyBlockConfig conf, int shmkey)
{
    // memset(&joyServer.nid, -1, sizeof(joyServer.nid));
    return joynetInit(&joyServer.cpool, cmap, conf, kEpollMaxFDs, shmkey);
}

int joyServerStopListen()
{
    joynetClose(joyServer.lfd);

    return 0;
}

static int joyServerStopNode_(struct JoyConnectNode *node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p].", node);
        return -1;
    }

    struct JoynetHead stoppkg;
    memset(&stoppkg, 0, sizeof(stoppkg));
    stoppkg.headlen = sizeof(stoppkg);
    stoppkg.msgtype = kJoynetMsgTypeStop;

    struct JoynetRWBuf wbuf;
    memset(&wbuf, 0, sizeof(wbuf));
    wbuf.buf[0] = (char *)&stoppkg;
    wbuf.len[0] = stoppkg.headlen;

    int wlen = joynetWriteSendPkg(node->procid, &wbuf);
    if (0 == wlen) {
        debug_msg("warn: write send pkg len[0]");
        return 0;
    } else if (wlen < 0) {
        debug_msg("error: fail to write send buf.");
        joyServerCloseTcp_(node);
        return -1;
    }

    return 0;
}

int joyServerStop()
{
    joynetTraverseNode(joyServer.cpool, joyServerStopNode_);

    return 0;
}

int joyServerListen(const char *addr, int port)
{
    debug_msg("debug: listen tcp.");
    if (0 != joyServer.lfd) {
        debug_msg("warn: joyServer.lfd[%d] not equal to 0.", joyServer.lfd);
        joynetClose(joyServer.lfd);
        return -1;
    }
    debug_msg("debug: startting listen tcp.");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd) {
        debug_msg("error: fail to create socket fd, errno[%s].", strerror(errno));
        return -1;
    }
    joyServer.lfd = sockfd;

    if (0 != joynetSetNoBlocking(sockfd)) {
        debug_msg("error: fail to set fd no blocking.");
        return -1;
    }

    if (0 != joynetSetAddrReuse(sockfd)) {
        debug_msg("error: fail to set addr reuse.");
        return -1;
    }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = inet_addr(addr);

    if (0 != bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))){
        debug_msg("error: fail to bind socket fd, addr[%s], port[%d], errno[%s].", addr, port, strerror(errno));
        return -1;
    }

    if (0 != listen(sockfd, kListenBacklog)) {
        debug_msg("error: fail to listen socket fd, errno[%s].", strerror(errno));
        return -1;
    }

    joyServer.efd = epoll_create(kEpollMaxFDs);
    if (joyServer.efd < 0) {
        debug_msg("fail to create epoll fd.");
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    if (0 != epoll_ctl(joyServer.efd, EPOLL_CTL_ADD, sockfd, &ev)) {
        debug_msg("error: fail to epoll_ctl(EPOLL_CTL_ADD), sockfd[%d], errno[%s]", sockfd, strerror(errno));
        return -1;
    }

    return 0;
}

int joyServerProcRecvData()
{
    int totallen = 0;
    struct epoll_event events[kEpollMaxFDs];
    int efdcnt = epoll_wait(joyServer.efd, events, kEpollMaxFDs, kJoyServerEpollTimeOut);
    if (efdcnt < 0) {
        if (EINTR == errno) {
            /*
             * EINTR  The call was interrupted by a signal handler before either (1)
             *  any of the requested events occurred or (2) the timeout expired; see signal(7).
             */
            return 0;
        } else {
            /*
             * EBADF  epfd is not a valid file descriptor.
             * EFAULT The memory area pointed to by events is not accessible with write permissions.
             * EINVAL epfd is not an epoll file descriptor, or maxevents is less than or equal to zero.
             */
            debug_msg("error: errno[%s] when epoll.", strerror(errno));
            // joyServerStopListen();
            // joynetTraverseNode(joyServer.cpool, joyServerCloseTcp_);
            return -1;
        }
    } else if(0 == efdcnt) {
        // debug_msg("info: time out epoll.");
        return 0;
    } else {
        for (int i = 0; i < efdcnt; ++i) {
            if(events[i].data.fd == joyServer.lfd) {
                struct sockaddr_in clientaddr;
                memset(&clientaddr, 0, sizeof(clientaddr));
                socklen_t clilen = sizeof(clientaddr);
                int connfd = accept(events[i].data.fd, (struct sockaddr *)&clientaddr, &clilen);
                if (connfd < 0) {
                    debug_msg("error: fail to accept, errno[%s].", strerror(errno));
                    continue;
                }

                debug_msg("debug: accept addr[%s], port[%d], fd[%d].", inet_ntoa(clientaddr.sin_addr),
                        ntohs(clientaddr.sin_port), connfd);

                if (0 != joynetSetNoBlocking(connfd)) {
                    debug_msg("error: fail to set fd no blocking.");
                    joynetClose(connfd);
                    continue;
                }

                if (0 != joynetSetTcpNoDelay(connfd)) {
                    debug_msg("error: fail to set fd no delay.");
                    joynetClose(connfd);
                    continue;
                }

                if (0 != joynetSetTcpKeepAlive(connfd)) {
                    joynetClose(connfd);
                    debug_msg("error: fail to set fd keep alive.");
                    continue;
                }

                struct epoll_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.data.fd = connfd;
                ev.events = EPOLLIN;
                if (0 != epoll_ctl(joyServer.efd, EPOLL_CTL_ADD, connfd, &ev)) {
                    debug_msg("error: fail to epoll_ctl(EPOLL_CTL_ADD), errno[%s].", strerror(errno));
                    joynetClose(connfd);
                    continue;
                }

                joynetSetSendBufSize(connfd, kJoyServerSendBufSize);
                joynetSetRecvBufSize(connfd, kJoyServerRecvBufSize);
                struct JoyConnectNode *node = joynetAllocConnectNode(joyServer.cpool, connfd);
                if (NULL == node) {
                    debug_msg("error: fail to alloc node, fd[%d]", connfd);
                    joynetClose(connfd);
                    continue;
                }

                node->status = kJoynetStatusShakeHand;
            } else if (events[i].events & EPOLLIN) {
                int infd = events[i].data.fd;
                struct JoyConnectNode *node = joynetGetConnectNodeByFD(joyServer.cpool, infd);
                if (NULL == node) {
                    debug_msg("error: fail to get node by fd[%d]", infd);
                    continue;
                }

                int rlen = joynetRecvBuf(node);
                if (rlen < 0) {
                    debug_msg("info: rlen[%d], close fd[%d]", rlen, infd);
                    joyServerCloseTcp_(node);
                    continue;
                }

                totallen += rlen;
            }
        }
    }

    return totallen;
}

static int joyServerHandleShakeMsg_(struct JoyConnectNode *node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p]", node);
        return -1;
    }

    if (kJoynetStatusShakeHand != node->status) {
        return 0;
    }

    time_t curtick;
    time(&curtick);
    if (node->shakelen <= 0) {
        if (node->createtick + kJoynetShakeWaitSecond < curtick) {
            debug_msg("error: shake msg timeout.");
            joyServerCloseTcp_(node);
        }
        return 0;
    }

    struct JoynetHead *pkghead = (struct JoynetHead *)(node->shakebuf);
    if (kJoynetMsgTypeShake != pkghead->msgtype) {
        debug_msg("error: invalid msgtype[%d].", pkghead->msgtype);
        return -1;
    }

    JoyRecvCallBack callback = joynetGetMsgCallBackFunc(joyServer.cpool, kJoynetMsgTypeShake);
    if (NULL != callback) {
        callback(node->shakebuf + pkghead->headlen, pkghead);
    }

    /* debug_msg("debug: get shake pkg, procid[%d]", pkghead->srcid); */
    node->status = kJoynetStatusConnected;
    node->shakelen = 0;
    if (0 != joynetSetNodeProcid(joyServer.cpool, node, pkghead->srcid)) {
        debug_msg("error: fail to set node procid[%d].", pkghead->srcid);
        joyServerCloseTcp_(node);
        return -1;
    }

    struct JoynetHead shakepkg;
    memset(&shakepkg, 0, sizeof(shakepkg));
    shakepkg.msgtype = kJoynetMsgTypeShake;
    shakepkg.headlen = sizeof(shakepkg);
    int slen = joynetSend(node->cfd, (const char *)(&shakepkg),  shakepkg.headlen, 0);
    if (slen < 0) {
        debug_msg("error: fail to send buf.");
        joyServerCloseTcp_(node);
        return -1;
    }

    debug_msg("debug: shake hand success");

    return 0;
}

static int joyServerHandleMsg_(struct JoyConnectNode *node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p]", node);
        return -1;
    }

    // 自己的连接断掉，还是可以把缓存的包发送给其他client
    /* if (kJoynetStatusConnected != node->status) {
        return 0;
    } */

    while (1) {
        struct JoynetRWBuf rbuf;
        memset(&rbuf, 0, sizeof(rbuf));
        int rlen = joynetReadRecvPkg(node, &rbuf);
        if (0 == rlen) {
            break;
        } else if (rlen < 0) {
            debug_msg("error: fail to read buf.");
            joyServerCloseTcp_(node);
            break;
        }

        if (0 == node->procid) {
            debug_msg("error: read data from procid[%d].", node->procid);
            return -1;
        }

        char *buf = rbuf.buf[0];
        if (0 < rbuf.len[1]) {
            buf = (char *)alloca(rbuf.len[0] + rbuf.len[1]);
            if (NULL == buf) {
                debug_msg("error: fail to alloca buf.");
                break;
            }

            memcpy(buf, rbuf.buf[0], rbuf.len[0]);
            memcpy(buf + rbuf.len[0], rbuf.buf[1], rbuf.len[1]);
            // debug_msg("debug: half head. len1[%d], len2[%d]", rbuf.len[0], rbuf.len[1]);
        }

        struct JoynetHead *pkghead = (struct JoynetHead *)buf;
        switch (pkghead->msgtype) {
            case kJoynetMsgTypeMsg: {
                    JoyRecvCallBack callback = joynetGetMsgCallBackFunc(joyServer.cpool, kJoynetMsgTypeMsg);
                    if (NULL != callback) {
                        callback(buf + pkghead->headlen, pkghead);
                    }
                    joynetReleaseRecvBuf(node, &rbuf);
                    break;
                }
            case kJoynetMsgTypeStop: {
                    debug_msg("debug: tcp stop, [%d]", node->cfd);
                    joyServerCloseTcp_(node);
                    break;
                }
            default: {
                    debug_msg("error: invalid msg type[%d].", pkghead->msgtype);
                    joyServerCloseTcp_(node);
                    return -1;
                }
        }
    }

    return 0;
}

int joyServerReadRecvData()
{
    // 处理握手消息
    joynetTraverseNode(joyServer.cpool, joyServerHandleShakeMsg_);

    // 处理业务消息
    joynetTraverseNode(joyServer.cpool, joyServerHandleMsg_);

    return joyServerProcRecvData();
}

static int joyServerNodeSendData_(struct JoyConnectNode *node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p]", node);
        return -1;
    }

    if (kJoynetStatusConnected != node->status) {
        return 0;
    }

    if (joynetSendBuf(node) < 0) {
        debug_msg("error: fail to send buf.");
        joyServerCloseTcp_(node);
        return -1;
    }

    return 0;
}

int joyServerProcSendData()
{
    joynetTraverseNode(joyServer.cpool, joyServerNodeSendData_);

    return 0;
}

int joyServerWriteSendData(const char *buf, int len, int procid, int srcid, int dstid)
{
    if (NULL == buf || len <= 0) {
        debug_msg("error: invalid param, buf[%p], len[%d].", buf, len);
        return -1;
    }

    struct JoynetHead pkghead;
    memset(&pkghead, 0, sizeof(pkghead));
    if (0 != joynetMakePkgHead(&pkghead, buf, len, srcid, dstid, 0)) {
        debug_msg("error: fail to make pkg head.");
        return -1;
    }

    struct JoynetRWBuf wbuf;
    memset(&wbuf, 0, sizeof(wbuf));
    wbuf.buf[0] = (char *)&pkghead;
    wbuf.len[0] = pkghead.headlen;
    wbuf.buf[1] = (char *)buf;
    wbuf.len[1] = pkghead.bodylen;
    int wlen = joynetWriteSendPkg(procid, &wbuf);
    if (wlen <= 0) {
        debug_msg("warn: write send buf, wlen[%d]", wlen);
        return -1;
    }

    joyServerProcSendData();

    return 0;
}

int joyServerGetNodeNum()
{
    return joynetGetNodeNum(joyServer.cpool);
}


#ifdef DEBUG
long int totalrecvlen;
int loselen;
static int serverRecvCallBack(char *buf, struct JoynetHead *pkghead)
{
    if (NULL == buf || NULL == pkghead) {
        debug_msg("error: invalid param, buf[%p], pkghead[%p]", buf, pkghead);
        return -1;
    }

    totalrecvlen += pkghead->bodylen;
    /* debug_msg("recv head, msgtype[%d], headlen[%d], bodylen[%d], srcid[%d], dstid[%d], md5[%d].", \ */
        /* pkghead->msgtype, pkghead->headlen, pkghead->bodylen, pkghead->srcid, pkghead->dstid, pkghead->md5); */
    int rv = joyServerWriteSendData(buf, pkghead->bodylen, 1, pkghead->dstid, pkghead->srcid);
    if (rv < 0) {
        loselen += pkghead->bodylen;
    }

    return 0;
}

int main()
{
    struct JoyBlockConfig cfg = { 128, 100, 15 };
    JoyRecvCallBack cmap[kJoynetMsgTypeMax] = {
        /*kJoynetMsgTypeMsg*/   serverRecvCallBack,
        /*kJoynetMsgTypeShake*/ NULL,
        /*kJoynetMsgTypeStop*/  NULL,
    };
    joyServerInit(cmap, cfg, 1);

    if (0 != joyServerListen("0.0.0.0", 20000)) {
        return -1;
    }

    time_t tick, now;
    time(&tick);
    while (1) {
        joyServerReadRecvData();
        joyServerProcSendData();
        time(&now);
        if (tick + 10 < now) {
            joyBlockMemCheck();
            tick = now;
            debug_msg("total recv len[%ld], lose len[%d]", totalrecvlen, loselen);
        }
    }

    return 0;
}
#endif
