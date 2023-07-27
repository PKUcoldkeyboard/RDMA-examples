#ifndef SERVER_H_
#define SERVER_H_
#pragma once
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

/* RDMA管理资源声明 */
static struct rdma_event_channel *cm_channel = NULL;
static struct rdma_cm_id *cm_server_id = NULL, *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp = NULL;

/* RDMA内存资源声明 */
static struct ibv_mr *client_metadata_mr = NULL, *server_metadata_mr = NULL, *server_buffer_mr = NULL;
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
static struct ibv_recv_wr client_recv_wr, *bad_client_recv_wr = NULL;
static struct ibv_send_wr server_send_wr, *bad_server_send_wr = NULL;
static struct ibv_sge client_recv_sge, server_send_sge;




#endif // SERVER_H_