mms_t  *mms_this = NULL;

void *mms_conn_thread( void *arg )
{
	mms_thrd_args *conn_args = (mms_thrd_args *)arg;
	mms_io_t *io = conn_args->io;
	void *data = conn_args->data;
	const char *url = conn_args->url;
	int bandwidth = conn_args->bandwidth;
	
  iconv_t url_conv = (iconv_t)-1;
  mms_t  *this;
  int     res;
  uint32_t openid;
  mms_buffer_t command_buffer;
  
  if (!url) {
    //return NULL;
	  mms_conn_flag = NULL;
	  pthread_exit(arg);
  }

#ifdef _WIN32
  if (!mms_internal_winsock_load()) {
    //return NULL;
	  mms_conn_flag = NULL;
	  pthread_exit(arg);
  }
#endif

  /* FIXME: needs proper error-signalling work */
  this = (mms_t*)calloc(1, sizeof(mms_t));

  this->url             = strdup (url);
  this->s               = -1;
  this->scmd_body       = this->scmd + CMD_HEADER_LEN + CMD_PREFIX_LEN;
  this->need_discont    = 1;
  this->buf_packet_seq_offset = -1;
  this->bandwidth       = bandwidth;

  this->guri = gnet_uri_new(this->url);
  if(!this->guri) {
    lprintf("invalid url\n");
    goto fail;
  }

  /* MMS wants unescaped (so not percent coded) strings */
  gnet_uri_unescape(this->guri);

  this->proto = this->guri->scheme;
  this->user = this->guri->user;
  this->connect_host = this->guri->hostname;
  this->connect_port = this->guri->port;
  this->password = this->guri->passwd;
  this->uri = gnet_mms_helper(this->guri, 0);

  if(!this->uri)
        goto fail;

  if (!mms_valid_proto(this->proto)) {
    lprintf("unsupported protocol: %s\n", this->proto);
    goto fail;
  }
  mms_this = this;
  
  if (mms_tcp_connect(io, this)) {
    goto fail;
  }
  
  url_conv = iconv_open("UTF-16LE", "UTF-8");
  if (url_conv == (iconv_t)-1) {
    lprintf("could not get iconv handle to convert url to unicode\n");
    goto fail;
  }

  /*
   * let the negotiations begin...
   */

  /* command 0x1 */
  lprintf("send command 0x01\n");
  mms_buffer_init(&command_buffer, this->scmd_body);
  mms_buffer_put_32 (&command_buffer, 0x0003001C);
  mms_gen_guid(this->guid);
  sprintf(this->str, "NSPlayer/7.0.0.1956; {%s}; Host: %s", this->guid,
          this->connect_host);
  res = string_utf16(url_conv, this->scmd_body + command_buffer.pos, this->str,
                     CMD_BODY_LEN - command_buffer.pos);
  if(!res)
    goto fail;
  
  /*
   * add
   * 2013-04-17
   */
  mms_cur_thrd = pthread_self();
  fprintf( stderr, "mms_cur_thrd : %u\n", mms_cur_thrd );
  pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL);     //允许退出线程
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL); //设置立即取消
  alarm(2);	//1s 未连上，则取消线程
  /* over */
  
  if (!send_command(io, this, 1, 0, 0x0004000b, command_buffer.pos + res)) {
    lprintf("failed to send command 0x01\n");
    goto fail;
  }
  
  if ((res = get_answer (io, this)) != 0x01) {
    lprintf("unexpected response: %02x (0x01)\n", res);
    goto fail;
  }

  res = LE_32(this->buf + 40);
  if (res != 0) {
    lprintf("error answer 0x01 status: %08x (%s)\n",
            res, status_to_string(res));
    goto fail;
  }

  /* TODO: insert network timing request here */
  /* command 0x2 */
  lprintf("send command 0x02\n");
  mms_buffer_init(&command_buffer, this->scmd_body);
  mms_buffer_put_32 (&command_buffer, 0x00000000);
  mms_buffer_put_32 (&command_buffer, 0x00989680);
  mms_buffer_put_32 (&command_buffer, 0x00000002);
  res = string_utf16(url_conv, this->scmd_body + command_buffer.pos,
                     "\\\\192.168.0.129\\TCP\\1037",
                     CMD_BODY_LEN - command_buffer.pos);
  if(!res)
    goto fail;

  if (!send_command(io, this, 2, 0, 0xffffffff, command_buffer.pos + res)) {
    lprintf("failed to send command 0x02\n");
    goto fail;
  }

  switch (res = get_answer (io, this)) {
    case 0x02:
      /* protocol accepted */
      break;
    case 0x03:
      lprintf("protocol failed\n");
      goto fail;
    default:
      lprintf("unexpected response: %02x (0x02 or 0x03)\n", res);
      goto fail;
  }

  res = LE_32(this->buf + 40);
  if (res != 0) {
    lprintf("error answer 0x02 status: %08x (%s)\n",
            res, status_to_string(res));
    goto fail;
  }

  /* command 0x5 */
  {
    mms_buffer_t command_buffer;
    
    lprintf("send command 0x05\n");
    mms_buffer_init(&command_buffer, this->scmd_body);
    mms_buffer_put_32 (&command_buffer, 0x00000000); /* ?? */
    mms_buffer_put_32 (&command_buffer, 0x00000000); /* ?? */

    res = string_utf16(url_conv, this->scmd_body + command_buffer.pos,
                       this->uri, CMD_BODY_LEN - command_buffer.pos);
    if(!res)
      goto fail;

    if (!send_command(io, this, 5, 1, 0, command_buffer.pos + res)) {
      lprintf("failed to send command 0x05\n");
      goto fail;
    }
  }
  
  switch (res = get_answer (io, this)) {
    case 0x06:
      {
        int xx, yy;
        /* no authentication required */
        openid = LE_32(this->buf + 48);
      
        /* Warning: sdp is not right here */
        xx = this->buf[62];
        yy = this->buf[63];
        this->live_flag = ((xx == 0) && ((yy & 0xf) == 2));
        this->seekable = !this->live_flag;
        lprintf("openid=%d, live: live_flag=%d, xx=%d, yy=%d\n", openid, this->live_flag, xx, yy);
      }
      break;
    case 0x1A:
      /* authentication request, not yet supported */
      lprintf("authentication request, not yet supported\n");
      goto fail;
      break;
    default:
      lprintf("unexpected response: %02x (0x06 or 0x1A)\n", res);
      goto fail;
  }

  res = LE_32(this->buf + 40);
  if (res != 0) {
    lprintf("error answer 0x06 status: %08x (%s)\n",
            res, status_to_string(res));
    goto fail;
  }

  /* command 0x15 */
  lprintf("send command 0x15\n");
  {
    mms_buffer_t command_buffer;
    mms_buffer_init(&command_buffer, this->scmd_body);
    mms_buffer_put_32 (&command_buffer, 0x00000000);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, 0x00008000);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, 0xFFFFFFFF);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, 0x00000000);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, 0x00000000);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, 0x00000000);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, 0x00000000);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, 0x40AC2000);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, ASF_HEADER_PACKET_ID_TYPE);   /* Header Packet ID type */
    mms_buffer_put_32 (&command_buffer, 0x00000000);                  /* ?? */
    if (!send_command (io, this, 0x15, openid, 0, command_buffer.pos)) {
      lprintf("failed to send command 0x15\n");
      goto fail;
    }
  }
  
  if ((res = get_answer (io, this)) != 0x11) {
    lprintf("unexpected response: %02x (0x11)\n", res);
    goto fail;
  }

  res = LE_32(this->buf + 40);
  if (res != 0) {
    lprintf("error answer 0x11 status: %08x (%s)\n",
            res, status_to_string(res));
    goto fail;
  }

  this->num_stream_ids = 0;

  if (!get_asf_header (io, this))
    goto fail;

  interp_asf_header (this);
  if (!this->asf_packet_len || !this->num_stream_ids)
    goto fail;

  if (!mms_choose_best_streams(io, this)) {
    lprintf("mms_choose_best_streams failed\n");
    goto fail;
  }

  /* command 0x07 */
  this->packet_id_type = ASF_MEDIA_PACKET_ID_TYPE;
  {
    mms_buffer_t command_buffer;
    mms_buffer_init(&command_buffer, this->scmd_body);
    mms_buffer_put_32 (&command_buffer, 0x00000000);                  /* 64 byte float timestamp */
    mms_buffer_put_32 (&command_buffer, 0x00000000);                  
    mms_buffer_put_32 (&command_buffer, 0xFFFFFFFF);                  /* ?? */
    mms_buffer_put_32 (&command_buffer, 0xFFFFFFFF);                  /* first packet sequence */
    mms_buffer_put_8  (&command_buffer, 0xFF);                        /* max stream time limit (3 bytes) */
    mms_buffer_put_8  (&command_buffer, 0xFF);
    mms_buffer_put_8  (&command_buffer, 0xFF);
    mms_buffer_put_8  (&command_buffer, 0x00);                        /* stream time limit flag */
    mms_buffer_put_32 (&command_buffer, this->packet_id_type);    /* asf media packet id type */
    if (!send_command (io, this, 0x07, 1, 0x0001FFFF, command_buffer.pos)) {
      lprintf("failed to send command 0x07\n");
      goto fail;
    }
  }

  iconv_close(url_conv);
  lprintf("connect: passed\n");
 
  //return this;
	  mms_conn_flag = this;
	  pthread_exit(arg);

fail:
  mms_close(this);
  mms_this = NULL;

  if (url_conv != (iconv_t)-1)
    iconv_close(url_conv);

  //return NULL;
	  mms_conn_flag = NULL;
	  pthread_exit(arg);
}

void mms_alarm(int signo)
{
	int ret;
	
	fprintf( stderr, "mms current thrd : %u\n", mms_cur_thrd );
	fprintf( stderr, "mmsh current thrd : %u\n", mmsh_cur_thrd );
	if ( !mms_conn_flag ) {
		fprintf(stderr, "\n<<<mms alarm coming>>>\n");
		if ( mms_this != NULL ) {
			mms_close(mms_this);
		}
		/*if (url_conv != (iconv_t)-1)
			iconv_close(url_conv);*/
		
		if ( mms_cur_thrd != 0 ) {
			ret = pthread_cancel(mms_cur_thrd);
		} else if ( mmsh_cur_thrd != 0 ) {
			ret = pthread_cancel(mmsh_cur_thrd);
		}
		if ( ret != 0 ) {
			fprintf( stderr, "mms cancel thread failed\n" );
			return ;
		}
		fprintf(stderr, "\n<<<mms thread cancel successful>>>\n\n");
	} else {
		fprintf(stderr, "*** mms_conn_flag != NULL >>>\n\n");
	}
}