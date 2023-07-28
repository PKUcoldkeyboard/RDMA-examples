#ifndef CLIENT_H
#define CLIENT_H
#pragma once
#include "utils.h"

/* RDMA管理资源声明 */
static struct rdma_event_channel *cm_channel = NULL;
static struct rdma_cm_id *cm_client_id = NULL;
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *client_cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp = NULL;

/* RDMA内存资源声明 */
static struct ibv_mr *client_metadata_mr = NULL,
                    *server_metadata_mr = NULL,
                    *client_src_mr = NULL,
                    *client_dst_mr = NULL;
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
static struct ibv_sge client_send_sge, server_recv_sge;

/* 源缓冲区和目标缓冲区 */
static char *src = NULL, *dst = NULL;

static int check_src_dst();
static int start_rdma_clilent(struct sockaddr_in *s_addr);
static int pre_post_recv();
static int connect_to_server();
static int exchange_metadata();
static int remote_memory_ops();
static int disconnect_and_cleanup();

#endif // CLIENT_H