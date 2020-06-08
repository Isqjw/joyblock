#include <arpa/inet.h>
#include "joyclient.h"


static struct JoyClient joyClient;

static int joyClientCheckConnectByPoll_(int sockfd) {
    struct pollfd pfd[1];
    pfd[0].fd = sockfd;
    pfd[0].events = POLLOUT;
    int rv = poll(pfd, 1, kJoyClientPollTimeOut);
    if (rv < 0) {
        return -1;
    } else if (0 == rv) {
        //超时特殊处理
        return 1;
    }

    int err = 0;
    unsigned int len = sizeof(err);
    rv = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
    if (rv < 0 || 0 != err) {
        return -1;
    } else {
        return 0;
    }
}

static int joyClientCloseTcp_(int fd)
{
    close(fd);
    debug_msg("debug: close fd[%d].", fd);
    if (0 != joynetReleaseConnectNode(joyClient.cpool, fd)) {
        debug_msg("error: fail to del node, fd[%d].", fd);
    }

    return 0;
}

static int joyClientShakeHand_(struct JoyConnectNode *node, int procid)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p]", node);
        return -1;
    }

    node->status = kJoynetStatusShakeHand;

    struct JoynetHead shakepkg;
    memset(&shakepkg, 0, sizeof(shakepkg));
    shakepkg.headlen = sizeof(shakepkg);
    shakepkg.srcid = procid;
    shakepkg.msgtype = kJoynetMsgTypeShake;
    shakepkg.bodylen = sizeof(joyClient.nid[0]) * joyClient.nids;

    char *buf = (char *)alloca(shakepkg.headlen + shakepkg.bodylen);
    if (NULL == buf) {
        debug_msg("error: fail to alloca.");
        return -1;
    }

    memcpy(buf, &shakepkg, shakepkg.headlen);
    memcpy(buf + shakepkg.headlen, joyClient.nid, shakepkg.bodylen);

    int slen = joynetSend(node->cfd, buf,  shakepkg.headlen + shakepkg.bodylen, 0);
    if (slen < 0) {
        debug_msg("error: fail to send buf.");
        joyClientCloseTcp_(node->cfd);
        return -1;
    }

    return 0;
}

static int joyClientStop_(struct JoyConnectNode *node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p]", node);
        return -1;
    }

    node->status = kJoynetStatusStop;

    struct JoynetHead stoppkg;
    memset(&stoppkg, 0, sizeof(stoppkg));
    stoppkg.headlen = sizeof(stoppkg);
    stoppkg.srcid = node->procid;
    stoppkg.msgtype = kJoynetMsgTypeStop;

    struct JoyBlockRWBuf wbuf;
    memset(&wbuf, 0, sizeof(wbuf));
    wbuf.buf[0] = (char *)&stoppkg;
    wbuf.len[0] = stoppkg.headlen;

    int wlen = joynetWriteSendPkg(node, &wbuf);
    if (0 == wlen) {
        debug_msg("warn: write send pkg len[0]");
        return -1;
    } else if (wlen < 0) {
        debug_msg("error: fail to write send buf.");
        joyClientCloseTcp_(node->cfd);
        return -1;
    }

    return 0;
}

int joyClientInit(struct JoyBlockConfig conf, int nids, int *nid, int nodeNum)
{
    if (nids <= 0 || NULL == nid) {
        debug_msg("error: invalid param, nids[%d], nid[%p]", nids, nid);
        return -1;
    }

    joyClient.nids = nids;
    memcpy(joyClient.nid, nid, sizeof(nid[0]) * nids);

    return joynetInit(&joyClient.cpool, conf, nodeNum);
}

int joyClientCloseTcp(int routerid)
{
    struct JoyConnectNode *node = joynetGetConnectNodeByID(joyClient.cpool, routerid);
    if (NULL == node) {
        debug_msg("fail to get node by id[%d].", routerid);
        return -1;
    }
    joyClientCloseTcp_(node->cfd);

    return 0;
}

int joyClientTick()
{
    int tmppos = joynetGetNextUsedPos(joyClient.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(joyClient.cpool, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }

        tmppos = joynetGetNextUsedPos(joyClient.cpool, tmppos);
        if (kJoynetStatusStop != node->status) {
            continue;
        }

        joyClientStop_(node);
    }

    return 0;
}

int joyClientConnectTcp(const char *addr, int port, int procid, int routerid)
{
    time_t tick;
    time(&tick);

    struct JoyConnectNode *node = joynetGetConnectNodeByID(joyClient.cpool, routerid);
    if (NULL != node && (kJoynetStatusConnected == node->status || kJoynetStatusShakeHand == node->status)) {
        return 0;
    } else if (NULL != node && kJoynetStatusConnecting == node->status) {
        // 只能等待一次连接，因为getsockopt第二次调用不会返回正确的状态
        if (0 == joyClientCheckConnectByPoll_(node->cfd)) {
            if (0 != joyClientShakeHand_(node, procid)) {
                debug_msg("error: fail to set connection to connected.");
                joyClientCloseTcp_(node->cfd);
                return -1;
            }
            return 0;
        } else {
            joyClientCloseTcp_(node->cfd);
            return -1;
        }
        if (tick <= node->createtick + kJoyClientConnectTimeOut) {
            return 0;
        }
        debug_msg("debug: connect timeout.");
        joyClientCloseTcp_(node->cfd);
    }
    debug_msg("debug: startting connect tcp.");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd) {
        debug_msg("error: fail to create socket fd, errno[%s].", strerror(errno));
        return -1;
    }

    if (0 != joynetSetNoBlocking(sockfd)) {
        debug_msg("error: fail to set fd no blocking.");
        return -1;
    }

    if (0 != joynetSetTcpNoDelay(sockfd)) {
        debug_msg("error: fail to set fd no delay.");
        return -1;
    }

    if (0 != joynetSetTcpKeepAlive(sockfd)) {
        debug_msg("error: fail to set fd keep alive.");
        return -1;
    }

    int allocpos = -1;
    node = joynetAllocConnectNode(joyClient.cpool, sockfd, &allocpos);
    if (NULL == node) {
        debug_msg("error: fail to insert new node, fd[%d].", sockfd);
        return -1;
    }
    joyClient.cpool->nodeidx[routerid] = allocpos;
    debug_msg("debug: insert node, fd[%d].", node->cfd);

    joynetSetSendBufSize(sockfd, kJoyClientSendBufSize);
    joynetSetRecvBufSize(sockfd, kJoyClientRecvBufSize);

    node->procid = routerid;

    struct sockaddr_in servaddr;
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = inet_addr(addr);

    int rv = connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    int connecterr = errno;
    debug_msg("debug: connect tcp, rv[%d], connecterr[%d][%s].", rv, connecterr, strerror(connecterr));
    if (-1 == rv) {
        if (EISCONN == connecterr) {
            //已经连接到该套接字
            if (0 != joyClientShakeHand_(node, procid)) {
                debug_msg("error: fail to set connection to connected.");
                joyClientCloseTcp_(node->cfd);
                return -1;
            }
        } else if (EINPROGRESS == connecterr || EALREADY == connecterr) {
            /*
            ** EINPROGRESS: The socket is nonblocking and the connection cannot be completed immediately. It is possible to select(2) or
            ** poll(2) for completion by selecting the socket for writing.  After select(2) indicates  writability,  use  get‐
            ** sockopt(2)  to  read  the SO_ERROR option at level SOL_SOCKET to determine whether connect() completed success‐
            ** fully (SO_ERROR is zero) or unsuccessfully (SO_ERROR is one of the usual error codes  listed  here,  explaining
            ** the reason for the failure).
            ** EALREADY: The socket is nonblocking and a previous connection attempt has not yet been completed.
            ** 正在进行连接...
            */
            node->status = kJoynetStatusConnecting;
            debug_msg("debug: connect tcp connecterr[%s].", strerror(connecterr));
        } else {
            debug_msg("error: fail to connect addr[%s], port[%d], connecterr[%d][%s].",
                    addr, port, connecterr, strerror(connecterr));
            joyClientCloseTcp_(node->cfd);
            return -1;
        }
    } else {
        if (0 != joyClientShakeHand_(node, procid)) {
            debug_msg("error: fail to set connection to connected.");
            joyClientCloseTcp_(node->cfd);
            return -1;
        }
    }

    return 0;
}

int joyClientIsReady(int routerid)
{
    struct JoyConnectNode *node = joynetGetConnectNodeByID(joyClient.cpool, routerid);
    if (NULL == node) {
        debug_msg("fail to get node by id[%d].", routerid);
        return -1;
    }

    return (node->status == kJoynetStatusConnected ? 1 : 0);
}

int joyClientProcRecvData()
{
    int totallen = 0;
    int tmppos = joynetGetNextUsedPos(joyClient.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(joyClient.cpool, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }
        tmppos = joynetGetNextUsedPos(joyClient.cpool, tmppos);

        if (kJoynetStatusConnected != node->status && kJoynetStatusShakeHand != node->status) {
            continue;
        }
        struct pollfd pfds[1];
        pfds[0].fd = node->cfd;
        pfds[0].events = POLLIN;
        int rv = poll(pfds, 1, kJoyClientPollTimeOut);
        if (rv < 0) {
            if (EINTR == errno) {
                continue;
            } else {
                debug_msg("error: errno[%s] when poll.", strerror(errno));
                joyClientCloseTcp_(node->cfd);
                continue;
            }
        } else if(0 == rv) {
            // debug_msg("info: time out poll.");
            continue;
        } else {
            if (pfds[0].events & POLLIN) {
                int rlen = joynetRecvBuf(node);
                if (rlen < 0) {
                    joyClientCloseTcp_(node->cfd);
                    continue;
                }
                totallen += rlen;
            }
        }
    }

    return totallen;
}

int joyClientReadRecvData(joyRecvCallBack recvCallBack)
{
    // 处理握手消息
    time_t curtick;
    time(&curtick);
    int tmppos = joynetGetNextUsedPos(joyClient.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(joyClient.cpool, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }
        tmppos = joynetGetNextUsedPos(joyClient.cpool, tmppos);

        if (kJoynetStatusShakeHand != node->status) {
            continue;
        }

        if (node->shakebufpos < sizeof(struct JoynetHead)) {
            if (node->createtick + kJoynetShakeWaitSecond < curtick) {
                debug_msg("error: shake msg timeout.");
                joyClientCloseTcp_(node->cfd);
            }
            continue;
        }

        struct JoynetHead *pkghead = (struct JoynetHead *)node->shakebuf;
        if (kJoynetMsgTypeShake != pkghead->msgtype) {
            debug_msg("error: invalid msgtype[%d].", pkghead->msgtype);
            continue;
        }

        node->status = kJoynetStatusConnected;
        node->shakebufpos = 0;
        debug_msg("debug: shake hands success.");
    }

    // 处理业务消息
    tmppos = joynetGetNextUsedPos(joyClient.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(joyClient.cpool, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }
        tmppos = joynetGetNextUsedPos(joyClient.cpool, tmppos);

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
                joyClientCloseTcp_(node->cfd);
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
            }

            struct JoynetHead *pkghead = (struct JoynetHead *)buf;
            char error = 0;
            switch (pkghead->msgtype) {
                case kJoynetMsgTypeMsg:
                {
                    recvCallBack(buf + pkghead->headlen, pkghead);
                    joynetReleaseRecvBuf(node, &rbuf);
                    break;
                }
                case kJoynetMsgTypeStop:
                {
                    joyClientStop_(node);
                    joynetReleaseRecvBuf(node, &rbuf);
                    break;
                }
                default:
                {
                    error = 1;
                    debug_msg("error: invalid msg type[%d].", pkghead->msgtype);
                    joyClientCloseTcp_(node->cfd);
                    break;
                }
            }

            if (0 != error) {
                break;
            }
        }
    }

    return joyClientProcRecvData();
}

int joyClientProcSendData()
{
    int tmppos = joynetGetNextUsedPos(joyClient.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(joyClient.cpool, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }
        tmppos = joynetGetNextUsedPos(joyClient.cpool, tmppos);

        if (kJoynetStatusConnected != node->status) {
            continue;
        }

        if (joynetSendBuf(node) < 0) {
            debug_msg("error: fail to send buf, fd[%d].", node->cfd);
            joyClientCloseTcp_(node->cfd);
            continue;
        }
    }

    return 0;
}

static struct JoyConnectNode *joyClientSelectNode_()
{
    int nodes = 0;
    struct JoyConnectNode *node[kEpollMaxFDs] = { 0 };

    int tmppos = joynetGetNextUsedPos(joyClient.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *cnode = joynetGetConnectNodeByPos(joyClient.cpool, tmppos);
        if (NULL == cnode) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return NULL;
        }
        tmppos = joynetGetNextUsedPos(joyClient.cpool, tmppos);

        if (kJoynetStatusConnected == cnode->status) {
            node[nodes] = cnode;
            nodes++;
        }
    }

    if (nodes <= 0) {
        debug_msg("error: hava not available node.");
        return NULL;
    }

    // tick当作随机
    time_t curtick;
    time(&curtick);

    return node[(curtick % nodes)];
}

int joyClientWriteSendData(const char *buf, int len, int srcid, int dstid)
{
    if (NULL == buf || len <= 0) {
        debug_msg("error: invalid param, buf[%p], len[%d].", buf, len);
        return -1;
    }

    struct JoyConnectNode *node = joyClientSelectNode_();
    if (NULL == node) {
        debug_msg("error: fail to select node.");
        return -1;
    }

    if (kJoynetStatusConnected != node->status) {
        debug_msg("error: send to not connected id[%d].", dstid);
        return -1;
    }

    joyClientProcSendData();
    // 再次检查，可能在处理发送过程中发生错误，导致fd已关闭
    if (kJoynetStatusConnected != node->status) {
        debug_msg("error: send to not connected id[%d].", dstid);
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
        joyClientCloseTcp_(node->cfd);
        return -1;
    }

    joyClientProcSendData();

    return 0;
}

int joyClientSendDataByNid(const char *buf, int len, int srcid, int dstnid)
{
    if (NULL == buf || len <= 0) {
        debug_msg("error: invalid param, buf[%p], len[%d].", buf, len);
        return -1;
    }

    struct JoyConnectNode *node = joyClientSelectNode_();
    if (NULL == node) {
        debug_msg("error: fail to select node.");
        return -1;
    }

    if (kJoynetStatusConnected != node->status) {
        debug_msg("error: send to not connected nid[%d].", dstnid);
        return -1;
    }

    joyClientProcSendData();
    // 再次检查，可能在处理发送过程中发生错误，导致fd已关闭
    if (kJoynetStatusConnected != node->status) {
        debug_msg("error: send to not connected nid[%d].", dstnid);
        return -1;
    }

    struct JoynetHead pkghead;
    memset(&pkghead, 0, sizeof(pkghead));
    if (0 != joynetMakePkgHead(&pkghead, buf, len, srcid, 0, dstnid)) {
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
        joyClientCloseTcp_(node->cfd);
        return -1;
    }

    joyClientProcSendData();

    return 0;
}


#ifdef DEBUG
long int totalrecvlen;
static int clientRecvCallBack(char *buf, struct JoynetHead *pkghead)
{
    if (NULL == buf || NULL == pkghead) {
        debug_msg("error: invalid param, buf[%p], pkghead[%p]", buf, pkghead);
        return -1;
    }
    totalrecvlen += pkghead->bodylen;
    /* debug_msg("recv head, msgtype[%d], headlen[%d], bodylen[%d], srcid[%d], dstid[%d], md5[%d].", \ */
        /* pkghead->msgtype, pkghead->headlen, pkghead->bodylen, pkghead->srcid, pkghead->dstid, pkghead->md5); */
    /*joyClientWriteSendData(buf, pkghead->bodylen, pkghead->dstid, pkghead->srcid, 0);*/
    return 0;
}

int main()
{
    struct JoyBlockConfig cfg = { 1024, 100, 15 };
    joynetInit(&joyClient.cpool, cfg, 512);

    time_t tick, now;
    time(&tick);
    const char *test = "1234567890qwertyuioplkjhgfdsazxcvbnm,.;?";
    long int totallen = 0;
    int nodecnt = 512;
    int pkgcnt = 10;
    const char *ip = "192.168.1.185";
    int port = 20000;
    for (int i = 0; i < nodecnt; ++i) {
        joyClientConnectTcp(ip, port, i + 1, i + 1);
    }

    while (1) {
        time(&now);
        tick = now;
        char allready = 1;
        for (int i = 0; i < nodecnt; ++i) {
            struct JoyConnectNode *node = joynetGetConnectNodeByID(joyClient.cpool, i + 1);
            if (NULL == node || 1 != joyClientIsReady(i + 1)) {
                joyClientConnectTcp(ip, port, i + 1, i + 1);
                allready = 0;
                break;
            }
        }

        if (1 == allready) {
            break;
        }

        joyClientReadRecvData(clientRecvCallBack);
    }

    for (int i = 0; i < pkgcnt; ++i) {
        for (int j = 0; j < nodecnt; ++j) {
            int len = rand() % 39 + 1;
            if (0 == joyClientWriteSendData(test, len, j + 1, j + 1)) {
                totallen += len;
            }
            joyClientReadRecvData(clientRecvCallBack);
        }
    }

    while(1){
        joyClientProcSendData();
        joyClientReadRecvData(clientRecvCallBack);
        time(&now);
        if (tick + 10 < now) {
            tick = now;
            break;
        }
    }

    debug_msg("total send len[%ld], recv len[%ld]", totallen, totalrecvlen);
    while(1) {}

    return 0;
}
#endif

