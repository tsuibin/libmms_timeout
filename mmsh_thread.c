//#include "mmsh_thread.h"

mmsh_t *mmsh_this = NULL;

void *mmsh_conn_thread( void *arg )
{
	mmsh_thrd_args *init_args_ptr = (mmsh_thrd_args *)arg;
	mms_io_t *io = init_args_ptr->io;
	mmsh_t *this = init_args_ptr->this;
	off_t seek = init_args_ptr->seek;
	uint32_t time_seek = init_args_ptr->time_seek;
	
	mmsh_this = this;
	
  int    i;
  int    video_stream = -1;
  int    audio_stream = -1;
  char   stream_selection[10 * ASF_MAX_NUM_STREAMS]; /* 10 chars per stream */
  int    offset;
  
  /* Close exisiting connection (if any) and connect */
  if (this->s != -1)
    closesocket(this->s);
  
  if (mms_tcp_connect(io, this)) {
    //return 0;
	  conn_init_flag = 0;
	  pthread_exit(arg);
  }

  /*
   * let the negotiations begin...
   */
  this->num_stream_ids = 0;

  /* first request */
  lprintf("first http request\n");
  
  snprintf (this->str, SCRATCH_SIZE, mmsh_FirstRequest, this->uri,
            this->http_host, this->http_port, this->http_request_number++);

  mmsh_cur_thrd = pthread_self();
  fprintf( stderr, "mmsh_cur_thrd : %u\n", mmsh_cur_thrd );
  pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL);     //允许退出线程
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL); //设置立即取消
  alarm(2);	//1s 未连上，则取消线程
  
  if (!send_command (io, this, this->str))
    goto fail;

  if (!get_answer (io, this))
    goto fail;

  /* Don't check for != MMSH_SUCCESS as EOS is normal here too */    
  if (get_header(io, this) == MMSH_ERROR)
    goto fail;

  interp_asf_header(this);
  if (!this->asf_packet_len || !this->num_stream_ids)
    goto fail;
  
  closesocket(this->s);

  mms_get_best_stream_ids(this, &audio_stream, &video_stream);

  /* second request */
  lprintf("second http request\n");

  if (mms_tcp_connect(io, this)) {
    //return 0;
	  conn_init_flag = 0;
	  pthread_exit(arg);
  }

  /* stream selection string */
  /* The same selection is done with mmst */
  /* 0 means selected */
  /* 2 means disabled */
  offset = 0;
  for (i = 0; i < this->num_stream_ids; i++) {
    int size;
    if ((this->streams[i].stream_id == audio_stream) ||
        (this->streams[i].stream_id == video_stream)) {
      lprintf("selecting stream %d\n", this->streams[i].stream_id);
      size = snprintf(stream_selection + offset, sizeof(stream_selection) - offset,
                      "ffff:%d:0 ", this->streams[i].stream_id);
    } else {
      lprintf("disabling stream %d\n", this->streams[i].stream_id);
      size = snprintf(stream_selection + offset, sizeof(stream_selection) - offset,
                      "ffff:%d:2 ", this->streams[i].stream_id);
    }
    if (size < 0) goto fail;
    offset += size;
  }

  switch (this->stream_type) {
    case MMSH_SEEKABLE:
      snprintf (this->str, SCRATCH_SIZE, mmsh_SeekableRequest, this->uri,
                this->http_host, this->http_port, time_seek,
                (unsigned int)(seek >> 32),
                (unsigned int)seek, this->http_request_number++, 0,
                this->num_stream_ids, stream_selection);
      break;
    case MMSH_LIVE:
      snprintf (this->str, SCRATCH_SIZE, mmsh_LiveRequest, this->uri,
                this->http_host, this->http_port, this->http_request_number++,
                this->num_stream_ids, stream_selection);
      break;
  }
  
  if (!send_command (io, this, this->str))
    goto fail;
  
  if (!get_answer (io, this))
    goto fail;

  if (get_header(io, this) != MMSH_SUCCESS)
    goto fail;

  interp_asf_header(this);
  if (!this->asf_packet_len || !this->num_stream_ids)
    goto fail;

  mms_disable_disabled_streams_in_asf_header(this, audio_stream, video_stream);

  //return 1;
  conn_init_flag = 1;
  pthread_exit(arg);
fail:
  closesocket(this->s);
  this->s = -1;
  //return 0;
  conn_init_flag = 0;
  pthread_exit(arg);
}

void mmsh_alarm(int signo)
{
	int ret;
	
	if ( !conn_init_flag ) {
		fprintf(stderr, "\n<<<mmsh alarm coming>>>\n");
		if ( mmsh_this->s != -1 ) {
			closesocket(mmsh_this->s);
			mmsh_this->s = -1;
		}
		
		fprintf( stderr, "current thrd : %u\n", mmsh_cur_thrd );
		ret = pthread_cancel(mmsh_cur_thrd);
		if ( ret != 0 ) {
			fprintf( stderr, "mmsh cancel thread failed\n" );
			return ;
		}
		fprintf(stderr, "\n<<<mmsh thread cancel successful>>>\n\n");
	} else {
		fprintf(stderr, "*** conn_init_flag == %d >>>\n\n", conn_init_flag);
	}
}