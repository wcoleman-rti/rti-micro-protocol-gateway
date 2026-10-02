/*
 * (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software. Licensee has the right to distribute object form only
 * for use with RTI products. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose. RTI is under no
 * obligation to maintain or support the Software. RTI shall not be liable for any
 * incidental or consequential damages arising out of the use or inability to use
 * the software.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "pgw/can_socketcan.h"
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"
#define REDA_SEQUENCE_USER_API
#define T PGW_CANSocketFilterDefinition
#define TSeq PGW_CANSocketFilterSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

static PGW_Status failure(PGW_CANSocket *s, bool sending)
{
    s->last_errno = errno;
    if (errno == EAGAIN || errno == EWOULDBLOCK ||
        (sending && errno == ENOBUFS)) return sending ? PGW_BACKPRESSURE : PGW_NO_DATA;
    if (sending) ++s->send_errors;
    else ++s->receive_errors;
    return PGW_IO_ERROR;
}

static PGW_Status receive(void *state, PGW_CANFrame *out)
{
    PGW_CANSocket *s = state;
    struct canfd_frame raw;
    struct iovec iov = {&raw, sizeof(raw)};
    union {
        struct cmsghdr align;
        unsigned char bytes[CMSG_SPACE(sizeof(struct timespec)) +
                            CMSG_SPACE(sizeof(uint32_t))];
    } control;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    ssize_t n;
    memset(&msg, 0, sizeof(msg));
    memset(&raw, 0, sizeof(raw));
    msg.msg_iov = &iov; msg.msg_iovlen = 1;
    msg.msg_control = control.bytes; msg.msg_controllen = sizeof(control.bytes);
    n = recvmsg(s->fd, &msg, MSG_DONTWAIT);
    if (n < 0) return failure(s, false);
    if ((n != CAN_MTU && n != CANFD_MTU) || (msg.msg_flags & MSG_TRUNC)) {
        ++s->receive_errors;
        s->last_errno = EMSGSIZE;
        return PGW_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->id = raw.can_id & CAN_EFF_MASK;
    out->length = raw.len;
    out->interface_index = s->interface_index;
    if (raw.can_id & CAN_EFF_FLAG) out->flags |= PGW_CAN_FLAG_EXTENDED;
    if (raw.can_id & CAN_RTR_FLAG) out->flags |= PGW_CAN_FLAG_RTR;
    if (raw.can_id & CAN_ERR_FLAG) out->flags |= PGW_CAN_FLAG_ERROR;
    if (msg.msg_flags & MSG_CONFIRM) out->flags |= PGW_CAN_FLAG_ECHO;
    if (n == CANFD_MTU) {
        out->flags |= PGW_CAN_FLAG_FD;
        if (raw.flags & CANFD_BRS) out->flags |= PGW_CAN_FLAG_BRS;
        if (raw.flags & CANFD_ESI) out->flags |= PGW_CAN_FLAG_ESI;
    }
    if (out->length > sizeof(out->data)) {
        ++s->receive_errors;
        return PGW_INVALID;
    }
    memcpy(out->data, raw.data, out->length);
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET) continue;
        if (cmsg->cmsg_type == SO_TIMESTAMPNS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(struct timespec))) {
            struct timespec t;
            memcpy(&t, CMSG_DATA(cmsg), sizeof(t));
            out->timestamp = (PGW_Timestamp){true, true, t.tv_sec,
                                           (uint32_t)t.tv_nsec};
        } else if (cmsg->cmsg_type == SO_RXQ_OVFL &&
                   cmsg->cmsg_len >= CMSG_LEN(sizeof(uint32_t))) {
            memcpy(&s->kernel_rx_overflow, CMSG_DATA(cmsg), sizeof(uint32_t));
        }
    }
    return PGW_OK;
}

static PGW_Status send_frame(void *state, const PGW_CANFrame *frame)
{
    PGW_CANSocket *s = state;
    struct canfd_frame raw;
    size_t bytes;
    ssize_t n;
    if (!PGW_CANFrame_valid(frame) ||
        (frame->flags & (PGW_CAN_FLAG_RTR | PGW_CAN_FLAG_ERROR | PGW_CAN_FLAG_ECHO)) ||
        ((frame->flags & PGW_CAN_FLAG_FD) && !s->enable_fd)) return PGW_INVALID;
    memset(&raw, 0, sizeof(raw));
    raw.can_id = frame->id |
                 ((frame->flags & PGW_CAN_FLAG_EXTENDED) ? CAN_EFF_FLAG : 0);
    raw.len = frame->length;
    if (frame->flags & PGW_CAN_FLAG_BRS) raw.flags |= CANFD_BRS;
    if (frame->flags & PGW_CAN_FLAG_ESI) raw.flags |= CANFD_ESI;
    memcpy(raw.data, frame->data, frame->length);
    bytes = (frame->flags & PGW_CAN_FLAG_FD) ? CANFD_MTU : CAN_MTU;
    n = send(s->fd, &raw, bytes, MSG_DONTWAIT);
    if (n < 0) return failure(s, true);
    if ((size_t)n != bytes) {
        ++s->send_errors;
        s->last_errno = EIO;
        return PGW_IO_ERROR;
    }
    return PGW_OK;
}

static PGW_Status close_socket(void *state)
{
    PGW_CANSocket *s = state;
    int fd = s->fd;
    s->fd = -1;
    if (fd >= 0 && close(fd)) return failure(s, false);
    return PGW_OK;
}

PGW_Status PGW_CANSocket_open(PGW_CANSocket *s, const PGW_CANSocketConfig *cfg)
{
    struct sockaddr_can address;
    struct can_filter filters[64];
    int one = 1, own;
    RTI_INT32 i, filter_count;
    PGW_Status status;
    if (!s || !cfg || !cfg->interface_name || !cfg->filters_initialized)
        return PGW_INVALID;
    filter_count = PGW_CANSocketFilterSeq_get_length(&cfg->filters);
    if (filter_count > 64) return PGW_INVALID;
    memset(s, 0, sizeof(*s)); s->fd = -1;
    s->interface_index = if_nametoindex(cfg->interface_name);
    if (!s->interface_index) return failure(s, false);
    s->fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
    if (s->fd < 0) return failure(s, false);
    s->enable_fd = cfg->enable_fd;
    own = cfg->receive_own_messages ? 1 : 0;
    if (setsockopt(s->fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &own, sizeof(own)) ||
        setsockopt(s->fd, SOL_SOCKET, SO_TIMESTAMPNS, &one, sizeof(one)) ||
        setsockopt(s->fd, SOL_SOCKET, SO_RXQ_OVFL, &one, sizeof(one)) ||
        setsockopt(s->fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                   &cfg->error_mask, sizeof(cfg->error_mask)) ||
        (cfg->enable_fd &&
         setsockopt(s->fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &one, sizeof(one))))
        goto fail;
    for (i = 0; i < filter_count; ++i) {
        const PGW_CANSocketFilter *filter =
            PGW_CANSocketFilterSeq_get_reference(&cfg->filters, i);
        filters[i].can_id = filter->id;
        filters[i].can_mask = filter->mask;
    }
    /* No configured filters means the kernel default: receive all frames. */
    if (filter_count &&
        setsockopt(s->fd, SOL_CAN_RAW, CAN_RAW_FILTER, filters,
                   (socklen_t)((size_t)filter_count * sizeof(filters[0])))) goto fail;
    memset(&address, 0, sizeof(address));
    address.can_family = AF_CAN;
    address.can_ifindex = (int)s->interface_index;
    if (bind(s->fd, (struct sockaddr *)&address, sizeof(address))) goto fail;
    return PGW_OK;
fail:
    status = failure(s, false);
    close_socket(s);
    return status;
}

PGW_Status PGW_CANSocketConfig_finalize(PGW_CANSocketConfig *cfg)
{
    if (!cfg || !cfg->filters_initialized) return PGW_INVALID;
    if (cfg->filters_borrowed &&
        !PGW_CANSocketFilterSeq_unloan(&cfg->filters)) return PGW_LOAN_ERROR;
    if (!PGW_CANSocketFilterSeq_finalize(&cfg->filters)) return PGW_LOAN_ERROR;
    cfg->filters_initialized = false;
    cfg->filters_borrowed = false;
    return PGW_OK;
}

PGW_CANTransport PGW_CANSocket_transport(PGW_CANSocket *s)
{
    static const PGW_CANTransportI iface = {receive, send_frame, close_socket};
    PGW_CANTransport result = {s, &iface};
    return result;
}
