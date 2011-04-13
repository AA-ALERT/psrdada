/***************************************************************************
 *  
 *    Copyright (C) 2010 by Andrew Jameson
 *    Licensed under the Academic Free License version 2.1
 * 
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include "dada_ib.h"

dada_ib_cm_t * dada_ib_create_cm (unsigned nbufs, multilog_t * log) 
{

  dada_ib_cm_t * ctx = (dada_ib_cm_t *) malloc(sizeof(dada_ib_cm_t));
  if (!ctx)
  {
    multilog(log, LOG_ERR, "dada_ib_create_cm: malloc failed\n");
    return 0;
  }

  ctx->cm_channel = 0;
  ctx->cm_id = 0;
  ctx->event = 0;
  ctx->comp_chan = 0;
  ctx->pd = 0;
  ctx->cq = 0;
  ctx->nbufs = nbufs;
  ctx->verbose = 0;
  ctx->sync_to = 0;
  ctx->sync_to_val = 0;
  ctx->sync_from = 0;
  ctx->sync_from_val = 0;
  ctx->cm_connected = 0;
  ctx->ib_connected = 0;
  ctx->log = log;
  ctx->depth = 0;

  ctx->bufs = (dada_ib_mb_t **) malloc(sizeof(dada_ib_mb_t *) * nbufs);
  if (!ctx->bufs)
  {
    multilog(log, LOG_ERR, "dada_ib_create_cm: could not allocate memory for ctx->bufs\n");
    return 0;
  }

  ctx->local_blocks = (dada_ib_shm_block_t *) malloc(sizeof(dada_ib_shm_block_t) * nbufs);
  if (!ctx->local_blocks)
  {
    multilog(log, LOG_ERR, "dada_ib_create_cm: could not allocate memory "
                           "for ctx->local_blocks\n");
    return 0;
  }

  /*
  // create the event channel
  ctx->cm_channel = rdma_create_event_channel();
  if (!ctx->cm_channel)
  {
    multilog(log, LOG_ERR, "dada_ib_create_cm: rdma_create_event_channel failed\n");
    return 0;
  }*/

  return ctx;

}

/*
 *  Accpet the RDMA CM connection on the specified port 
 */
int dada_ib_listen_cm (dada_ib_cm_t * ctx, int port)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose)
    multilog(log, LOG_INFO, "dada_ib_listen_cm()\n");

  // create the event channel
  assert(ctx->cm_channel == 0);
  ctx->cm_channel = rdma_create_event_channel();
  if (!ctx->cm_channel)
  {
    multilog(log, LOG_ERR, "dada_ib_create_cm: rdma_create_event_channel failed\n");
    return -1;
  }

  int err = 0;
  struct rdma_cm_id    * listen_id;
  struct rdma_cm_event * event;
  struct sockaddr_in     sin;

  // ensure the cm_id is not set
  assert(ctx->cm_id == 0);

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_listen_cm: rdma_create_id\n");
  err = rdma_create_id(ctx->cm_channel, &listen_id, NULL, RDMA_PS_TCP);
  if (err)
  {
    multilog(log, LOG_ERR, "dada_ib_listen_cm: rdma_create_id failed [%d]\n", err);
    return -1;
  }

  sin.sin_family      = AF_INET;
  sin.sin_port        = htons(port);
  sin.sin_addr.s_addr = INADDR_ANY;

  // Bind to local port and listen for connection request 
  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_listen_cm: rdma_bind_addr on port %d\n", port);
  err = rdma_bind_addr(listen_id, (struct sockaddr *) &sin);
  if (err)
  {
    multilog(log, LOG_ERR, "dada_ib_listen_cm: rdma_bind_addr failed [%d]\n", err);
    return -1;
  }

  // accept the connection
  if (ctx->verbose)
    multilog(log, LOG_INFO, "dada_ib_listen_cm: waiting for RDMA connection\n");
  err = rdma_listen (listen_id, 1);
  if (err)
  {
    multilog(log, LOG_ERR, "dada_ib_listen_cm:  rdma_listen failed [%d]\n", err);
    return -1;
  }
  err = rdma_get_cm_event(ctx->cm_channel, &event);
  if (err)
  {
    multilog(log, LOG_ERR, "dada_ib_listen_cm: rdma_get_cm_event failed "
             "[%d]\n", err); 
    return -1;
  }

  if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST)
  {
    multilog(log, LOG_ERR, "dada_ib_listen_cm: rdma_get_cm_event returned "
             "%s event, expected RDMA_CM_EVENT_CONNECT_REQUEST\n", 
             rdma_event_str(event->event));
    return -1;
  }

  if (ctx->verbose)
    multilog(log, LOG_INFO, "dada_ib_listen_cm: event->id = %d\n", event->id);

  ctx->cm_id = event->id;

  rdma_ack_cm_event(event);

  return 0;

}


/*
 *  Connect to the IB CM on the server
 */
int dada_ib_connect_cm (dada_ib_cm_t * ctx, const char *host, unsigned port)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose)
    multilog(log, LOG_INFO, "dada_ib_connect_cm()\n");

  int err = 0;
  struct rdma_cm_event * event;
  struct addrinfo hints = {
    .ai_family   = AF_INET,
    .ai_socktype = SOCK_STREAM
  };
  struct addrinfo *res, *t;
  char port_str[8];
  sprintf(port_str, "%d", port);
  
  // create the event channel
  assert(ctx->cm_channel == 0);
  ctx->cm_channel = rdma_create_event_channel();
  if (!ctx->cm_channel)
  { 
    multilog(log, LOG_ERR, "connect_cm: rdma_create_event_channel failed\n");
    return -1;
  }

  // ensure the cm_id is not set
  assert(ctx->cm_id == 0);

  // create the CM ID
  err = rdma_create_id(ctx->cm_channel, &(ctx->cm_id), NULL, RDMA_PS_TCP);
  if (err)
  {
    multilog(log, LOG_ERR, "connect_cm: rdma_create_id failed [%d]\n", err);
    return -1;
  }

  // convert the host:port inot an addrinfo struct
  err = getaddrinfo(host, port_str, &hints, &res);
  if (err < 0)
  {
    multilog(log, LOG_ERR, "connect_cm: getaddrinfo failed. host=%s, port=%s\n", host, port_str);
    return -1;
  }

  /* Resolve server address and route */
  for (t = res; t; t = t->ai_next) {
    err = rdma_resolve_addr (ctx->cm_id, NULL, t->ai_addr, RESOLVE_TIMEOUT_MS);
    if (!err)
      break;
  }
  if (err) 
  {
    multilog(log, LOG_ERR, "connect_cm: rdma_resolve_addr failed. host=%s, port=%s\n", host, port_str);
    return -1;
  }

  err = rdma_get_cm_event(ctx->cm_channel, &event);
  if (err) 
  {
    multilog(log, LOG_ERR, "connect_cm: rdma_get_cm_event failed\n");
    return -1;
  }

  if (ctx->verbose)
    multilog(log, LOG_INFO, "connect_cm: event->event=%s\n", rdma_event_str(event->event));

  if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
    multilog(log, LOG_ERR, "connect_cm: rdma_get_cm_event returned %s event, expected "
                             "RDMA_CM_EVENT_ADDR_RESOLVED\n", rdma_event_str(event->event));
    return -1;
  }

  rdma_ack_cm_event(event);

  err = rdma_resolve_route(ctx->cm_id, RESOLVE_TIMEOUT_MS);
  if (err)
  {
    multilog(log, LOG_ERR, "connect_cm: rdma_resolve_route failed\n");
    return -1;
  }

  err = rdma_get_cm_event(ctx->cm_channel, &event);
  if (err) 
  {
    multilog(log, LOG_ERR, "connect_cm: rdma_get_cm_event failed\n");
    return -1;
  }

  if (ctx->verbose)
    multilog(log, LOG_INFO, "connect_cm: event->event=%s\n", rdma_event_str(event->event));

  if (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED)
  {
    multilog(log, LOG_ERR, "connect_cm: rdma_get_cm_event returned %s event, expected "
                           "RDMA_CM_EVENT_ROUTE_RESOLVED\n", rdma_event_str(event->event));
    return -1;
  }

  rdma_ack_cm_event(event);

  return 0;

}

int dada_ib_create_verbs(dada_ib_cm_t * ctx, unsigned depth)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose)
    multilog(log, LOG_INFO, "dada_ib_create_verbs()\n");

  // create a PD (protection domain). The PD limits which memory regions can be 
  // accessed by which QP (queue pairs) or CQ (completion queues). 
  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "create_verbs: ibv_alloc_pd \n");
  ctx->pd = ibv_alloc_pd(ctx->cm_id->verbs);
  if (!ctx->pd)
  {
    multilog(log, LOG_ERR, "dada_ib_create_verbs: ibv_alloc_pd failed\n");
    return -1;
  }

  // create a completion channel. This is the mechanism for receiving notifications when
  // CQE (completion queue events) are placed on the CQ
  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "create_verbs: ibv_create_comp_channel\n");
  ctx->comp_chan = ibv_create_comp_channel(ctx->cm_id->verbs);
  if (!ctx->comp_chan)
  {
    multilog(log, LOG_ERR, "dada_ib_create_verbs: ibv_create_comp_channel failed\n");
    return -1;
  }

  // create a CQ. The CQ will hold the CQEs
  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "create_verbs: ibv_create_cq\n");
  ctx->cq = ibv_create_cq(ctx->cm_id->verbs, (int) depth, NULL, ctx->comp_chan, 0);
  if (!ctx->cq)
  {
    multilog(log, LOG_ERR, "dada_ib_create_verbs: ibv_create_cq failed\n");
    return -1;
  }

  // arm the notification mechanism for the CQ 
  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "create_verbs: ibv_req_notify_cq\n");
  if (ibv_req_notify_cq(ctx->cq, 0))
  {
    multilog(log, LOG_ERR, "dada_ib_create_verbs: ibv_req_notify_cq failed\n");
    return -1;
  }

  return 0;
}


/*
 * register the buffer of bufsz bytes to the ctx's PD with the specified access flags 
 * return a dada_ib_mb_t initialised to the correct values 
 */
dada_ib_mb_t * dada_ib_reg_buffer (dada_ib_cm_t * ctx, void * buffer,
                                   uint64_t bufsz, int access_flags)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_reg_buffer()\n");

  dada_ib_mb_t * mr = (dada_ib_mb_t *) malloc(sizeof(dada_ib_mb_t));
  if (!mr)
  {
    multilog (log, LOG_ERR, "dada_ib_reg_buffer: failed to allocate memory for dada_ib_mb_t\n");
    return 0;
  }

  mr->mr = ibv_reg_mr(ctx->pd, buffer, bufsz, access_flags);
  if (!mr->mr)
  {
    multilog(log, LOG_ERR, "dada_ib_reg_buffer: ibv_reg_mr failed buffer=%p, "
                           "buf_size=%"PRIu64"\n", buffer, bufsz);
    free(mr);
    return 0;
  }

  mr->buffer = buffer;
  mr->size   = bufsz;
  mr->wr_id  = 0;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "reg_buffer: buffer=%p, size=%"PRIu64"\n", mr->buffer, mr->size);

  return mr;
}

int dada_ib_dereg_buffer (dada_ib_mb_t * mr)
{
  if (mr->mr)
  {
    ibv_dereg_mr(mr->mr);
    mr->mr = 0; 
  }
  mr->buffer = 0;
  mr->size = 0;
  mr->wr_id = 0;
  free(mr);

  return 0;
}

/*
 *  register the specified number of buffers to the PD (protection domain), 
 *  all buffers should be the same size
 */
int dada_ib_reg_buffers(dada_ib_cm_t * ctx, char ** buffers, uint64_t bufsz, 
                        int access_flags)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_reg_buffers()\n");

  unsigned i = 0;

  if (!ctx->bufs)
  {
    multilog(log, LOG_ERR, "reg_buffers: ctx->bufs was not defined\n");
    return -1;
  } 

  if (!ctx->nbufs)
  {
    multilog(log, LOG_ERR, "reg_buffers: ctx->nbufs was not defined\n");
    return -1;
  }

  // register the memory regions for each buffer
  for (i=0; i < ctx->nbufs; i++)
  {
    ctx->bufs[i] = dada_ib_reg_buffer(ctx, buffers[i], bufsz, access_flags);
    if (!ctx->bufs[i])
    {
      multilog(log, LOG_ERR, "reg_buffers: dada_ib_reg_buffer bufs[%d] failed\n", i);
      return -1;
    }
    ctx->bufs[i]->wr_id = i;

    ctx->local_blocks[i].buf_va = (uintptr_t) ctx->bufs[i]->buffer;
    ctx->local_blocks[i].buf_lkey = ctx->bufs[i]->mr->lkey;
    ctx->local_blocks[i].buf_rkey = ctx->bufs[i]->mr->rkey;

    if (ctx->verbose)
      multilog (log, LOG_INFO, "reg_buffers: block[%d] buffer=%p buf_va=%p buf_lkey=%p "
                "buf_rkey=%p\n", i, ctx->bufs[i]->buffer, ctx->local_blocks[i].buf_va, 
                ctx->local_blocks[i].buf_lkey, ctx->local_blocks[i].buf_rkey);

  }

  // register the sync memory buffers use 128 bits [16 bytes]
  ctx->sync_size     = sizeof(uint64_t) * 2;
  ctx->sync_to_val   = (uint64_t *) malloc(ctx->sync_size);
  ctx->sync_from_val = (uint64_t *) malloc(ctx->sync_size);
  assert(ctx->sync_to_val != 0);
  assert(ctx->sync_from_val != 0);

  if (ctx->verbose)
    multilog(log, LOG_INFO, "reg_buffers: creating sync buffers size=%d "
             "bytes\n", ctx->sync_size);

  ctx->sync_to = dada_ib_reg_buffer(ctx, ctx->sync_to_val, ctx->sync_size, 
                                    access_flags);
  if (!ctx->sync_to)
  {
    multilog(log, LOG_ERR, "reg_buffers: dada_ib_reg_buffer sync_to failed\n");
    return -1;
  }
  ctx->sync_to->wr_id = 200000;
  ctx->sync_to_val[0] = 0;
  ctx->sync_to_val[1] = 0;

  ctx->sync_from = dada_ib_reg_buffer(ctx, ctx->sync_from_val, ctx->sync_size, 
                                      access_flags);
  if (!ctx->sync_from)
  {
    multilog(log, LOG_ERR, "reg_buffers: dada_ib_reg_buffer sync_from failed\n");
    return -1;
  }
  ctx->sync_from->wr_id = 300000;
  ctx->sync_from_val[0] = 0;
  ctx->sync_from_val[1] = 0;

  return 0;
}

/*
 *  create QP's for each connection
 */
int dada_ib_create_qp (dada_ib_cm_t * ctx, unsigned send_depth, unsigned recv_depth)
{
  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose)
    multilog(log, LOG_INFO, "dada_ib_create_qp()\n");

  struct ibv_qp_init_attr qp_attr = { };
  unsigned err = 0;

  qp_attr.cap.max_send_wr  = send_depth;
  qp_attr.cap.max_send_sge = 1;
  qp_attr.cap.max_recv_wr  = recv_depth;
  qp_attr.cap.max_recv_sge = 1;

  qp_attr.send_cq          = ctx->cq;
  qp_attr.recv_cq          = ctx->cq;

  qp_attr.qp_type          = IBV_QPT_RC;
  qp_attr.sq_sig_all       = 0;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "create_qp: rdma_create_qp\n");

  err = rdma_create_qp(ctx->cm_id, ctx->pd, &qp_attr);
  if (err)
  {
    multilog(log, LOG_ERR, "create_qps: rdma_create_qp cm_id=%p, pd=%p, failed: %s\n", ctx->cm_id, ctx->pd, strerror(err));
    return -1;
  }
  return 0;

}

/*
 *  Accept the connection from the client
 */
int dada_ib_accept (dada_ib_cm_t * ctx)
{
  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose)
    multilog(log, LOG_INFO, "dada_ib_accept()\n");

  struct rdma_cm_event * event;
  struct rdma_conn_param conn_param = { };
  unsigned err = 0;

  conn_param.responder_resources = 1;
  conn_param.private_data        = 0;
  conn_param.private_data_len    = 0;

  // Accept connection
  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "accept: rdma_accept\n");
  err = rdma_accept(ctx->cm_id, &conn_param);
  if (err)
  {
    multilog(log, LOG_ERR, "accept: rdma_accept failed [%d]\n", err);
    return -1;
  }

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "accept: rdma_get_cm_event\n");
  err = rdma_get_cm_event(ctx->cm_channel, &event);
  if (err)
  {
    multilog(log, LOG_ERR, "accept: rdma_get_cm_event failed [%d]\n", err);
    return -1;
  }
      
  if (event->event != RDMA_CM_EVENT_ESTABLISHED)
  {
    multilog(log, LOG_ERR, "accept: rdma_get_cm_event returned %s event, expected "
                           "RDMA_CM_EVENT_ESTABLISHED\n", rdma_event_str(event->event));
    return -1;
  }

  if (ctx->verbose)
    multilog(log, LOG_INFO, "accept: connection established\n");

  rdma_ack_cm_event(event);

  return 0;

}

/*
 *  Connect to the server
 */
int dada_ib_connect (dada_ib_cm_t * ctx)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose)
    multilog(log, LOG_INFO, "dada_ib_connect()\n");

  struct rdma_conn_param conn_param = { };
  struct rdma_cm_event * event;
  int err = 0;
  int connected = 0;

  conn_param.responder_resources = 1;
  conn_param.initiator_depth = 1;
  conn_param.retry_count     = 7;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "connect: rdma_connect\n");
  err = rdma_connect(ctx->cm_id, &conn_param);
  if (err)
  {
    multilog(log, LOG_ERR, "connect: rdma_connect failed: err=%d, str=%s\n", err, strerror(err));
    return -1;
  }

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "connect: rdma_get_cm_event\n");
  err = rdma_get_cm_event(ctx->cm_channel, &event);
  if (err)
  {
    multilog(log, LOG_ERR, "connect: rdma_get_cm_event failed: %s\n", strerror(err));
    return -1;
  }

  if (ctx->verbose)
    multilog(log, LOG_INFO, "connect: cm_event=%s attempts\n", rdma_event_str(event->event));

  if (event->event != RDMA_CM_EVENT_ESTABLISHED)
    multilog(log, LOG_WARNING, "connect: rdma_get_cm_event returned %s event, expected "
                               "RDMA_CM_EVENT_ESTABLISHED\n", rdma_event_str(event->event));
  else
    connected = 1;

  rdma_ack_cm_event(event);

  if (!connected)
  {
    multilog(log, LOG_ERR, "connect: connection could not be established\n");
    return -1;
  }
  else
  {
    if (ctx->verbose)
      multilog(log, LOG_INFO, "connect: connection established\n");
    return 0;
  }
}


/*
 *  post recv on the specified Memory Buffer (mb) and connection 
 */
int dada_ib_post_recv (dada_ib_cm_t * ctx, dada_ib_mb_t *mb)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_post_recv()\n");

  struct ibv_sge      sge;
  struct ibv_recv_wr  rcv_wr = { };
  struct ibv_recv_wr *bad_wr;

  // set the buffer that will receive the data to 0 
  memset ( mb->buffer, 0, mb->size);

  sge.addr   = (uintptr_t) mb->buffer;
  sge.length = mb->size;
  sge.lkey   = mb->mr->lkey;

  rcv_wr.wr_id   = mb->wr_id;
  rcv_wr.sg_list = &sge;
  rcv_wr.num_sge = 1;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_post_recv: addr=%p, length=%d, lkey=%"PRIu32", wr_id=%d\n",
                            sge.addr, sge.length, sge.lkey, mb->wr_id);

  if (ibv_post_recv(ctx->cm_id->qp, &rcv_wr, &bad_wr))
  {
    multilog(log, LOG_ERR, "dada_ib_post_recv: ibv_post_recv failed\n");
    return -1;
  }

  return 0;

}

/*
 *  Waits for a CQE on the CC with IBV_SUCCESS and a matching wr_id
 */
int dada_ib_wait_recv(dada_ib_cm_t * ctx, dada_ib_mb_t * mb)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_wait_recv()\n");

  struct ibv_cq  * evt_cq;
  void           * cq_context;
  struct ibv_wc    wc;

  int data_received = 0;
  int erroneous_wr = 0;

  assert(ctx->comp_chan != 0);
  assert(ctx->cq != 0);

  while (!data_received) {
  
    if (erroneous_wr || ctx->verbose > 1)
      multilog(log, LOG_INFO, "wait_recv: ibv_get_cq_event\n");
    if (ibv_get_cq_event(ctx->comp_chan, &evt_cq, &cq_context))
    {
      multilog(log, LOG_ERR, "wait_recv: ibv_get_cq_event failed\n");
      return -1;
    }

    /* Request notification upon the next completion event */
    if (erroneous_wr || ctx->verbose > 1)
      multilog(log, LOG_INFO, "wait_recv: ibv_req_notify_cq\n");
    if (ibv_req_notify_cq(ctx->cq, 0))
    {
      multilog(log, LOG_ERR, "dada_ib_wait_recv: ibv_req_notify_cq() failed\n");
      return -1;
    } 

    if (erroneous_wr || ctx->verbose > 1)
      multilog(log, LOG_INFO, "wait_recv: ibv_poll_cq\n");
    int ne = ibv_poll_cq(ctx->cq, 1, &wc);
    if (ne < 0) {
      multilog(log, LOG_ERR, "dada_ib_wait_recv: ibv_poll_cq() failed: ne=%d\n", ne);
      return -1;
    }

    if (wc.status != IBV_WC_SUCCESS) 
    {
      multilog(log, LOG_WARNING, "dada_ib_wait_recv: wc.status != IBV_WC_SUCCESS "
               "[wc.status=%s, wc.wr_id=%"PRIu64", mb->wr_id=%"PRIu64"]\n", 
               ibv_wc_status_str(wc.status), wc.wr_id, mb->wr_id);
      return -1;
    }

    if (wc.wr_id != mb->wr_id ) 
    {
      multilog(log, LOG_WARNING, "dada_ib_wait_recv: wr_id=%"PRIu64" != %"PRIu64"\n", wc.wr_id, mb->wr_id);
      // seeing what happens if we ignore the extra WR
      erroneous_wr = 1;
      //return -1;
    }

    if ((wc.wr_id == mb->wr_id) && (wc.status == IBV_WC_SUCCESS))
    {
      if (erroneous_wr || ctx->verbose > 2)
        multilog(log, LOG_INFO, "wait_recv: wr correct\n");
      data_received = 1;
    }
    if (erroneous_wr || ctx->verbose > 1)
      multilog(log, LOG_INFO, "wait_recv: ibv_ack_cq_events\n"); 
    ibv_ack_cq_events(ctx->cq, 1);
  }

  if (erroneous_wr || ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_wait_recv: returned\n");

  return 0;

}


/*
 *  post_send the specified memory buffer (ib) on the specified connection
 *
 *  Will send the data in the buffer in one transaction via the WR_SEND 
 *  mode which will require a post_recv on the remote end
 */
int dada_ib_post_send (dada_ib_cm_t * ctx, dada_ib_mb_t * mb)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  struct ibv_sge       sge;
  struct ibv_send_wr   send_wr = { };
  struct ibv_send_wr * bad_send_wr;
  int err = 0;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_post_send: buffer=%p, bytes=%"PRIu64", wr_id=%d\n", mb->buffer, mb->size, mb->wr_id);

  sge.addr   = (uintptr_t) mb->buffer;
  sge.length = mb->size;
  sge.lkey   = mb->mr->lkey;

  send_wr.sg_list    = &sge;
  send_wr.num_sge    = 1;
  send_wr.next       = NULL;
  send_wr.wr_id      = mb->wr_id;
  send_wr.opcode     = IBV_WR_SEND;
  send_wr.send_flags = IBV_SEND_SIGNALED;

  if (ctx->verbose > 1)
    multilog(log, LOG_INFO, "dada_ib_post_send: ibv_post_send\n");
  err = ibv_post_send(ctx->cm_id->qp, &send_wr, &bad_send_wr);
  if (err)
  {
    multilog(log, LOG_ERR, "dada_ib_post_send: ibv_post_send failed errno=%d strerror=%s\n", errno, strerror(errno));
    return -1;
  }

  return 0;

}

/*
 * Post bytes/chunk_size sends via RDMA on the specified connection
 */

int dada_ib_post_sends(dada_ib_cm_t * ctx, void * buffer, uint64_t bytes, uint64_t chunk_size,
                       uint32_t lkey, uint32_t rkey, uintptr_t raddr)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  struct ibv_sge       sge;
  struct ibv_send_wr   send_wr = { };
  struct ibv_send_wr * bad_send_wr;
  unsigned i = 0;
  uint64_t chunks;

  sge.addr   = (uintptr_t) buffer;
  sge.length = chunk_size;
  sge.lkey   = lkey;

  send_wr.sg_list  = &sge;
  send_wr.num_sge  = 1;
  send_wr.next     = NULL;
  send_wr.wr.rdma.rkey = rkey;
  send_wr.wr.rdma.remote_addr = raddr;
  send_wr.opcode     = IBV_WR_RDMA_WRITE;
  send_wr.send_flags = 0;


  chunks = bytes / chunk_size;
  if ( bytes % chunk_size != 0)
    chunks++;
    
  for (i=0; i<chunks; i++)
  {

    /* first send data via RDMA */
    send_wr.wr_id = i;

    if (ctx->verbose > 2)
      multilog(log, LOG_INFO, "post_sends: ibv_post_send[%d]\n", send_wr.wr_id);

    if (ibv_post_send(ctx->cm_id->qp, &send_wr, &bad_send_wr))
    {
      multilog(log, LOG_ERR, "post_sends: ibv_post_send[%d] failed\n", send_wr.wr_id);
      return -1;
    }

    // incremement address pointers 
    sge.addr += chunk_size; 
    send_wr.wr.rdma.remote_addr += chunk_size;
  }

  return 0;
}

int dada_ib_post_sends_gap (dada_ib_cm_t * ctx, void * buffer, 
                            uint64_t bytes, uint64_t chunk_size, uint32_t lkey, 
                            uint32_t rkey, uintptr_t raddr, uint64_t roffset,
                            uint64_t rgap)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  struct ibv_sge       sge;
  struct ibv_send_wr   send_wr = { };
  struct ibv_send_wr * bad_send_wr;
  unsigned i = 0;
  uint64_t chunks;
  
  sge.addr   = (uintptr_t) buffer;
  sge.length = chunk_size; 
  sge.lkey   = lkey;
  
  send_wr.sg_list  = &sge;
  send_wr.num_sge  = 1;
  send_wr.next     = NULL;
  send_wr.wr.rdma.rkey = rkey;
  send_wr.wr.rdma.remote_addr = raddr;
  send_wr.wr.rdma.remote_addr += roffset;
  send_wr.opcode     = IBV_WR_RDMA_WRITE;
  send_wr.send_flags = 0;
  
  chunks = bytes / chunk_size;
  if ( bytes % chunk_size != 0)
    chunks++;

  if (ctx->verbose)
  {
    multilog(log, LOG_INFO, "post_sends_gap: sending %"PRIu64" bytes "
             "in %"PRIu64" byte chunks\n", bytes, chunk_size);
    multilog(log, LOG_INFO, "post_sends_gap: rkey=%p, raddr=%p, "
             "roffset=%"PRIu64", rgap=%"PRIu64"\n", rkey, raddr, roffset, rgap);
    multilog(log, LOG_INFO, "post_sends_gap: send_wr.wr.rdma.remote_addr=%p\n",
              send_wr.wr.rdma.remote_addr);
  }
    
  for (i=0; i<chunks; i++)
  {
  
    /* first send data via RDMA */
    send_wr.wr_id = i;
    
    if (ctx->verbose > 2)
      multilog(log, LOG_INFO, "post_sends_gap: ibv_post_send[%d]\n", send_wr.wr_id);
      
    if (ibv_post_send(ctx->cm_id->qp, &send_wr, &bad_send_wr))
    {
      multilog(log, LOG_ERR, "post_sends_gap: ibv_post_send[%d] failed\n", send_wr.wr_id);
      return -1;
    } 
    
    // incremement address pointers 
    sge.addr += chunk_size;
    send_wr.wr.rdma.remote_addr += (chunk_size + rgap);
  }

  if (ctx->verbose)
    multilog(log, LOG_INFO, "post_sends_gap: posted %"PRIu64" bytes as sent\n", bytes);

  return 0;
}

/*
 * disconnect from the server 
 */
int dada_ib_client_destroy(dada_ib_cm_t * ctx)
{

  int err = 0;

  assert(ctx);
  multilog_t * log = ctx->log;

  err = rdma_disconnect(ctx->cm_id);
  if (err)
  {
    multilog(log, LOG_ERR, "client_destroy: rdma_disconnect failed\n");
    return -1;
  }

  return dada_ib_destroy(ctx);
}


/*
 * Disconnect the IB connection  
 */
int dada_ib_disconnect(dada_ib_cm_t * ctx)
{

  assert(ctx);
  multilog_t * log = ctx->log;

  struct rdma_cm_event *event;
  int err;

  ctx->cm_connected = 0;
  ctx->ib_connected = 0;

  err = rdma_get_cm_event(ctx->cm_channel, &event);
  if (err)
  { 
    multilog(log, LOG_ERR, "disconnect: rdma_get_cm_event failed [%d]\n", err);
    return -1;
  } 

  if (event->event != RDMA_CM_EVENT_DISCONNECTED)
  { 
    multilog(log, LOG_ERR, "disconnect: rdma_get_cm_event returned %s event, expected "
                           "RDMA_CM_EVENT_DISCONNECTED\n", rdma_event_str(event->event));
    return -1;
  } 

  rdma_ack_cm_event(event);

  err = 0;

  if (ctx->verbose)
    multilog (log, LOG_INFO, "disconnect: ibv_destroy_qp\n"); 
  if (ctx->cm_id->qp)
  {
    if (ibv_destroy_qp(ctx->cm_id->qp)) {
      multilog(log, LOG_ERR, "disconnect: failed to destroy QP\n");
      err = 1;
    }
    ctx->cm_id->qp = 0;
  }

  if (ctx->verbose)
    multilog (log, LOG_INFO, "disconnect: ibv_destroy_cq\n"); 
  if (ctx->cq) {
    if (ibv_destroy_cq(ctx->cq)) {
      multilog(log, LOG_ERR, "disconnect: failed to destroy CQ\n");
      err = 1;
    }
    ctx->cq = 0;
  }


  if (ctx->bufs) 
  {
    unsigned i=0;
    for (i=0; i<ctx->nbufs; i++)
    {
      if (ctx->verbose > 1)
        multilog (log, LOG_INFO, "disconnect: dada_ib_dereg_buffer bufs[%d]\n", i);
      if (dada_ib_dereg_buffer(ctx->bufs[i]))
      {
        multilog(log, LOG_ERR, "disconnect: failed to deregister MR[%d]\n", i);
        err = 1;
      }
    }
  }

  if (ctx->header_mb)
  {
    if (dada_ib_dereg_buffer(ctx->header_mb) < 0)
    {
      multilog(log, LOG_ERR, "disconnect: failed to deregister header_mb\n");
      err = 1;
    }
  }
  ctx->header_mb = 0;

  if (ctx->header)
    free(ctx->header);
  ctx->header = 0;

  if (ctx->sync_to)
  {
    if (ctx->verbose > 1)
      multilog (log, LOG_INFO, "disconnect: dada_ib_dereg_buffer sync_to\n");
    if (dada_ib_dereg_buffer(ctx->sync_to))
    {
      multilog(log, LOG_ERR, "disconnect: failed to deregister sync_to MR\n");
      err = 1;
    }
    ctx->sync_to = 0;
  }
  if (ctx->sync_to_val)
    free(ctx->sync_to_val);
  ctx->sync_to_val = 0;

  if (ctx->sync_from)
  {
    if (ctx->verbose > 1)
      multilog (log, LOG_INFO, "disconnect: dada_ib_dereg_buffer sync_from\n");
    if (dada_ib_dereg_buffer(ctx->sync_from))
    {
      multilog(log, LOG_ERR, "disconnect: failed to deregister sync_from MR\n");
      err = 1;
    }
    ctx->sync_from = 0;
  }
  if (ctx->sync_from_val)
    free(ctx->sync_from_val);
  ctx->sync_from_val = 0;

  if (ctx->comp_chan) 
  {
    if (ctx->verbose > 1)
      multilog (log, LOG_INFO, "disconnect: ibv_destroy_comp_channel()\n");
    if (ibv_destroy_comp_channel(ctx->comp_chan)) 
    {
      multilog(log, LOG_ERR, "disconnect: failed to destroy completion channel\n");
      err = 1;
    }
    ctx->comp_chan = 0;
  }

  if (ctx->pd) 
  {
    if (ctx->verbose > 1)
      multilog (log, LOG_INFO, "disconnect: ibv_dealloc_pd()\n");
    if (ibv_dealloc_pd(ctx->pd)) 
    {
      multilog(log, LOG_ERR, "disconnect: failed to deallocate PD\n");
      err = 1;
    }
    ctx->pd = 0;
  }


  if (ctx->cm_id)
  {
    if (ctx->verbose > 1)
      multilog (log, LOG_INFO, "disconnect: rdma_destroy_id()\n");
    if (rdma_destroy_id(ctx->cm_id))
    {
      multilog(log, LOG_ERR, "disconnect: failed to destroy CM ID\n");
      err = 1;
    }
    ctx->cm_id = 0;
  }

  if (ctx->cm_channel)
  {
    if (ctx->verbose > 1)
      multilog (log, LOG_INFO, "disconnect: rdma_destroy_event_channel()\n");
    rdma_destroy_event_channel(ctx->cm_channel);
    ctx->cm_channel = 0;
  }

  return err;
}

/*
 * clean up IB resources 
 */
int dada_ib_destroy (dada_ib_cm_t * ctx)
{

  int err;

  assert(ctx);
  multilog_t * log = ctx->log;

  if (ctx->verbose)
    multilog (log, LOG_INFO, "dada_ib_destroy()\n");

  if (dada_ib_disconnect(ctx) < 0)
  {
    multilog(log, LOG_ERR, "destroy: dada_ib_disconnect failed()\n");    
    err = 1;    
  }

  if (ctx->local_blocks)
    free(ctx->local_blocks);
  ctx->local_blocks = 0;

  if (ctx->bufs)
    free(ctx->bufs);
  ctx->bufs = 0;

  if (ctx->verbose > 1)
    multilog (log, LOG_INFO, "destroy: success\n");

  ctx->log = 0;
  free(ctx);

  return err;
}

