#include "server.h"

void usage()
{
    printf("Usage:");
    printf("    server [-a <server-address>] [-p <server-port>]");
    printf("default port: %d", DEFAULT_PORT);
    exit(1);
}

static int start_rdma_server(struct sockaddr_in *server_addr)
{
    struct rdma_cm_event *cm_event = NULL;
    int ret                        = -1;
    cm_channel                     = rdma_create_event_channel();
    if (!cm_channel)
    {
        log_err("Creating cm event channel failed with errno: %d ", -errno);
        return -errno;
    }
    debug("RDMA CM event channel is created at %p ", cm_channel);
    ret = rdma_create_id(cm_channel, &cm_server_id, NULL, RDMA_PS_TCP);
    if (ret)
    {
        log_err("Creating cm id failed with errno: %d ", -errno);
        return -errno;
    }
    debug("RDMA CM id is created at %p ", cm_server_id);
    ret = rdma_bind_addr(cm_server_id, (struct sockaddr *)server_addr);
    if (ret)
    {
        log_err("Failed to bind server address, errno: %d ", -errno);
        return -errno;
    }
    debug("Server RDMA CM id is successfully binded ");
    /* backlog = 8, same as TCP */
    ret = rdma_listen(cm_server_id, 8);
    if (ret)
    {
        log_err("Failed to listen on RDMA CM id, errno: %d ", -errno);
        return -errno;
    }
    log_info("Server is listening successfully at: %s , port: %d ",
             inet_ntoa(server_addr->sin_addr), ntohs(server_addr->sin_port));
    ret = process_rdma_cm_event(cm_channel, RDMA_CM_EVENT_CONNECT_REQUEST, &cm_event);
    if (ret)
    {
        log_err("Failed to get cm event, ret: %d ", ret);
        return ret;
    }
    cm_client_id = cm_event->id;
    ret          = rdma_ack_cm_event(cm_event);
    if (ret)
    {
        log_err("Failed to acknowledge the cm event, errno: %d ", -errno);
        return -errno;
    }
    debug("Client RDMA CM client is created at %p ", cm_client_id);
    return ret;
}

static int init_client_resources()
{
    int ret = -1;
    if (!cm_client_id)
    {
        log_err("Client id is not created");
        return -EINVAL;
    }
    /*
     * 通过一个合理的连接标识符cm_client_id，创建PD、QP、MR、CQ等资源
     */
    pd = ibv_alloc_pd(cm_client_id->verbs);
    if (!pd)
    {
        log_err("Failed to allocate PD, errno: %d ", -errno);
        return -errno;
    }
    debug("PD is created at %p ", pd);

    io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
    if (!io_completion_channel)
    {
        log_err("Failed to create IO completion event channel, errno: %d ", -errno);
        return -errno;
    }
    debug("Completion channel is created at %p ", io_completion_channel);

    cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, NULL, io_completion_channel, 0);
    if (!cq)
    {
        log_err("Failed to create CQ, errno: %d ", -errno);
        return -errno;
    }
    debug("CQ is created at %p with %d entries ", cq, cq->cqe);

    ret = ibv_req_notify_cq(cq, 0);
    if (ret)
    {
        log_err("Failed to request notifications on CQ, errno: %d ", -errno);
        return -errno;
    }

    /* 创建QP */
    bzero(&qp_init_attr, sizeof(qp_init_attr));
    qp_init_attr.cap.max_send_wr  = MAX_WR;
    qp_init_attr.cap.max_recv_wr  = MAX_WR;
    qp_init_attr.cap.max_send_sge = MAX_SGE;
    qp_init_attr.cap.max_recv_sge = MAX_SGE;
    qp_init_attr.send_cq          = cq;
    qp_init_attr.recv_cq          = cq;
    qp_init_attr.qp_type          = IBV_QPT_RC;

    ret = rdma_create_qp(cm_client_id, pd, &qp_init_attr);
    if (ret)
    {
        log_err("Failed to create QP, errno: %d ", -errno);
        return -errno;
    }
    client_qp = cm_client_id->qp;
    debug("Client QP is created at %p ", client_qp);
    return ret;
}

static int accept_client_connection()
{
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    struct sockaddr_in remote_sockaddr;
    int ret = -1;
    if (!cm_client_id || !client_qp)
    {
        log_err("Client resources are not initialized");
        return -EINVAL;
    }
    client_metadata_mr = rdma_buffer_register(pd, &client_metadata_attr, sizeof(client_metadata_attr),
                                              (IBV_ACCESS_LOCAL_WRITE));

    if (!client_metadata_mr)
    {
        log_err("Failed to register client metadata buffer");
        return -ENOMEM;
    }
    client_recv_sge.addr     = (uint64_t)client_metadata_mr->addr;
    client_recv_sge.length = client_metadata_mr->length;
    client_recv_sge.lkey   = client_metadata_mr->lkey;
    bzero(&conn_param, sizeof(conn_param));
    client_recv_wr.sg_list = &client_recv_sge;
    client_recv_wr.num_sge = 1;
    ret                    = ibv_post_recv(client_qp, &client_recv_wr, &bad_client_recv_wr);
    if (ret)
    {
        log_err("Failed to pre-post the receive buffer, errno: %d", ret);
        return ret;
    }
    debug("Receive buffer is pre-posted successfully");
    memset(&conn_param, 0, sizeof(conn_param));
    conn_param.initiator_depth     = 3;
    conn_param.responder_resources = 3;
    ret                            = rdma_accept(cm_client_id, &conn_param);
    if (ret)
    {
        log_err("Failed to accept the connection request, errno: %d", -errno);
        return -errno;
    }
    debug("Waiting for: RDMA_CM_EVENT_ESTABLISHED event ...");
    ret = process_rdma_cm_event(cm_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event);
    if (ret)
    {
        log_err("Failed to receive the cm event, ret = %d", ret);
        return ret;
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret)
    {
        log_err("Failed to acknowledge the cm event, errno: %d", -errno);
        return -errno;
    }
    memcpy(&remote_sockaddr, rdma_get_peer_addr(cm_client_id), sizeof(struct sockaddr_in));
    log_info("A new connection is accepted from: %s", inet_ntoa(remote_sockaddr.sin_addr));
    return ret;
}

static int send_server_metadata()
{
    struct ibv_wc wc;
    int ret = -1;
    ret     = process_work_completion_events(io_completion_channel, &wc, 1);
    if (ret != 1)
    {
        log_err("Failed to receive the work completion, ret = %d", ret);
        return ret;
    }
    log_info("Client side buffer information is received...");
    print_rdma_buffer_attr(&client_metadata_attr);
    log_info("The client has requested buffer length of: %u bytes", client_metadata_attr.length);

    server_buffer_mr = rdma_buffer_alloc(
        pd, client_metadata_attr.length,
        (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE));
    if (!server_buffer_mr)
    {
        log_err("Failed to allocate server buffer");
        return -ENOMEM;
    }

    server_metadata_attr.address         = (uint64_t)server_buffer_mr->addr;
    server_metadata_attr.length          = (uint32_t)server_buffer_mr->length;
    server_metadata_attr.stag.local_stag = (uint32_t)server_buffer_mr->lkey;
    server_metadata_mr                   = rdma_buffer_register(
                          pd, &server_metadata_attr, sizeof(server_metadata_attr), (IBV_ACCESS_LOCAL_WRITE));
    if (!server_metadata_mr)
    {
        log_err("Failed to register server metadata buffer");
        return -ENOMEM;
    }

    server_send_sge.addr   = (uint64_t) &server_metadata_attr;
    server_send_sge.length = sizeof(server_metadata_attr);
    server_send_sge.lkey   = server_metadata_mr->lkey;
    bzero(&server_send_wr, sizeof(server_send_wr));
    server_send_wr.sg_list    = &server_send_sge;
    server_send_wr.num_sge    = 1;
    server_send_wr.opcode     = IBV_WR_SEND;
    server_send_wr.send_flags = IBV_SEND_SIGNALED;

    ret = ibv_post_send(client_qp, &server_send_wr, &bad_server_send_wr);
    if (ret)
    {
        log_err("Failed to send the server metadata, errno: %d", -errno);
        return -errno;
    }
    debug("Server metadata is sent successfully");
    return 0;
}

static int disconnect_and_cleanup()
{
    struct rdma_cm_event *cm_event = NULL;
    int ret                        = -1;
    debug("Wait for: RDMA_CM_EVENT_DISCONNECTED event ...");
    ret = process_rdma_cm_event(cm_client_id->channel, RDMA_CM_EVENT_DISCONNECTED, &cm_event);

    if (ret)
    {
        log_err("Failed to receive the cm event, ret = %d", ret);
        return ret;
    }

    ret = rdma_ack_cm_event(cm_event);
    if (ret)
    {
        log_err("Failed to acknowledge the cm event, errno: %d", -errno);
        return -errno;
    }

    log_info("A disconnect event is received from client");

    rdma_destroy_qp(cm_client_id);
    ret = rdma_destroy_id(cm_client_id);
    if (ret)
    {
        log_err("Failed to destroy the cm id, errno: %d", -errno);
    }
    ret = ibv_destroy_cq(cq);
    if (ret)
    {
        log_err("Failed to destroy the cq, errno: %d", -errno);
    }

    ret = ibv_destroy_comp_channel(io_completion_channel);
    if (ret)
    {
        log_err("Failed to destroy the completion channel, errno: %d", -errno);
    }

    rdma_buffer_free(server_buffer_mr);
    rdma_buffer_deregister(server_metadata_mr);
    rdma_buffer_deregister(client_metadata_mr);
    ret = ibv_dealloc_pd(pd);
    if (ret)
    {
        log_err("Failed to deallocate the pd, errno: %d", -errno);
    }

    ret = rdma_destroy_id(cm_server_id);
    if (ret)
    {
        log_err("Failed to destroy the cm id, errno: %d", -errno);
    }
    rdma_destroy_event_channel(cm_channel);
    log_info("Server Shutdown complete.");
    return 0;
}

int main(int argc, char **argv)
{
    int ret, option;
    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof(server_sockaddr));
    /* AF_INET: IPv4, SOCK_STREAM: TCP */
    server_sockaddr.sin_family = AF_INET;
    /* 0.0.0.0: listen on all interfaces */
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    /* 解析命令行参数 */
    while ((option = getopt(argc, argv, "a:p:")) != -1)
    {
        switch (option)
        {
            case 'a':
                ret = get_addr(optarg, (struct sockaddr *)&server_sockaddr);
                if (ret)
                {
                    log_err("Invalid IP address or hostname");
                    return ret;
                }
                break;
            case 'p':
                server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0)); 
                break;
            default:
                usage();
                break;
        }
    }
    // 检查是否缺少 -a 参数
    if (server_sockaddr.sin_addr.s_addr == 0)
    {
        log_info("Server address is not specified, listen on all interfaces ");
    }

    if (!server_sockaddr.sin_port)
    {
        log_info("Server port is not specified, use default port: %d ", DEFAULT_PORT);
        server_sockaddr.sin_port = htons(DEFAULT_PORT);
    }
    ret = start_rdma_server(&server_sockaddr);
    if (ret)
    {
        log_err("RDMA server failed to start cleanly, ret = %d ", ret);
        return ret;
    }
    ret = init_client_resources();
    if (ret)
    {
        log_err("Failed to initialize client resources, ret = %d ", ret);
        return ret;
    }
    ret = accept_client_connection();
    if (ret)
    {
        log_err("Failed to accept client connection, ret = %d ", ret);
        return ret;
    }
    ret = send_server_metadata();
    if (ret)
    {
        log_err("Failed to send server metadata, ret = %d ", ret);
        return ret;
    }
    ret = disconnect_and_cleanup();
    if (ret)
    {
        log_err("Failed to disconnect and cleanup resources, ret = %d ", ret);
        return ret;
    }
}