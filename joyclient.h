#ifndef __JOY_CLIENT_H
#define __JOY_CLIENT_H

#include "joynet.h"

#include <poll.h>

#define kJoyClientPollTimeOut 0
#define kJoyClientConnectTimeOut 10
#define kJoyClientSendBufSize 10 * 1024 * 1024   //10MB发送缓存
#define kJoyClientRecvBufSize 10 * 1024 * 1024   //10MB接受缓存


struct JoyClient {
    int shakeDataLen;                        //握手数据长度
    char shakeData[kJoynetShakeBufSize];     //握手数据
    struct JoyConnectPool *cpool;            //连接池
};

#ifdef __cplusplus
extern "C" {
#endif

int joyClientInit(JoyRecvCallBack *cmap, struct JoyBlockConfig conf, char *initbuf, int len, int nodeNum, int shmkey);
int joyClientTick();
int joyClientConnectTcp(const char *addr, int port, int procid, int routerid);
int joyClientIsReady(int routerid);
int joyClientCloseTcp(int routerid);
int joyClientProcRecvData();
int joyClientReadRecvData();
int joyClientProcSendData();
int joyClientWriteSendData(const char *buf, int len, int srcid, int dstid, int dstnid);

#ifdef __cplusplus
}
#endif

#endif
