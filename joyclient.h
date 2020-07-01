#ifndef __JOY_CLIENT_H
#define __JOY_CLIENT_H

#include "joynet.h"

#define kJoyClientPollTimeOut 0
#define kJoyClientConnectTimeOut 10
#define kJoyClientSendBufSize 10 * 1024 * 1024   //10MB发送缓存
#define kJoyClientRecvBufSize 10 * 1024 * 1024   //10MB接受缓存

struct JoyClientConfig {
    JoyRecvCallBack *cmap;                  // 回调函数索引
    char *initbuf;                          // 初始握手包额外数据
    int buflen;                             // 初始包数据大小
    int nodeNum;                            // 节点数量
    int procid;                             // 进程id
};

struct JoyClient {
    struct JoyClientConfig cfg;
    struct JoyConnectPool *cpool;            //连接池
};

#ifdef __cplusplus
extern "C" {
#endif

// 不要重复调用init，导致内存泄漏
int joyClientInit(struct JoyClientConfig clientCfg, struct JoyBlockConfig blockCfg);

int joyClientStop();
int joyClientConnectTcp(const char *addr, int port, int routerid);
int joyClientProcRecvData();
int joyClientReadRecvData();
int joyClientProcSendData();
int joyClientWriteSendData(const char *buf, int len, int srcid, int dstid, int dstnid);
int joyClientCanStop();

int joyClientGetMemUsage();

#ifdef __cplusplus
}
#endif

#endif
