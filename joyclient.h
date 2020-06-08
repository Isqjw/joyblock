#ifndef __JOY_CLIENT_H
#define __JOY_CLIENT_H

#include "joynet.h"

#include <poll.h>

#define kJoyClientPollTimeOut 0
#define kJoyClientConnectTimeOut 10
#define kJoyClientSendBufSize 10 * 1024 * 1024   //10MB发送缓存
#define kJoyClientRecvBufSize 10 * 1024 * 1024   //10MB接受缓存

#define kJoyClientMaxNidNum 64


struct JoyClient {
    int nids;
    int nid[kJoyClientMaxNidNum];
    struct JoyConnectPool *cpool;            //连接池
};

#ifdef __cplusplus
extern "C" {
#endif

int joyClientInit(struct JoyBlockConfig conf, int nids, int *nid, int nodeNum);
int joyClientTick();
int joyClientConnectTcp(const char *addr, int port, int procid, int routerid);
int joyClientIsReady(int routerid);
int joyClientCloseTcp(int routerid);
int joyClientProcRecvData();
int joyClientReadRecvData(joyRecvCallBack recvCallBack);
int joyClientProcSendData();
int joyClientWriteSendData(const char *buf, int len, int srcid, int dstid);

// 为了兼容发送时只知道nid不知道procid的情况
int joyClientSendDataByNid(const char *buf, int len, int srcid, int dstnid);

#ifdef __cplusplus
}
#endif

#endif
