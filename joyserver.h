#ifndef __JOY_SERVER_H
#define __JOY_SERVER_H

#include "joynet.h"

#define kJoyServerEpollTimeOut 0
#define kJoyServerSendBufSize 1 * 1024 * 1024   //1MB发送缓存
#define kJoyServerRecvBufSize 1 * 1024 * 1024   //1MB接受缓存
#define kJoyServerMaxNid    65536               //nid最大值
#define kListenBacklog  4096                    //建立连接(ESTABLISHED状态)的最大数量
#define kJoynetMaxIPLen 64

struct JoyServerConfig {
    JoyRecvCallBack *cmap;                  // 回调函数索引
    char addr[kJoynetMaxIPLen];             // ip
    int port;                               // 端口
    int insid;                              // 实例id
};

struct JoyServer {
    struct JoyServerConfig cfg;
    int efd;                                //epoll fd
    struct JoyConnectNode lnode;            //listen node
    struct JoyConnectPool *cpool;           //连接池
};

#ifdef __cplusplus
extern "C" {
#endif

int joyServerInit(struct JoyServerConfig serverCfg, struct JoyBlockConfig blockCfg);
int joyServerStop();
int joyServerProcRecvData();
int joyServerReadRecvData();
int joyServerProcSendData();
int joyServerWriteSendData(const char *buf, int len, int procid, int srcid, int dstid);
int joyServerCanStop();

int joyServerGetMemUsage();

#ifdef __cplusplus
}
#endif

#endif
