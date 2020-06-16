#ifndef __C_QUEUE_H
#define __C_QUEUE_H

#include "joyconst.h"

#ifdef __cplusplus
extern "C" {
#endif

int cqueueEn(int *head, int *tail, int cnt, int size);
int cqueueDe(int *head, int *tail, int cnt, int size);
int cqueueIsEmpty(int head, int tail, int size);
int cqueueIsFull(int head, int tail, int size);
int cqueueGetCount(int head, int tail, int size);
int cqueueGetRoom(int head, int tail, int size);
int cqueueReset(int *head, int *tail);

int cqueueWrite(int *head, int *tail, int size, char *buf, struct JoynetRWBuf *wbuf);
int cqueueRead(int *head, int *tail, int size, char *buf, int rlen, struct JoynetRWBuf *rbuf);

#ifdef __cplusplus
}
#endif

#endif
