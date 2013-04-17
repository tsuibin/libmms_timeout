#ifndef __MMS_THREAD_H__
#define __MMS_THREAD_H__

#include <signal.h>
#include <pthread.h>

#include "bswap.h"
#include "mms.h"
#include "asfheader.h"
#include "uri.h"
#include "mms-common.h"

typedef struct mms_thread_args {
	mms_io_t *io;
	void *data;
	const char *url;
	int bandwidth
}mms_thrd_args;

mms_t  *mms_conn_flag;
pthread_t mms_cur_thrd;
extern pthread_t mmsh_cur_thrd;

void mms_alarm(int signo);
void *mms_conn_thread( void *arg );

#endif