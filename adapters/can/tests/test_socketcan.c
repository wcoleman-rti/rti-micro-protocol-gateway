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

#include "pgw/can_socketcan.h"
#include <assert.h>
#include <errno.h>

int main(void)
{
    PGW_CANSocket socket;
    PGW_CANSocketConfig config = {0};
    config.interface_name = "pgw-no-such-if";
    assert(PGW_CANSocketFilterSeq_initialize(&config.filters));
    config.filters_initialized = true;
    assert(PGW_CANSocket_open(&socket, &config) == PGW_IO_ERROR);
    assert(socket.fd == -1 && socket.last_errno != 0 && socket.receive_errors == 1);
    PGW_CANTransport transport = PGW_CANSocket_transport(&socket);
    PGW_CANFrame frame = {0};
    assert(transport.iface->receive(transport.state, &frame) == PGW_IO_ERROR);
    assert(socket.last_errno == EBADF);
    frame.length = 8; frame.id = 0x123;
    assert(transport.iface->send(transport.state, &frame) == PGW_IO_ERROR);
    assert(socket.last_errno == EBADF);
    frame.id = 0x800;
    assert(transport.iface->send(transport.state, &frame) == PGW_INVALID);
    assert(transport.iface->close(transport.state) == PGW_OK);
    PGW_CANSocketFilter filters[65] = {{0}};
    assert(PGW_CANSocketFilterSeq_loan_contiguous(&config.filters, filters, 65, 65));
    config.filters_borrowed = true;
    assert(PGW_CANSocket_open(&socket, &config) == PGW_INVALID);
    assert(PGW_CANSocketConfig_finalize(&config) == PGW_OK);
    return 0;
}
