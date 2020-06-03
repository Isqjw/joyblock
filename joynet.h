#ifndef __JOY_NET_H
#define __JOY_NET_H

#include "debug.h"
#include "joyblock.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <time.h>

#define kShakeBufSize           1024                //握手协议buf大小
#define kShakeWaitSecond        3                   //等待握手协议时间, 时间到了没收到握手协议直接关闭连接
#define kJoynetSendBufSize      10 * 1024 * 1024    //10MB发送缓存
#define kJoynetRecvBufSize      10 * 1024 * 1024    //10MB接受缓存
#define kEpollMaxFDs            1024

#ifdef __cplusplus
extern "C" {
#endif

struct JoyBlockRWBuf;

//网络状态
enum JoynetStatus {
    kJoynetStatusNone           = 1, //无连接
    kJoynetStatusConnecting     = 2, //正在连接
    kJoynetStatusShakeHand      = 3, //握手
    kJoynetStatusConnected      = 4, //已连接
};

enum JoynetMsgType {
    kJoynetMsgTypeMsg           = 0, //消息包
    kJoynetMsgTypeShake         = 1, //握手(注册)包
};

struct JoynetHead {
    int msgtype;
    int headlen;
    int bodylen;
    int srcid;
    int dstid;
    int dstnid;                             // 为了兼容发送时只知道nid不知道procid的情况
    int md5;
};

struct JoyConnectNode {
    int cfd;                                //连接fd
    int procid;                             //进程id
    enum JoynetStatus status;               //连接状态
    int createtick;
    size_t shakebufpos;
    char shakebuf[kShakeBufSize];           //临时缓存握手协议(握手的时候还不知道对端的procid, 无法直接写入block)
};

struct JoyConnectPool {
    int nodes;
    struct JoyConnectNode node[kEpollMaxFDs];
};

typedef int (*joyRecvCallBack)(char *buf, struct JoynetHead *pkghead);

//网络
int joynetSetNoBlocking(int fd);
int joynetSetTcpNoDelay(int fd);
int joynetSetTcpKeepAlive(int fd);
int joynetSetAddrReuse(int fd);
int joynetSetSendBufSize(int sockfd, unsigned int bufsize);
int joynetSetRecvBufSize(int sockfd, unsigned int bufsize);
int joynetSend(int fd, const char *buf, int len, int to);
int joynetSendBuf(struct JoyConnectNode* node);
int joynetWriteSendPkg(struct JoyConnectNode *node, struct JoyBlockRWBuf *wbuf);
int joynetRecv(int fd, char *buf, int len, int to);
int joynetRecvBuf(struct JoyConnectNode* node);
int joynetReadRecvPkg(struct JoyConnectNode *node, struct JoyBlockRWBuf *rbuf);
int joynetReleaseRecvBuf(struct JoyConnectNode *node, struct JoyBlockRWBuf *rbuf);
int joynetMakePkgHead(struct JoynetHead *pkghead, const char *buf, int len, int srcid, int dstid, int dstnid);


//节点
int joynetGetConnectNodePosByFD(struct JoyConnectPool *cp, int cfd);
int joynetGetConnectNodePosByID(struct JoyConnectPool *cp, int id);
int joynetDelConnectNode(struct JoyConnectPool *cp, int cfd);
int joynetInsertConnectNode(struct JoyConnectPool *cp, int cfd);


#ifdef __cplusplus
}
#endif

#endif
