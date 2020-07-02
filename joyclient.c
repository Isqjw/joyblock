#include "joyclient.h"

#include <arpa/inet.h>
#include <poll.h>


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

static int joyClientCloseTcp_(struct JoyConnectNode *node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p]", node);
        return -1;
    }

    if (0 != joynetCloseConnectNode(joyClient.cpool, node)) {
        debug_msg("error: fail to del node, fd[%d].", node->cfd);
        return -1;
    }

    return 0;
}

static int joyClientShakeHand_(struct JoyConnectNode *node, int dstid)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p]", node);
        return -1;
    }

    node->status = kJoynetStatusShakeHand;

    struct JoynetHead shakepkg;
    memset(&shakepkg, 0, sizeof(shakepkg));
    shakepkg.headlen = sizeof(shakepkg);
    shakepkg.srcid = joyClient.cfg.procid;
    shakepkg.dstid = dstid;
    shakepkg.msgtype = kJoynetMsgTypeShake;
    shakepkg.bodylen = joyClient.cfg.buflen;

    char *buf = (char *)alloca(shakepkg.headlen + shakepkg.bodylen);
    if (NULL == buf) {
        debug_msg("error: fail to alloca.");
        return -1;
    }

    memcpy(buf, &shakepkg, shakepkg.headlen);
    memcpy(buf + shakepkg.headlen, joyClient.cfg.initbuf, shakepkg.bodylen);

    int slen = joynetSend(node->cfd, buf, shakepkg.headlen + shakepkg.bodylen, 0);
    if (slen < 0) {
        debug_msg("error: fail to send buf.");
        joyClientCloseTcp_(node);
        return -1;
    }

    return 0;
}

static int joyClientStopNode_(struct JoyConnectNode *node)
{
    if (NULL == node) {
        debug_msg("error: invalid param, node[%p]", node);
        return -1;
    }

    struct JoynetHead stoppkg;
    memset(&stoppkg, 0, sizeof(stoppkg));
    stoppkg.headlen = sizeof(stoppkg);
    stoppkg.srcid = joyClient.cfg.procid;
    stoppkg.dstid = node->procid;
    stoppkg.msgtype = kJoynetMsgTypeStop;

    struct JoynetRWBuf wbuf;
    memset(&wbuf, 0, sizeof(wbuf));
    wbuf.buf[0] = (char *)&stoppkg;
    wbuf.len[0] = stoppkg.headlen;

    int wlen = joynetWriteSendPkg(joyClient.cfg.procid, &wbuf);
    if (0 == wlen) {
        debug_msg("warn: write send pkg len[0]");
        return -1;
    } else if (wlen < 0) {
        debug_msg("error: fail to write send buf.");
        joyClientCloseTcp_(node);
        return -1;
    }

    return 0;
}

int joyClientInit(struct JoyClientConfig clientCfg, struct JoyBlockConfig blockCfg)
{
    if (NULL == clientCfg.cmap || NULL == clientCfg.initbuf
            || clientCfg.buflen < 0 || kJoynetTempBufSize <= clientCfg.buflen
            || clientCfg.procid <= 0 || kJoynetMaxProcID <= clientCfg.procid) {
        debug_msg("error: invalid param, cmap[%p], initbuf[%p], len[%d], procid[%d]",
                clientCfg.cmap, clientCfg.initbuf, clientCfg.buflen, clientCfg.procid);
        return -1;
    }

    char *buf = (char *)malloc(clientCfg.buflen);
    if (NULL == buf) {
        debug_msg("error: fail to malloc.");
        return -1;
    }
    memcpy(buf, clientCfg.initbuf, clientCfg.buflen);

    joyClient.cfg = clientCfg;
    joyClient.cfg.initbuf = buf;

    if (0 != joynetInit(&joyClient.cpool, clientCfg.cmap, blockCfg, clientCfg.nodeNum)) {
        debug_msg("error: fail to init net.");
        return -1;
    }

    return 0;
}

int joyClientStop()
{
    joynetTraverseNode(joyClient.cpool, joyClientStopNode_);
    joyClientProcSendData();
    return 0;
}

int joyClientConnectTcp(const char *addr, int port, int routerid)
{
    time_t tick;
    time(&tick);

    struct JoyConnectNode *node = joynetGetConnectNodeByID(joyClient.cpool, routerid);
    if (NULL != node && (kJoynetStatusConnected == node->status || kJoynetStatusShakeHand == node->status)) {
        return 0;
    } else if (NULL != node && kJoynetStatusConnecting == node->status) {
        // 只能等待一次连接，因为getsockopt第二次调用不会返回正确的状态
        if (0 == joyClientCheckConnectByPoll_(node->cfd)) {
            if (0 != joyClientShakeHand_(node, routerid)) {
                debug_msg("error: fail to set connection to connected.");
                joyClientCloseTcp_(node);
                return -1;
            }
            return 0;
        } else {
            joyClientCloseTcp_(node);
            return -1;
        }
        if (tick <= node->createtick + kJoyClientConnectTimeOut) {
            return 0;
        }
        debug_msg("debug: connect timeout.");
        joyClientCloseTcp_(node);
    }
    debug_msg("debug: startting connect tcp.");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd) {
        debug_msg("error: fail to create socket fd, errno[%s].", strerror(errno));
        return -1;
    }

    if (0 != joynetSetNoBlocking(sockfd)) {
        debug_msg("error: fail to set fd no blocking.");
        joynetClose(sockfd);
        return -1;
    }

    if (0 != joynetSetTcpNoDelay(sockfd)) {
        debug_msg("error: fail to set fd no delay.");
        joynetClose(sockfd);
        return -1;
    }

    if (0 != joynetSetTcpKeepAlive(sockfd)) {
        debug_msg("error: fail to set fd keep alive.");
        joynetClose(sockfd);
        return -1;
    }

    node = joynetAllocConnectNode(joyClient.cpool, sockfd);
    if (NULL == node) {
        debug_msg("error: fail to insert new node, fd[%d].", sockfd);
        joynetClose(sockfd);
        return -1;
    }

    if (0 != joynetSetNodeProcid(joyClient.cpool, node, routerid)) {
        debug_msg("error: fail to set node procid[%d].", routerid);
        joyClientCloseTcp_(node);
        return -1;    }

    debug_msg("debug: insert node, fd[%d].", node->cfd);

    joynetSetSendBufSize(sockfd, kJoyClientSendBufSize);
    joynetSetRecvBufSize(sockfd, kJoyClientRecvBufSize);

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
            if (0 != joyClientShakeHand_(node, routerid)) {
                debug_msg("error: fail to set connection to connected.");
                joyClientCloseTcp_(node);
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
            joyClientCloseTcp_(node);
            return -1;
        }
    } else {
        if (0 != joyClientShakeHand_(node, routerid)) {
            debug_msg("error: fail to set connection to connected.");
            joyClientCloseTcp_(node);
            return -1;
        }
    }

    return 0;
}

int joyClientProcRecvData()
{
    // 处理临时缓存中的数据
    joynetTraverseNode(joyClient.cpool, joynetReadRecvBuf);

    // 处理网络数据
    int totallen = 0;
    int tmppos = joynetGetNextUsedPos(joyClient.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *node = joynetGetConnectNodeByPos(joyClient.cpool, tmppos);
        if (NULL == node) {
            debug_msg("error: fail to get node, pos[%d]", tmppos);
            return -1;
        }
        tmppos = joynetGetNextUsedPos(joyClient.cpool, tmppos);

        // stop 状态表示不能发, 但是还可以收
        if (kJoynetStatusConnected != node->status
                && kJoynetStatusShakeHand != node->status
                && kJoynetStatusStop != node->status) {
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
                joyClientCloseTcp_(node);
                continue;
            }
        } else if(0 == rv) {
            // debug_msg("info: time out poll.");
            continue;
        } else {
            if (pfds[0].events & POLLIN) {
                int rlen = joynetRecvBuf(node);
                if (rlen < 0) {
                    debug_msg("error: recv rlen[%d]", rlen);
                    joyClientCloseTcp_(node);
                    continue;
                }
                totallen += rlen;
            }
        }

    }

    return totallen;
}

static int joyClientHandleShakeMsg_(struct JoyConnectNode *node)
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
            joyClientCloseTcp_(node);
        }
        return 0;
    }

    struct JoynetHead *pkghead = (struct JoynetHead *)node->shakebuf;
    if (kJoynetMsgTypeShake != pkghead->msgtype) {
        debug_msg("error: invalid msgtype[%d].", pkghead->msgtype);
        joyClientCloseTcp_(node);
        return -1;
    }

    node->status = kJoynetStatusConnected;
    node->shakelen = 0;
    debug_msg("debug: shake hands success.");

    return 0;
}

// 一次调用处理一个包
int joyClientReadRecvData()
{
    // 处理握手消息
    joynetTraverseNode(joyClient.cpool, joyClientHandleShakeMsg_);

    struct JoynetRWBuf rbuf;
    memset(&rbuf, 0, sizeof(rbuf));
    int rlen = joynetReadRecvPkg(&rbuf);
    if (0 == rlen) {
        return 0;
    } else if (rlen < 0) {
        debug_msg("error: fail to read buf, rlen[%d].", rlen);
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
    }

    struct JoynetHead *pkghead = (struct JoynetHead *)buf;
    switch (pkghead->msgtype) {
        case kJoynetMsgTypeMsg: {
                JoyRecvCallBack callback = joynetGetMsgCallBackFunc(joyClient.cpool, kJoynetMsgTypeMsg);
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
                struct JoyConnectNode *node = joynetGetConnectNodeByID(joyClient.cpool, pkghead->srcid);
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
                struct JoyConnectNode *node = joynetGetConnectNodeByID(joyClient.cpool, pkghead->srcid);
                if (NULL == node) {
                    debug_msg("error: fail to get node by procid[%d]", pkghead->srcid);
                    break;
                }
                joyClientCloseTcp_(node);
                break;
            }
    }


    joynetReleaseRecvBuf(&rbuf);

    return 1;
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
        /* debug_msg("error: hava not available node."); */
        return NULL;
    }

    // tick当作随机
    time_t curtick;
    time(&curtick);

    return node[(curtick % nodes)];
}

int joyClientProcSendData()
{
    // 先随机挑选一个发送
    int totallen = 0;
    struct JoyConnectNode *snode = joyClientSelectNode_();
    if (NULL == snode) {
        return 0;
    }

    int slen = joynetSendBuf(snode, joyClient.cfg.procid);
    if (slen < 0) {
        debug_msg("error: fail to send buf, fd[%d].", snode->cfd);
        joyClientCloseTcp_(snode);
    } else {
        totallen += slen;
    }

    // 再从剩余节点发送
    int tmppos = joynetGetNextUsedPos(joyClient.cpool, -1);
    while(0 <= tmppos) {
        struct JoyConnectNode *cnode = joynetGetConnectNodeByPos(joyClient.cpool, tmppos);
        if (NULL == cnode) {
            debug_msg("error: fail to get cnode, pos[%d]", tmppos);
            return -1;
        }
        tmppos = joynetGetNextUsedPos(joyClient.cpool, tmppos);

        if (kJoynetStatusConnected != cnode->status) {
            continue;
        }

        if (cnode == snode) {
            continue;
        }

        // TODO
        int slen = joynetSendBuf(cnode, joyClient.cfg.procid);
        if (slen < 0) {
            debug_msg("error: fail to send buf, fd[%d].", cnode->cfd);
            joyClientCloseTcp_(cnode);
            continue;
        }

        totallen += slen;
    }

    return totallen;
}

int joyClientWriteSendData(const char *buf, int len, int srcid, int dstid, int dstnid)
{
    if (NULL == buf || len <= 0) {
        debug_msg("error: invalid param, buf[%p], len[%d].", buf, len);
        return -1;
    }

    struct JoynetHead pkghead;
    memset(&pkghead, 0, sizeof(pkghead));
    if (0 != joynetMakePkgHead(&pkghead, buf, len, srcid, dstid, dstnid)) {
        debug_msg("error: fail to make pkg head.");
        return -1;
    }

    struct JoynetRWBuf wbuf;
    memset(&wbuf, 0, sizeof(wbuf));
    wbuf.buf[0] = (char *)&pkghead;
    wbuf.len[0] = pkghead.headlen;
    wbuf.buf[1] = (char *)buf;
    wbuf.len[1] = pkghead.bodylen;

    int wlen = joynetWriteSendPkg(joyClient.cfg.procid, &wbuf);
    if (0 == wlen) {
        debug_msg("warn: write send pkg len[0]");
        return -1;
    } else if (wlen < 0) {
        debug_msg("error: fail to write send buf.");
        return -1;
    }

    joyClientProcSendData();

    return 0;
}

int joyClientCanStop()
{
    int rlen = joyClientProcRecvData();
    joyClientReadRecvData();
    int slen = joyClientProcSendData();
    size_t rsize = joynetGetTempRecvBufSize(joyClient.cpool);
    size_t ssize = joynetGetTempSendBufSize(joyClient.cpool);
    int usedBlockNum = joynetGetBlockUsedNum();

    debug_msg("debug: rlen[%d], slen[%d], rsize[%ld], ssize[%ld], num[%d]",
            rlen, slen, rsize, ssize, usedBlockNum);
    return (rlen <= 0 && slen <= 0 && rsize <= 0
            && ssize <= 0 && usedBlockNum <= 0);
}

int joyClientGetMemUsage()
{
    return joynetGetBlockUsage();
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
    /* debug_msg("recv head, msgtype[%d], headlen[%d], bodylen[%d], srcid[%d], dstid[%d], md5[%d].", \
        pkghead->msgtype, pkghead->headlen, pkghead->bodylen, pkghead->srcid, pkghead->dstid, pkghead->md5); */
    /*joyClientWriteSendData(buf, pkghead->bodylen, pkghead->dstid, pkghead->srcid, 0);*/
    return 0;
}

int main()
{
    struct JoyBlockConfig blockcfg = { 1024, 1000, 15, 2 };
    JoyRecvCallBack cmap[kJoynetMsgTypeMax] = {
        /*kJoynetMsgTypeMsg*/   clientRecvCallBack,
        /*kJoynetMsgTypeShake*/ NULL,
        /*kJoynetMsgTypeStop*/  NULL,
    };

    int nids[2] = {1, 2};
    struct JoyClientConfig clientcfg;
    clientcfg.cmap = cmap;
    clientcfg.initbuf = (char *)nids;
    clientcfg.buflen = sizeof(nids);
    clientcfg.procid = 1;
    clientcfg.nodeNum = 512;

    joyClientInit(clientcfg, blockcfg);

    time_t tick, now;
    time(&tick);
    const char *test = "1234567890qwertyuioplkjhgfdsazxcvbnm,.;?";
    long int totallen = 0;
    int nodecnt = 1;
    int pkgcnt = 65535;
    const char *ip = "192.168.1.185";
    int port = 20000;

    for (int i = 0; i < nodecnt; ++i) {
        joyClientConnectTcp(ip, port, i + 1);
    }

    while (1) {
        time(&now);
        if (now < tick + 1) {
            continue;
        }
        tick = now;
        char allready = 1;
        for (int i = 0; i < nodecnt; ++i) {
            struct JoyConnectNode *node = joynetGetConnectNodeByID(joyClient.cpool, i + 1);
            if (NULL == node || kJoynetStatusConnected != node->status) {
                joyClientConnectTcp(ip, port, i + 1);
                allready = 0;
                break;
            }
        }

        if (1 == allready) {
            break;
        }

        joyClientProcRecvData();
        joyClientProcSendData();
        joyClientReadRecvData();
    }

    for (int i = 0; i < pkgcnt; ++i) {
        for (int j = 0; j < nodecnt; ++j) {
            int len = rand() % 39 + 1;
            if (0 == joyClientWriteSendData(test, len, j + 1, j + 1, 0)) {
                totallen += len;
            }
            joyClientProcRecvData();
            joyClientReadRecvData();
        }
    }

    while(1){
        joyClientProcRecvData();
        joyClientProcSendData();
        joyClientReadRecvData();
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

