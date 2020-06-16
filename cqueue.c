#include "cqueue.h"
#include "debug.h"

#include <stdlib.h>


int cqueueEn(int *head, int *tail, int cnt, int size)
{
    if (NULL == head || NULL == tail || cnt <= 0 || size <= 0) {
        debug_msg("error: invalid param, head[%p], tail[%p], cnt[%d], size[%d]", head, tail, cnt, size);
        return -1;
    }

    if ((*head) <= ((*tail + cnt) % size)) {
        debug_msg("error: queue room not enough.");
        return -1;
    }

    *tail = (*tail + cnt) % size;

    return 0;
}

int cqueueDe(int *head, int *tail, int cnt, int size)
{
    if (NULL == head || NULL == tail || cnt <= 0 || size <= 0) {
        debug_msg("error: invalid param, head[%p], tail[%p], cnt[%d], size[%d]", head, tail, cnt, size);
        return -1;
    }

    if (*tail < (*head + cnt) % size) {
        debug_msg("error: queue data not enough.");
        return -1;
    }

    *head = (*head + cnt) % size;

    return 0;
}

int cqueueIsEmpty(int head, int tail, int size)
{
    if (head < 0 || tail < 0 || size <= 0) {
        debug_msg("error: invalid param, head[%d], tail[%d], size[%d]", head, tail, size);
        return -1;
    }

    if (head == tail) {
        return 1;
    }

    return 0;
}

int cqueueIsFull(int head, int tail, int size)
{
    if (head < 0 || tail < 0 || size <= 0) {
        debug_msg("error: invalid param, head[%d], tail[%d], size[%d]", head, tail, size);
        return -1;
    }

    if ((tail + 1) % size == head) {
        return 1;
    }

    return 0;
}

int cqueueGetCount(int head, int tail, int size)
{
    if (head < 0 || tail < 0 || size <= 0) {
        debug_msg("error: invalid param, head[%d], tail[%d], size[%d]", head, tail, size);
        return -1;
    }

    if (tail < head) {
        return (size - head) + tail;
    }

    return tail - head;
}

int cqueueGetRoom(int head, int tail, int size)
{
    if (head < 0 || tail < 0 || size <= 0) {
        debug_msg("error: invalid param, head[%d], tail[%d], size[%d]", head, tail, size);
        return -1;
    }

    if (tail < head) {
        return head - tail - 1;
    }

    return (size - tail) + head - 1;
}

int cqueueReset(int *head, int *tail)
{
    if (NULL == head || NULL == tail) {
        debug_msg("error: invalid param, head[%p], tail[%p]", head, tail);
    }

    *head = 0;
    *tail = 0;

    return 0;
}

int cqueueWrite(int *head, int *tail, int size, char *buf, struct JoynetRWBuf *wbuf)
{
    if (NULL == head || NULL == tail || size <= 0 || NULL == buf || NULL == wbuf) {
        debug_msg("error: invalid param, head[%p], tail[%p], size[%d], buf[%p], wbuf[%p]",
                head, tail, size, buf, wbuf);
        return -1;
    }

    int wlen = wbuf->len[0] + wbuf->len[1];
    if (wlen <= 0) {
        debug_msg("error: invalid param, wlen[%d], len1[%d], len2[%d]", wlen, wbuf->len[0], wbuf->len[1]);
        return -1;
    }

    int leftroom = cqueueGetRoom(*head, *tail, size);
    if (leftroom < 0) {
        debug_msg("error: fail to get room, head[%d], tail[%d], size[%d]", *head, *tail, size);
        return -1;
    }

    if (leftroom < wlen) {
        debug_msg("error: queue room not enough, leftroom[%d], wlen[%d]", leftroom, wlen);
        return -1;
    }

    for (int i = 0; i < 2; ++i) {
        if (*tail < *head) {
            memcpy(buf + *tail, wbuf->buf[i], wbuf->len[i]);
        } else {
            int tailroom = size - *tail;
            if (wbuf->len[i] <= tailroom) {
                memcpy(buf + *tail, wbuf->buf[i], wbuf->len[i]);
            } else {
                memcpy(buf + *tail, wbuf->buf[i], tailroom);
                memcpy(buf, wbuf->buf[i], wbuf->len[i] - tailroom);
            }
        }
        *tail = (*tail + wbuf->len[i]) % size;
    }

    return 0;
}

int cqueueRead(int *head, int *tail, int size, char *buf, int rlen, struct JoynetRWBuf *rbuf)
{
    if (NULL == head || NULL == tail || size <= 0 || NULL == buf || rlen <= 0 || NULL == rbuf) {
        debug_msg("error: invalid param, head[%p], tail[%p], size[%d], buf[%p], rlen[%d], wbuf[%p]",
                head, tail, size, buf, rlen, rbuf);
        return -1;
    }

    int datacnt = cqueueGetCount(*head, *tail, size);
    if (datacnt < rlen) {
        debug_msg("error: queue data not enough, datacnt[%d], rlen[%d]", datacnt, rlen);
        return -1;
    }

    if (*head < *tail) {
        rbuf->buf[0] = buf + *head;
        rbuf->len[0] = rlen;
        rbuf->len[1] = 0;
    } else {
        int tailcnt = size - *head;
        if (rlen <= tailcnt) {
            rbuf->buf[0] = buf + *head;
            rbuf->len[0] = rlen;
            rbuf->len[1] = 0;
        } else {
            rbuf->buf[0] = buf + *head;
            rbuf->len[0] = tailcnt;
            rbuf->buf[1] = buf;
            rbuf->len[1] = rlen - tailcnt;
        }
    }

    *head = (*head + rlen) % size;

    return 0;
}

/* #ifdef DEBUG */
/* int main() */
/* { */
    /* return 0; */
/* } */
/* #endif */
