#include <arpa/inet.h>
#include "joyserver.h"


static struct JoyServer joyServer;

static int joyServerCloseTcp_(int fd)
{
    if (0 == fd) {
        debug_msg("error: invalid fd[%d].", fd);
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    if (0 != epoll_ctl(joyServer.efd, EPOLL_CTL_DEL, fd, &ev)) {
        debug_msg("fail to epoll_ctl(EPOLL_CTL_DEL), fd[%d], errno[%s]", fd, strerror(errno));
    }

    close(fd);
    if (fd == joyServer.lfd) {
        bzero(&joyServer, sizeof(joyServer));
    } else {
        if (0 != joynetDelConnectNode(&joyServer.cpool, fd)) {
            debug_msg("error: fail to del node, fd[%d].", fd);
        }
    }
    return 0;
}

int joyServerCloseTcp()
{
    for (int i = 0; i < joyServer.cpool.nodes; ++i) {
        struct JoyConnectNode *node = joyServer.cpool.node + i;
        joyServerCloseTcp_(node->cfd);
    }
    return 0;
}

int joyServerListen(const char *addr, int port)
{
    debug_msg("debug: listen tcp.");
    if (0 != joyServer.lfd) {
        debug_msg("warn: joyServer.lfd[%d] not equal to 0.", joyServer.lfd);
        joyServerCloseTcp_(joyServer.lfd);
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
            return 0;
        } else {
            // close all fd
            joyServerCloseTcp();
            debug_msg("error: errno[%s] when epoll.", strerror(errno));
            return -1;
        }
    } else if(0 == efdcnt) {
        // debug_msg("info: time out epoll.");
        return 0;
    } else {
        for (int i = 0; i < efdcnt; ++i) {
            if(events[i].data.fd == joyServer.lfd) {
                struct sockaddr_in clientaddr = { 0 };
                socklen_t clilen = sizeof(clientaddr);
                int connfd = accept(events[i].data.fd, (struct sockaddr *)&clientaddr, &clilen);
                if (connfd < 0) {
                    debug_msg("error: fail to accept, errno[%s].", strerror(errno));
                    continue;
                }
                debug_msg("debug: accept addr[%s], port[%d], fd[%d].", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), connfd);
                if (0 != joynetSetNoBlocking(connfd)) {
                    debug_msg("error: fail to set fd no blocking.");
                    continue;
                }
                if (0 != joynetSetTcpNoDelay(connfd)) {
                    debug_msg("error: fail to set fd no delay.");
                    continue;
                }
                if (0 != joynetSetTcpKeepAlive(connfd)) {
                    debug_msg("error: fail to set fd keep alive.");
                    continue;
                }
                struct epoll_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.data.fd = connfd;
                ev.events = EPOLLIN;
                if (epoll_ctl(joyServer.efd, EPOLL_CTL_ADD, connfd, &ev)) {
                    debug_msg("fail to epoll_ctl(EPOLL_CTL_ADD), errno[%s].", strerror(errno));
                    joyServerCloseTcp_(connfd);
                    continue;
                }
                joynetSetSendBufSize(connfd, kJoyServerSendBufSize);
                joynetSetRecvBufSize(connfd, kJoyServerRecvBufSize);
                int insertpos = joynetInsertConnectNode(&joyServer.cpool, connfd);
                if (insertpos < 0) {
                    joyServerCloseTcp_(connfd);
                    continue;
                }
                struct JoyConnectNode *node = joyServer.cpool.node + insertpos;
                node->status = kJoynetStatusShakeHand;
            } else if (events[i].events & EPOLLIN) {
                int infd = events[i].data.fd;
                int nodepos = joynetGetConnectNodePosByFD(&joyServer.cpool, infd);
                if (nodepos < 0) {
                    joyServerCloseTcp_(infd);
                    continue;
                }
                struct JoyConnectNode *node = joyServer.cpool.node + nodepos;
                int rlen = joynetRecvBuf(node);
                if (rlen < 0) {
                    joyServerCloseTcp_(infd);
                    continue;
                }
                totallen += rlen;
            }
        }
    }

    return totallen;
}

int joyServerReadRecvData(joyRecvCallBack recvCallBack)
{
    // 处理握手消息
    time_t curtick;
    time(&curtick);
    for (int i = 0; i < joyServer.cpool.nodes; ++i) {
        struct JoyConnectNode *node = joyServer.cpool.node + i;
        if (kJoynetStatusShakeHand != node->status) {
            continue;
        }

        if (node->shakebufpos < sizeof(struct JoynetHead)) {
            if (node->createtick + kJoynetShakeWaitSecond < curtick) {
                debug_msg("error: shake msg timeout.");
                joyServerCloseTcp_(node->cfd);
            }
            continue;
        }

        struct JoynetHead *pkghead = (struct JoynetHead *)node->shakebuf;
        if (kJoynetMsgTypeShake != pkghead->msgtype) {
            debug_msg("error: invalid msgtype[%d].", pkghead->msgtype);
            continue;
        }

        node->procid = pkghead->srcid;
        node->status = kJoynetStatusConnected;
        node->shakebufpos = 0;
        joynetSetNodeIndex(&joyServer.cpool, i, node->procid);

        struct JoynetHead shakepkg;
        memset(&shakepkg, 0, sizeof(shakepkg));
        shakepkg.msgtype = kJoynetMsgTypeShake;
        shakepkg.headlen = sizeof(shakepkg);
        int slen = joynetSend(node->cfd, (const char *)(&shakepkg),  shakepkg.headlen, 0);
        if (slen < 0) {
            debug_msg("error: fail to send buf.");
            joyServerCloseTcp_(node->cfd);
            return -1;
        }
        debug_msg("debug: shake hand success");
    }

    // 处理业务消息
    for (int i = 0; i < joyServer.cpool.nodes; ++i) {
        struct JoyConnectNode *node = joyServer.cpool.node + i;
        if (kJoynetStatusConnected != node->status) {
            continue;
        }

        while (1) {
            struct JoyBlockRWBuf rbuf;
            memset(&rbuf, 0, sizeof(rbuf));
            int rlen = joynetReadRecvPkg(node, &rbuf);
            if (0 == rlen) {
                break;
            } else if (rlen < 0) {
                debug_msg("error: fail to read buf.");
                joyServerCloseTcp_(node->cfd);
                break;
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
                debug_msg("debug: half head. len1[%d], len2[%d]", rbuf.len[0], rbuf.len[1]);
            }

            struct JoynetHead *pkghead = (struct JoynetHead *)buf;
            char error = 0;
            switch (pkghead->msgtype) {
                case kJoynetMsgTypeMsg:
                {
                    recvCallBack(buf + sizeof(pkghead), pkghead);
                    joynetReleaseRecvBuf(node, &rbuf);
                    break;
                }
                default:
                {
                    error = 1;
                    debug_msg("error: invalid msg type[%d].", pkghead->msgtype);
                    joyServerCloseTcp_(node->cfd);
                    break;
                }
            }

            if (0 != error) {
                break;
            }
        }
    }

    return joyServerProcRecvData();
}

int joyServerProcSendData()
{
    for (int i = 0; i < joyServer.cpool.nodes; ++i) {
        struct JoyConnectNode *node = joyServer.cpool.node + i;
        if (kJoynetStatusConnected != node->status) {
            continue;
        }

        if (joynetSendBuf(node) < 0) {
            debug_msg("error: fail to send buf.");
            joyServerCloseTcp_(node->cfd);
            continue;
        }
    }

    return 0;
}

int joyServerWriteSendData(const char *buf, int len, int procid, int srcid, int dstid)
{
    if (NULL == buf || len <= 0) {
        debug_msg("error: invalid param, buf[%p], len[%d].", buf, len);
        return -1;
    }

    int nodepos = joynetGetConnectNodePosByID(&joyServer.cpool, procid);
    if (nodepos < 0) {
        debug_msg("error: fail to get node, procid[%d].", procid);
        return -1;
    }
    struct JoyConnectNode *node = joyServer.cpool.node + nodepos;
    if (kJoynetStatusConnected != node->status) {
        debug_msg("error: send to not connected id[%d].", procid);
        return -1;
    }

    joyServerProcSendData();
    // 再次检查，可能在处理发送过程中发生错误，导致fd已关闭
    if (kJoynetStatusConnected != node->status) {
        debug_msg("error: send to not connected id[%d].", procid);
        return -1;
    }

    struct JoynetHead pkghead;
    memset(&pkghead, 0, sizeof(pkghead));
    if (0 != joynetMakePkgHead(&pkghead, buf, len, srcid, dstid, 0)) {
        debug_msg("error: fail to make pkg head.");
        return -1;
    }

    struct JoyBlockRWBuf wbuf;
    memset(&wbuf, 0, sizeof(wbuf));
    wbuf.buf[0] = (char *)&pkghead;
    wbuf.len[0] = pkghead.headlen;
    wbuf.buf[1] = (char *)buf;
    wbuf.len[1] = pkghead.bodylen;
    int wlen = joynetWriteSendPkg(node, &wbuf);
    if (0 == wlen) {
        debug_msg("warn: write send pkg len[0]");
        return -1;
    } else if (wlen < 0) {
        debug_msg("error: fail to write send buf.");
        joyServerCloseTcp_(node->cfd);
        return -1;
    }

    joyServerProcSendData();

    return 0;
}


long int totalrecvlen;
int loselen;
static int serverRecvCallBack(char *buf, struct JoynetHead *pkghead)
{
    if (NULL == buf || NULL == pkghead) {
        debug_msg("error: invalid param, buf[%p], pkghead[%p]", buf, pkghead);
        return -1;
    }
    totalrecvlen += pkghead->bodylen;
    debug_msg("recv head, msgtype[%d], headlen[%d], bodylen[%d], srcid[%d], dstid[%d], md5[%d].", \
        pkghead->msgtype, pkghead->headlen, pkghead->bodylen, pkghead->srcid, pkghead->dstid, pkghead->md5);
    int rv = joyServerWriteSendData(buf, pkghead->bodylen, pkghead->dstid, pkghead->srcid, 0);
    if (rv < 0) {
        loselen += pkghead->bodylen;
    }
    return 0;
}

int main()
{
    struct JoyBlockConfig cfg = { 1024, 50, 15 };
    joynetInit(&joyServer.cpool, cfg);

    if (0 != joyServerListen("0.0.0.0", 20000)) {
        return -1;
    }
    time_t tick, now;
    time(&tick);
    while (1) {
        joyServerReadRecvData(serverRecvCallBack);
        joyServerProcSendData();
        time(&now);
        if (tick + 10 < now) {
            tick = now;
            debug_msg("total recv len[%ld], lose len[%d]", totalrecvlen, loselen);
            joyBlockListCheck();
        }
    }
    return 0;
}

