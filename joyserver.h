#ifndef __JOY_SERVER_H
#define __JOY_SERVER_H

#include "joynet.h"

#define kJoyServerEpollTimeOut 5
#define kJoyServerSendBufSize 1 * 1024 * 1024   //1MB发送缓存
#define kJoyServerRecvBufSize 1 * 1024 * 1024   //1MB接受缓存
#define kJoyServerMaxNid    65536               //nid最大值
#define kListenBacklog  4096                    //建立连接(ESTABLISHED状态)的最大数量


struct JoyServer {
    int efd;                                //epoll fd
    struct JoyConnectNode lnode;            //listen node
    // int nid[kJoyServerMaxNid];              //nid索引
    struct JoyConnectPool *cpool;           //连接池
};

#ifdef __cplusplus
extern "C" {
#endif

int joyServerInit(JoyRecvCallBack *cmap, struct JoyBlockConfig conf, int shmkey);
int joyServerStop();
int joyServerListen(const char *addr, int port);
int joyServerStopListen();
int joyServerProcRecvData();
int joyServerReadRecvData();
int joyServerProcSendData();
int joyServerWriteSendData(const char *buf, int len, int procid, int srcid, int dstid);
int joyServerGetNodeNum();

#ifdef __cplusplus
}
#endif

#endif
