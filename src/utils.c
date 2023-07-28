#include "utils.h"

int get_addr(char *dst, struct sockaddr *addr)
{
    int ret = -1;
    struct addrinfo *res;
    ret = getaddrinfo(dst, NULL, NULL, &res);
    if (ret)
    {
        log_err("getaddrinfo failed - invalid hostname or IP address");
        return ret;
    }
    memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(res);
    return ret;
}

int process_rdma_cm_event(struct rdma_event_channel *echannel,
                          enum rdma_cm_event_type expected_event,
                          struct rdma_cm_event **cm_event)
{
    int ret = 1;
    ret     = rdma_get_cm_event(echannel, cm_event);
    if (ret)
    {
        log_err("Failed to retrieve a cm event, errno: %d ", -errno);
        return -errno;
    }
    /* 是否为good event */
    if (0 != (*cm_event)->status)
    {
        log_err("CM event has non zero status: %d", (*cm_event)->status);
        ret = -((*cm_event)->status);
        rdma_ack_cm_event(*cm_event);
        return ret;
    }
    /* 是否为期望的event */
    if ((*cm_event)->event != expected_event)
    {
        log_err("Unexpected event received: %s [ expecting: %s ]",
                rdma_event_str((*cm_event)->event), rdma_event_str(expected_event));
        rdma_ack_cm_event(*cm_event);
        return -1;
    }

    debug("A new %s type event is received ", rdma_event_str((*cm_event)->event));
    return ret;
}

int process_work_completion_events(struct ibv_comp_channel *comp_channel,
                                   struct ibv_wc *wc,
                                   int max_wc)
{
    struct ibv_cq *cq_ptr = NULL;
    void *context         = NULL;
    int ret = -1, total_wc = 0;
    /* 等待完成队列中的事件 */
    ret = ibv_get_cq_event(comp_channel, &cq_ptr, &context);
    if (ret)
    {
        log_err("Failed to get cq event, errno: %d ", -errno);
        return -errno;
    }
    ret = ibv_req_notify_cq(cq_ptr, 0);
    if (ret)
    {
        log_err("Failed to request notifications on CQ, errno: %d ", -errno);
        return -errno;
    }
    total_wc = 0;
    do
    {
        ret = ibv_poll_cq(cq_ptr, max_wc - total_wc, wc + total_wc);
        if (ret < 0)
        {
            log_err("Failed to poll cq for wc, errno: %d ", -errno);
            return -errno;
        }
        total_wc += ret;
    } while (total_wc < max_wc);
    /* 检查完成队列中的状态与操作是否成功 */
    for (int i = 0; i < total_wc; i++)
    {
        if (wc[i].status != IBV_WC_SUCCESS)
        {
            log_err("Work completion (WC) has error status: %s ",
                    ibv_wc_status_str(wc[i].status));
            return -wc[i].status;
        }
    }
    ibv_ack_cq_events(cq_ptr, 1);
    return total_wc;
}

struct ibv_mr *rdma_buffer_alloc(struct ibv_pd *pd, uint32_t size, enum ibv_access_flags permission)
{
    struct ibv_mr *mr = NULL;
    if (!pd)
    {
        log_err("Protection domain is NULL");
        return NULL;
    }
    void *buf = calloc(1, size);
    if (!buf)
    {
        log_err("Failed to allocate buffer");
        return NULL;
    }
    debug("Buffer allocated: %p , size: %u ", buf, size);
    mr = rdma_buffer_register(pd, buf, size, permission);
    if (!mr)
    {
        free(buf);
    }
    return mr;
}

void rdma_buffer_free(struct ibv_mr *mr)
{
    if (!mr)
    {
        log_err("Memory region is NULL, ignoring ");
        return;
    }
    void *to_free = mr->addr;
    rdma_buffer_deregister(mr);
    debug("Buffer freed: %p ", to_free);
    free(to_free);
}

struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd,
                                    void *addr,
                                    uint32_t size,
                                    enum ibv_access_flags permission)
{
    struct ibv_mr *mr = NULL;
    if (!pd)
    {
        log_err("Protection domain is NULL, ignoring ");
        return NULL;
    }
    mr = ibv_reg_mr(pd, addr, size, permission);
    if (!mr)
    {
        log_err("Failed to register memory region, errno: %d ", -errno);
        return NULL;
    }
    debug("Registered: %p , len: %u , stag: 0x%x ", mr->addr, (unsigned int)mr->length,
              mr->lkey);
    return mr;
}

void rdma_buffer_deregister(struct ibv_mr *mr)
{
    if (!mr)
    {
        log_err("Memory region is NULL, ignoring ");
        return;
    }
    debug("Deregistered: %p , len: %u , stag : 0x%x ", mr->addr, (unsigned int)mr->length,
              mr->lkey);
    ibv_dereg_mr(mr);
}

void print_rdma_buffer_attr(struct rdma_buffer_attr *attr)
{
    if (!attr)
    {
        log_err("Buffer attribute is NULL, ignoring ");
        return;
    }
    printf("--------------------------------------\n");
    printf("Buffer attribute:\n");  
    printf("  Address: 0x%lx\n", attr->address);
    printf("  Length: %u\n", attr->length);
    printf("  Stag: 0x%x\n", attr->stag.local_stag);
    printf("--------------------------------------\n");
}