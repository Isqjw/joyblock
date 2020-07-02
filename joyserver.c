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

    if (node->cfd == joyServer.lnode.cfd) {
        debug_msg("error: invalid close fd[%d]", node->cfd);
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.ptr = node;
    if (0 != epoll_ctl(joyServer.efd, EPOLL_CTL_DEL, node->cfd, &ev)) {
        debug_msg("error: fail to epoll_ctl(EPOLL_CTL_DEL), fd[%d], errno[%s]", node->cfd, strerror(errno));
    }

    if (0 != joynetCloseConnectNode(joyServer.cpool, node)) {
        debug_msg("error: fail to del node, fd[%d].", node->cfd);
        return -1;
    }

    return 0;
}

static int joyServerStopListen_()
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.ptr = &joyServer.lnode;
    if (0 != epoll_ctl(joyServer.efd, EPOLL_CTL_DEL, joyServer.lnode.cfd, &ev)) {
        debug_msg("error: fail to epoll_ctl(EPOLL_CTL_DEL), fd[%d], errno[%s]", 
                joyServer.lnode.cfd, strerror(errno));
        return -1;
    }

    if (0 != joynetClose(joyServer.lnode.cfd)) {
        debug_msg("error: fail to close listen fd[%d]", joyServer.lnode.cfd);
        return -1;
    }


    joyServer.lnode.cfd = 0;

    return 0;
}

// stop包直接写入缓存中, 如果换用共享内存, 这里需要修改
// 因为可能存在重启之后缓存中还存在stop包的情况, 下发后导致对端无法发消息过来
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
    stoppkg.srcid = joyServer.cfg.insid;

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

static int joyServerListen_(const char *addr, int port)
{
    debug_msg("debug: listen tcp.");
    if (0 != joyServer.lnode.cfd) {
        debug_msg("warn: joyServer.lfd[%d] not equal to 0.", joyServer.lnode.cfd);
        joynetClose(joyServer.lnode.cfd);
        return -1;
    }
    debug_msg("debug: startting listen tcp.");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd) {
        debug_msg("error: fail to create socket fd, errno[%s].", strerror(errno));
        return -1;
    }
    joyServer.lnode.cfd = sockfd;

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
    ev.data.ptr = &joyServer.lnode;
    if (0 != epoll_ctl(joyServer.efd, EPOLL_CTL_ADD, sockfd, &ev)) {
        debug_msg("error: fail to epoll_ctl(EPOLL_CTL_ADD), sockfd[%d], errno[%s]", sockfd, strerror(errno));
        return -1;
    }

    return 0;
}

int joyServerInit(struct JoyServerConfig serverCfg, struct JoyBlockConfig blockCfg)
{
    if (NULL == serverCfg.cmap || serverCfg.port <= 0) {
        debug_msg("errror: invalid param, cmap[%p], port[%d]", serverCfg.cmap, serverCfg.port);
        return -1;
    }

    if (0 != joynetInit(&joyServer.cpool, serverCfg.cmap, blockCfg, kEpollMaxFDs)) {
        debug_msg("error: fail to init server net.");
        return -1;
    }

    joyServer.cfg = serverCfg;

    if (0 != joyServerListen_(serverCfg.addr, serverCfg.port)) {
        debug_msg("error: fail to listen, addr[%s], port[%d]", serverCfg.addr, serverCfg.port);
        return -1;
    }

    return 0;
}

int joyServerStop()
{
    if (0 < joyServer.lnode.cfd) {
        joyServerStopListen_();
    }

    joynetTraverseNode(joyServer.cpool, joyServerStopNode_);
    joyServerProcSendData();

    return 0;
}

int joyServerProcRecvData()
{
    // 处理临时缓存中的数据
    joynetTraverseNode(joyServer.cpool, joynetReadRecvBuf);

    // 处理网络数据
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
            struct JoyConnectNode *enode = (struct JoyConnectNode *)events[i].data.ptr;
            if (NULL == enode) {
                debug_msg("error: invalid event node data[%p]", enode);
                continue;
            }

            if(joyServer.lnode.cfd == enode->cfd) {
                struct sockaddr_in clientaddr;
                memset(&clientaddr, 0, sizeof(clientaddr));
                socklen_t clilen = sizeof(clientaddr);
                int connfd = accept(enode->cfd, (struct sockaddr *)&clientaddr, &clilen);
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

                struct JoyConnectNode *node = joynetAllocConnectNode(joyServer.cpool, connfd);
                if (NULL == node) {
                    debug_msg("error: fail to alloc node, fd[%d]", connfd);
                    joynetClose(connfd);
                    continue;
                }

                struct epoll_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.events = EPOLLIN;
                ev.data.ptr = node;
                if (0 != epoll_ctl(joyServer.efd, EPOLL_CTL_ADD, connfd, &ev)) {
                    debug_msg("error: fail to epoll_ctl(EPOLL_CTL_ADD), errno[%s].", strerror(errno));
                    joyServerCloseTcp_(node);
                    continue;
                }
                node->status = kJoynetStatusShakeHand;

                joynetSetSendBufSize(connfd, kJoyServerSendBufSize);
                joynetSetRecvBufSize(connfd, kJoyServerRecvBufSize);
            } else if (events[i].events & EPOLLIN) {
                int rlen = joynetRecvBuf(enode);
                if (rlen < 0) {
                    debug_msg("info: rlen[%d], close fd[%d]", rlen, enode->cfd);
                    joyServerCloseTcp_(enode);
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
        joyServerCloseTcp_(node);
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
    shakepkg.srcid = joyServer.cfg.insid;
    int slen = joynetSend(node->cfd, (const char *)(&shakepkg),  shakepkg.headlen, 0);
    if (slen < 0) {
        debug_msg("error: fail to send buf.");
        joyServerCloseTcp_(node);
        return -1;
    }

    debug_msg("debug: shake hand success");

    return 0;
}

// 一次调用处理一个包
int joyServerReadRecvData()
{
    // 处理握手消息
    joynetTraverseNode(joyServer.cpool, joyServerHandleShakeMsg_);

    struct JoynetRWBuf rbuf;
    memset(&rbuf, 0, sizeof(rbuf));
    int rlen = joynetReadRecvPkg(&rbuf);
    if (0 == rlen) {
        return 0;
    } else if (rlen < 0) {
        debug_msg("error: fail to read buf.");
        return -1;
    }

    char *buf = rbuf.buf[0];
    if (0 < rbuf.len[1]) {
        buf = (char *)alloca(rbuf.len[0] + rbuf.len[1]);
        if (NULL == buf) {
            debug_msg("error: fail to alloca buf.");
            return -1;
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
                    if (0 != callback(buf + pkghead->headlen, pkghead)) {
                        debug_msg("warn: callback fail, srcid[%d], dstid[%d], dstnid[%d], bodylen[%d]",
                                pkghead->srcid, pkghead->dstid, pkghead->dstnid, pkghead->bodylen);
                        joynetWriteRecvPkg(&rbuf);
                    }
                }
                break;
            }
        case kJoynetMsgTypeStop: {
                struct JoyConnectNode *node = joynetGetConnectNodeByID(joyServer.cpool, pkghead->srcid);
                if (NULL == node) {
                    debug_msg("error: fail to get node by procid[%d]", pkghead->srcid);
                    break;
                }
                node->status = kJoynetStatusStop;
                debug_msg("debug: tcp stop, procid[%d]", node->procid);
                break;
            }
        default: {
                debug_msg("error: invalid msg type[%d].", pkghead->msgtype);
                struct JoyConnectNode *node = joynetGetConnectNodeByID(joyServer.cpool, pkghead->srcid);
                if (NULL == node) {
                    debug_msg("error: fail to get node by procid[%d]", pkghead->srcid);
                    break;
                }
                joyServerCloseTcp_(node);
                break;
            }
    }

    joynetReleaseRecvBuf(&rbuf);

    return 1;
}

int joyServerProcSendData()
{
    int totallen = 0;
    int tmppos = joynetGetNextUsedPos(joyServer.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(joyServer.cpool, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }
        tmppos = joynetGetNextUsedPos(joyServer.cpool, tmppos);

        if (kJoynetStatusConnected != node->status) {
            continue;
        }

        int slen = joynetSendBuf(node, node->procid);
        if (slen < 0) {
            debug_msg("error: fail to send buf.");
            joyServerCloseTcp_(node);
            continue;
        }

        totallen += slen;
    }

    return totallen;
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

    // joyServerProcSendData();

    return 0;
}

int joyServerCanStop()
{
    int rlen = joyServerProcRecvData();
    int dealPkgCnt = joyServerReadRecvData();
    int slen = joyServerProcSendData();
    // 处理接收缓存中包的数量
    size_t rsize = joynetGetTempRecvBufSize(joyServer.cpool);
    size_t ssize = joynetGetTempSendBufSize(joyServer.cpool);
    // 接收缓存和发送缓存中的包总数量
    int usedBlockNum = joynetGetBlockUsedNum();
    // 这里确保接受缓存的包全部处理完, 不检查接受缓存和发送缓存中包的总数
    // 这样做可以确保上发的包全部处理完, 而下发的包可能会有丢弃

    debug_msg("debug: rlen[%d], slen[%d], dealPkgCnt[%d], rsize[%ld], ssize[%ld], num[%d]",
            rlen, slen, dealPkgCnt, rsize, ssize, usedBlockNum);
    return (rlen <= 0 && slen <= 0 && rsize <= 0
            && ssize <= 0 && dealPkgCnt <= 0);
}

int joyServerGetMemUsage()
{
    return joynetGetBlockUsage();
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
    int rv = joyServerWriteSendData(buf, pkghead->bodylen, pkghead->srcid, pkghead->dstid, pkghead->srcid);
    if (rv < 0) {
        loselen += pkghead->bodylen;
    }

    return 0;
}

int main()
{
    struct JoyBlockConfig blockCfg = { 1024, 1000, 15, 1 };
    JoyRecvCallBack cmap[kJoynetMsgTypeMax] = {
        /*kJoynetMsgTypeMsg*/   serverRecvCallBack,
        /*kJoynetMsgTypeShake*/ NULL,
        /*kJoynetMsgTypeStop*/  NULL,
    };
    struct JoyServerConfig serverCfg;
    serverCfg.cmap = cmap;
    strcpy(serverCfg.addr, "0.0.0.0");
    serverCfg.port = 20000;
    joyServerInit(serverCfg, blockCfg);

    time_t tick, now;
    time(&tick);
    while (1) {
        joyServerProcRecvData();
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
