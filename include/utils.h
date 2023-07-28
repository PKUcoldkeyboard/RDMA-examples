#ifndef UTILS_H_
#define UTILS_H_
#pragma once
#include <arpa/inet.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "dbg.h"
#include "const.h"

/** __attribute__((__packed__))表示取消对齐 */
struct __attribute__((__packed__)) rdma_buffer_attr
{
    uint64_t address;
    uint32_t length;
    union stag
    {
        uint32_t local_stag;
        uint32_t remote_stag;
    } stag;
};

/**
 * @brief: 获取目的RDMA地址
 * @param: dst 目的IP地址
 * @param: addr RDMA地址
 * @return: 0表示成功，否则表示失败
 */
int get_addr(char *dst, struct sockaddr *addr);

/**
 *  @brief: 处理RDMA连接管理事件
 *  @param: echannel RDMA事件通道
 *  @param: expected_event 期望的事件类型
 *  @param: cm_event RDMA连接管理事件
 *  @return: 0表示成功，否则表示失败
 */
int process_rdma_cm_event(struct rdma_event_channel *echannel,
                          enum rdma_cm_event_type expected_event,
                          struct rdma_cm_event **cm_event);

/**
 * @brief: 分配大小为 "length "的 RDMA 缓冲区，权限为 permission,
 * 函数还将注册内存，并返回一个内存区域 (MR)。
 * @param: pd 应分配缓冲区的保护域
 * @param: size 缓冲区大小
 * @param: permission 枚举 ibv_access_flags 所定义的 IBV_ACCESS_* 权限的 OR 组合
 * @return: 标识符，错误时则为 NULL
 *
 */
struct ibv_mr *rdma_buffer_alloc(struct ibv_pd *pd,
                                 uint32_t size,
                                 enum ibv_access_flags permission);

/**
 * @brief: 释放先前分配的 RDMA 缓冲区。该缓冲区必须由rdma_buffer_alloc()分配。
 * @param: mr 即将释放的内存区域。
 */
void rdma_buffer_free(struct ibv_mr *mr);

/**
 * @brief: 注册内存区域，返回一个内存区域 (MR)。
 * @param: pd 应分配缓冲区的保护域
 * @param: addr 缓存区地址
 * @param: size 缓冲区大小
 * @param: permission 枚举 ibv_access_flags 所定义的 IBV_ACCESS_* 权限的 OR 组合
 * @return: 内存区域 (MR) 标识符，错误时则为 NULL
 */
struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd,
                                    void *addr,
                                    uint32_t size,
                                    enum ibv_access_flags permission);

/**
 * @brief: 注销先前注册的内存区域。
 * @param: mr 即将注销的内存区域。
 */
void rdma_buffer_deregister(struct ibv_mr *mr);

/**
 * @brief: 处理工作完成 (WC) 通知
 * @param: comp_channel 工作完成通道
 * @param: wc 工作完成事件
 * @param: max_wc 最大工作完成事件数
 */
int process_work_completion_events(struct ibv_comp_channel *comp_channel,
                                   struct ibv_wc *wc,
                                   int max_wc);

void print_rdma_buffer_attr(struct rdma_buffer_attr *attr);

#endif  // UTILS_H_