#ifndef __JOY_NET_H
#define __JOY_NET_H

#include "joyconst.h"
#include "debug.h"
#include "mempool.h"
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



struct JoynetRWBuf;
struct JoyBlockConfig;

//网络状态
enum JoynetStatus {
    kJoynetStatusNone           = 1, //无连接
    kJoynetStatusConnecting     = 2, //正在连接
    kJoynetStatusShakeHand      = 3, //握手
    kJoynetStatusConnected      = 4, //已连接
    kJoynetStatusStop           = 5, //已停止
    kJoynetStatusClosed         = 6, //fd已关闭
};

enum JoynetMsgType {
    kJoynetMsgTypeMsg           = 0, //消息包
    kJoynetMsgTypeShake         = 1, //握手(注册)包
    kJoynetMsgTypeStop          = 2, //停服
    kJoynetMsgTypeMax           = 3,
};

struct JoyConnectNode {
    int pos;                                //池子中的位置
    int cfd;                                //连接fd
    int procid;                             //对端进程id
    enum JoynetStatus status;               //连接状态
    int createtick;

    int shakelen;
    char *shakebuf;     //临时缓存握手协议(握手的时候还不知道对端的procid, 无法直接写入block)

    int recvlen;
    char *recvbuf;      //临时接收缓存
    int sendlen;
    char *sendbuf;      //临时发送缓存
};

typedef int (*JoyNodeCallBack)(struct JoyConnectNode *node);
typedef int (*JoyRecvCallBack)(char *buf, struct JoynetHead *pkghead);

struct JoyConnectPool {
    JoyRecvCallBack cmap[kJoynetMsgTypeMax];
    int nodeidx[kJoynetMaxProcID];          //procid(zoneid)作为下标, 索引node
};


#ifdef __cplusplus
extern "C" {
#endif

//网络
int joynetClose(int fd);
int joynetSetNoBlocking(int fd);
int joynetSetTcpNoDelay(int fd);
int joynetSetTcpKeepAlive(int fd);
int joynetSetAddrReuse(int fd);
int joynetSetSendBufSize(int sockfd, unsigned int bufsize);
int joynetSetRecvBufSize(int sockfd, unsigned int bufsize);

int joynetSend(int fd, const char *buf, int len, int to);
int joynetSendBuf(struct JoyConnectNode* node);
int joynetWriteSendPkg(int procid, struct JoynetRWBuf *wbuf);

int joynetRecv(int fd, char *buf, int len, int to);
int joynetRecvBuf(struct JoyConnectNode* node);
int joynetReadRecvPkg(struct JoyConnectNode *node, struct JoynetRWBuf *rbuf);
int joynetReleaseRecvBuf(struct JoyConnectNode *node, struct JoynetRWBuf *rbuf);

int joynetMakePkgHead(struct JoynetHead *pkghead, const char *buf, int len, int srcid, int dstid, int dstnid);


//节点
int joynetInit(struct JoyConnectPool **cp, JoyRecvCallBack *cmap, struct JoyBlockConfig conf, int nodeNum, int shmkey);
int joynetSetNodeProcid(struct JoyConnectPool *cp, struct JoyConnectNode *node, int procid);
struct JoyConnectNode *joynetGetConnectNodeByPos(struct JoyConnectPool *cp, int pos);
struct JoyConnectNode *joynetGetConnectNodeByFD(struct JoyConnectPool *cp, int cfd);
struct JoyConnectNode *joynetGetConnectNodeByID(struct JoyConnectPool *cp, int id);

int joynetCloseConnectNode(struct JoyConnectPool *cp, struct JoyConnectNode *node);

struct JoyConnectNode *joynetAllocConnectNode(struct JoyConnectPool *cp, int cfd);
int joynetGetNextUsedPos(struct JoyConnectPool *cp, int pos);
int joynetGetNodeNum(struct JoyConnectPool *cp);
int joynetTraverseNode(struct JoyConnectPool *cp, JoyNodeCallBack callback);

JoyRecvCallBack joynetGetMsgCallBackFunc(struct JoyConnectPool *cp, enum JoynetMsgType type);


#ifdef __cplusplus
}
#endif

#endif
