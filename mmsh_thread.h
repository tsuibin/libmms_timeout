#ifndef __MMSH_THREAD_H__
#define __MMSH_THREAD_H__

#include <signal.h>
#include <pthread.h>

#include "bswap.h"
#include "mmsh.h"
#include "asfheader.h"
#include "uri.h"
#include "mms-common.h"

typedef struct mmsh_thread_args {
	mms_io_t *io;
	mmsh_t *this;
	off_t seek;
	uint32_t time_seek
}mmsh_thrd_args;

int conn_init_flag;
pthread_t mmsh_cur_thrd;

void mmsh_alarm(int signo);
void *mmsh_conn_thread( void *arg );

#endif