#include "client.h"

void usage()
{
    printf("Usage:\n");
    printf("    client -s string (required) [-a <server-address>] [-p <server-port>] \n");
    printf("default IP: 127.0.0.1, default port: %d\n", DEFAULT_PORT);
    exit(1);
}

static int start_rdma_client(struct sockaddr_in *s_addr)
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
    ret = rdma_create_id(cm_channel, &cm_client_id, NULL, RDMA_PS_TCP);
    if (ret)
    {
        log_err("Creating cm id failed with errno: %d ", -errno);
        return -errno;
    }
    ret = rdma_resolve_addr(cm_client_id, NULL, (struct sockaddr *)s_addr, 2000);
    if (ret)
    {
        log_err("Failed to resolve addr, errno: %d ", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ADDR_RESOLVED");
    ret = process_rdma_cm_event(cm_channel, RDMA_CM_EVENT_ADDR_RESOLVED, &cm_event);
    if (ret)
    {
        log_err("Failed to get cm event, ret: %d ", ret);
        return ret;
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret)
    {
        log_err("Failed to acknowledge the cm event, errno: %d ", -errno);
        return -errno;
    }
    debug("RDMA address is resolved ");

    ret = rdma_resolve_route(cm_client_id, 2000);
    if (ret)
    {
        log_err("Failed to resolve route, errno: %d ", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ROUTE_RESOLVED");
    ret = process_rdma_cm_event(cm_channel, RDMA_CM_EVENT_ROUTE_RESOLVED, &cm_event);
    if (ret)
    {
        log_err("Failed to get cm event, ret: %d ", ret);
        return ret;
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret)
    {
        log_err("Failed to acknowledge the cm event, errno: %d ", -errno);
        return -errno;
    }
    log_info("Trying to connect to server at %s:%d ", inet_ntoa(s_addr->sin_addr),
             ntohs(s_addr->sin_port));
    pd = ibv_alloc_pd(cm_client_id->verbs);
    if (!pd)
    {
        log_err("Failed to alloc pd, errno: %d ", -errno);
        return -errno;
    }
    debug("PD allocated at %p ", pd);
    io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
    if (!io_completion_channel)
    {
        log_err("Failed to create IO completion event channel, errno: %d ", -errno);
        return -errno;
    }
    debug("Completion event channel created at %p ", io_completion_channel);

    client_cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, NULL, io_completion_channel, 0);
    if (!client_cq)
    {
        log_err("Failed to create CQ, errno: %d ", -errno);
        return -errno;
    }
    debug("CQ created at %p with %d entries ", client_cq, client_cq->cqe);
    ret = ibv_req_notify_cq(client_cq, 0);
    if (ret)
    {
        log_err("Failed to request notifications on CQ, errno: %d ", -errno);
        return -errno;
    }
    bzero(&qp_init_attr, sizeof(qp_init_attr));
    qp_init_attr.qp_type          = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr  = MAX_WR;
    qp_init_attr.cap.max_recv_wr  = MAX_WR;
    qp_init_attr.cap.max_send_sge = MAX_SGE;
    qp_init_attr.cap.max_recv_sge = MAX_SGE;
    qp_init_attr.send_cq          = client_cq;
    qp_init_attr.recv_cq          = client_cq;

    ret = rdma_create_qp(cm_client_id, pd, &qp_init_attr);
    if (ret)
    {
        log_err("Failed to create QP, errno: %d ", -errno);
        return -errno;
    }

    client_qp = cm_client_id->qp;
    debug("QP created at %p ", client_qp);
    return 0;
}

static int pre_post_recv()
{
    int ret            = -1;
    server_metadata_mr = rdma_buffer_register(
        pd, &server_metadata_attr, sizeof(server_metadata_attr), (IBV_ACCESS_LOCAL_WRITE));

    if (!server_metadata_mr)
    {
        log_err("Failed to register server metadata buffer, -ENOMEM ");
        return -ENOMEM;
    }
    server_recv_sge.addr   = (uint64_t)server_metadata_mr->addr;
    server_recv_sge.length = (uint32_t)server_metadata_mr->length;
    server_recv_sge.lkey   = (uint32_t)server_metadata_mr->lkey;
    bzero(&server_recv_wr, sizeof(server_recv_wr));
    server_recv_wr.sg_list = &server_recv_sge;
    server_recv_wr.num_sge = 1;
    ret                    = ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr);
    if (ret)
    {
        log_err("Failed to pre-post recv buffer, errno: %d ", ret);
        return ret;
    }
    debug("Recv buffer pre-posted ");
    return 0;
}

static int connect_to_server()
{
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    int ret                        = -1;
    bzero(&conn_param, sizeof(conn_param));
    conn_param.initiator_depth     = 3;
    conn_param.retry_count         = 3;
    conn_param.responder_resources = 3;
    ret                            = rdma_connect(cm_client_id, &conn_param);
    if (ret)
    {
        log_err("Failed to connect to remote host, errno: %d ", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ESTABLISHED");
    ret = process_rdma_cm_event(cm_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event);
    if (ret)
    {
        log_err("Failed to get cm event, ret: %d ", ret);
        return ret;
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret)
    {
        log_err("Failed to acknowledge the cm event, errno: %d ", -errno);
        return -errno;
    }
    log_info("Connection established ");
    return 0;
}

static int exchange_metadata()
{
    struct ibv_wc wc[2];
    int ret       = -1;
    client_src_mr = rdma_buffer_register(
        pd, src, strlen(src),
        (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE));

    if (!client_src_mr)
    {
        log_err("Failed to register client src buffer, -ENOMEM ");
        return -ENOMEM;
    }
    client_metadata_attr.address         = (uint64_t)client_src_mr->addr;
    client_metadata_attr.length          = (uint32_t)client_src_mr->length;
    client_metadata_attr.stag.local_stag = client_src_mr->lkey;
    client_metadata_mr                   = rdma_buffer_register(
                          pd, &client_metadata_attr, sizeof(client_metadata_attr), (IBV_ACCESS_LOCAL_WRITE));
    if (!client_metadata_mr)
    {
        log_err("Failed to register client metadata buffer, ret = %d ", ret);
        return ret;
    }

    client_send_sge.addr   = (uint64_t)client_metadata_mr->addr;
    client_send_sge.length = (uint32_t)client_metadata_mr->length;
    client_send_sge.lkey   = client_metadata_mr->lkey;
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list    = &client_send_sge;
    client_send_wr.num_sge    = 1;
    client_send_wr.opcode     = IBV_WR_SEND;
    client_send_wr.send_flags = IBV_SEND_SIGNALED;

    ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
    if (ret)
    {
        log_err("Failed to post send, errno: %d ", -errno);
        return -errno;
    }

    ret = process_work_completion_events(io_completion_channel, wc, 2);
    if (ret != 2)
    {
        log_err("Failed to get 2 work completions, ret = %d ", ret);
        return ret;
    }
    debug("Server sent us buffer location and credentials ");
    print_rdma_buffer_attr(&server_metadata_attr);
    return 0;
}

static int remote_memory_ops()
{
    struct ibv_wc wc;
    int ret       = -1;
    client_dst_mr = rdma_buffer_register(
        pd, dst, strlen(dst),
        (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE));
    if (!client_dst_mr)
    {
        log_err("Failed to register client dst buffer, -ENOMEM ");
        return -ENOMEM;
    }
    client_send_sge.addr   = (uint64_t)client_src_mr->addr;
    client_send_sge.length = (uint32_t)client_src_mr->length;
    client_send_sge.lkey   = client_src_mr->lkey;
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list             = &client_send_sge;
    client_send_wr.num_sge             = 1;
    client_send_wr.opcode              = IBV_WR_RDMA_WRITE;
    client_send_wr.send_flags          = IBV_SEND_SIGNALED;
    client_send_wr.wr.rdma.rkey        = server_metadata_attr.stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;

    ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
    if (ret)
    {
        log_err("Failed to post send, errno: %d ", -errno);
        return -errno;
    }

    ret = process_work_completion_events(io_completion_channel, &wc, 1);

    if (ret != 1)
    {
        log_err("Failed to get 1 work completions, ret = %d ", ret);
        return ret;
    }

    debug("Client side WRITE is completed ");
    client_send_sge.addr   = (uint64_t)client_dst_mr->addr;
    client_send_sge.length = (uint32_t)client_dst_mr->length;
    client_send_sge.lkey   = client_dst_mr->lkey;
    bzero(&client_send_wr, sizeof(client_send_wr));
    client_send_wr.sg_list             = &client_send_sge;
    client_send_wr.num_sge             = 1;
    client_send_wr.opcode              = IBV_WR_RDMA_READ;
    client_send_wr.send_flags          = IBV_SEND_SIGNALED;
    client_send_wr.wr.rdma.rkey        = server_metadata_attr.stag.remote_stag;
    client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;
    ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
    if (ret)
    {
        log_err("Failed to post send, errno: %d ", -errno);
        return -errno;
    }

    ret = process_work_completion_events(io_completion_channel, &wc, 1);
    if (ret != 1)
    {
        log_err("Failed to get 1 work completions, ret = %d ", ret);
        return ret;
    }
    debug("Client side READ is completed ");
    return 0;
}

static int disconnect_and_cleanup()
{
    struct rdma_cm_event *cm_event = NULL;
    int ret                        = -1;
    ret                            = rdma_disconnect(cm_client_id);
    if (ret)
    {
        log_err("Failed to disconnect, errno: %d ", -errno);
    }

    ret = process_rdma_cm_event(cm_channel, RDMA_CM_EVENT_DISCONNECTED, &cm_event);
    if (ret)
    {
        log_err("Failed to get disconnect event, ret = %d ", ret);
    }

    ret = rdma_ack_cm_event(cm_event);
    if (ret)
    {
        log_err("Failed to acknowledge cm event, errno: %d ", -errno);
    }

    rdma_destroy_qp(cm_client_id);

    ret = rdma_destroy_id(cm_client_id);
    if (ret)
    {
        log_err("Failed to destroy cm id, errno: %d ", -errno);
    }

    ret = ibv_destroy_cq(client_cq);
    if (ret)
    {
        log_err("Failed to destroy client cq, errno: %d ", -errno);
    }

    ret = ibv_destroy_comp_channel(io_completion_channel);
    if (ret)
    {
        log_err("Failed to destroy client completion channel, errno: %d ", -errno);
    }

    rdma_buffer_deregister(client_src_mr);
    rdma_buffer_deregister(client_dst_mr);
    rdma_buffer_deregister(server_metadata_mr);
    rdma_buffer_deregister(client_metadata_mr);
    free(src);
    free(dst);

    ret = ibv_dealloc_pd(pd);
    if (ret)
    {
        log_err("Failed to deallocate pd, errno: %d ", -errno);
    }
    rdma_destroy_event_channel(cm_channel);
    log_info("Client side resources are cleaned up successfully ");
    return 0;
}

static int check_src_dst()
{
    return memcmp((void *)src, (void *)dst, strlen(src));
}

int main(int argc, char **argv)
{
    struct sockaddr_in server_sockaddr;
    int ret, option;
    bzero(&server_sockaddr, sizeof(server_sockaddr));
    server_sockaddr.sin_family      = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    src = dst = NULL;
    while ((option = getopt(argc, argv, "s:a:p:")) != -1)
    {
        switch (option)
        {
            case 's':
                log_info("send string: %s, len: %u", optarg, (unsigned int)strlen(optarg));
                src = calloc(strlen(optarg), 1);
                if (!src)
                {
                    log_err("Failed to allocate src memory : -ENOMEM");
                    return -ENOMEM;
                }
                strncpy(src, optarg, strlen(optarg));
                dst = calloc(strlen(optarg), 1);
                if (!dst)
                {
                    log_err("Failed to allocate dst memory : -ENOMEM");
                    free(src);
                    return -ENOMEM;
                }
                break;

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

    if (!server_sockaddr.sin_port)
    {
        server_sockaddr.sin_port = htons(DEFAULT_PORT);
    }

    if (src == NULL)
    {
        log_err("Should specify the string to send");
        usage();
    }

    ret = start_rdma_client(&server_sockaddr);
    if (ret)
    {
        log_err("RDMA client failed to start cleanly, ret = %d ", ret);
        return ret;
    }
    ret = pre_post_recv();
    if (ret)
    {
        log_err("Failed to pre-post recv, ret = %d ", ret);
        return ret;
    }
    ret = connect_to_server();
    if (ret)
    {
        log_err("Failed to connect to server, ret = %d ", ret);
        return ret;
    }

    ret = exchange_metadata();
    if (ret)
    {
        log_err("Failed to exchange metadata, ret = %d ", ret);
        return ret;
    }

    ret = remote_memory_ops();
    if (ret)
    {
        log_err("Failed to perform remote memory ops, ret = %d ", ret);
        return ret;
    }

    if (check_src_dst())
    {
        log_err("src and dst buffers don't match");
    }
    else
    {
        log_info("src and dst buffers match");
    }

    ret = disconnect_and_cleanup();
    if (ret)
    {
        log_err("Failed to disconnect and cleanup resources, ret = %d ", ret);
        return ret;
    }

    return ret;
}