#ifndef __JOY_SERVER_H
#define __JOY_SERVER_H

#include "joynet.h"

#include <sys/epoll.h>

#define kJoyServerEpollTimeOut 5
#define kJoyServerSendBufSize 1 * 1024 * 1024   //1MB发送缓存
#define kJoyServerRecvBufSize 1 * 1024 * 1024   //1MB接受缓存
#define kJoyServerMaxNid    65536               //nid最大值
#define kListenBacklog  1024                    //建立连接(ESTABLISHED状态)的最大数量


struct JoyServer {
    int efd;                                //epoll fd
    int lfd;                                //监听fd
    int nid[kJoyServerMaxNid];              //nid索引
    struct JoyConnectPool *cpool;           //连接池
};

#ifdef __cplusplus
extern "C" {
#endif

int joyServerInit(struct JoyBlockConfig conf);
int joyServerStop();
int joyServerListen(const char *addr, int port);
int joyServerCloseTcp();
int joyServerProcRecvData();
int joyServerReadRecvData(joyRecvCallBack recvCallBack);
int joyServerProcSendData();
int joyServerWriteSendData(const char *buf, int len, int procid, int srcid, int dstid, int dstnid);
int joyServerGetNodeNum();

#ifdef __cplusplus
}
#endif

#endif