/*
 * Copyright (c) 2019, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <SIMCom/SIM7020/SIMCom_SIM7020_CellularStack.h>
#include "CellularLog.h"
#include "CellularUtil.h"
#include "stdlib.h"

#define MAX_SOCKET 5

using namespace mbed;

SIMCom_SIM7020_CellularStack::SIMCom_SIM7020_CellularStack(ATHandler       &atHandler,
                                                           int              cid,
                                                           nsapi_ip_stack_t stack_type) :
    AT_CellularStack(atHandler, cid, stack_type),
    _rx_buf_offset(0)
{
    _at.set_urc_handler("+CSONMI:",
                        mbed::Callback<void()>(this, &SIMCom_SIM7020_CellularStack::urc_csonmi));
    _at.set_urc_handler("+CSOERR:",
                        mbed::Callback<void()>(this, &SIMCom_SIM7020_CellularStack::urc_socket_closed));
}

SIMCom_SIM7020_CellularStack::~SIMCom_SIM7020_CellularStack()
{
    _at.set_urc_handler("+CSONMI:", NULL);
    _at.set_urc_handler("+CSOERR:", NULL);
}

nsapi_error_t SIMCom_SIM7020_CellularStack::socket_listen(nsapi_socket_t handle, int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

nsapi_error_t SIMCom_SIM7020_CellularStack::socket_accept(void *server, void **socket, SocketAddress *addr)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

nsapi_error_t SIMCom_SIM7020_CellularStack::socket_connect(nsapi_socket_t handle, const SocketAddress &address)
{
    CellularSocket *socket = static_cast<CellularSocket *>(handle);

    if (socket != NULL) {
        if (socket->id == -1) {
            _at.lock();
            const nsapi_error_t err = create_socket_impl(socket);
            _at.unlock();
            if (err != NSAPI_ERROR_OK) {
                return err;
            }
        }
    } else {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    _at.lock();
    _at.cmd_start("AT+CSOCON=");
    _at.write_int(socket->id - 1);
    _at.write_int(address.get_port());
    _at.write_string(address.get_ip_address());
    _at.cmd_stop_read_resp();
    _at.unlock();

    if (_at.get_last_error() == NSAPI_ERROR_OK) {
        socket->remoteAddress = address;
        socket->connected     = true;

        return NSAPI_ERROR_OK;
    }

    return NSAPI_ERROR_NO_CONNECTION;
}

void SIMCom_SIM7020_CellularStack::urc_csonmi()
{
    int sock_id = _at.read_int();
    tr_debug("urc_csonmi sock id: %d", static_cast<int>(sock_id));

    CellularSocket *sock = find_socket(sock_id + 1);
    if (sock == NULL) {
        return;
    }

    if (sock->_cb == NULL) {
        return;
    }

    const nsapi_size_t pending_bytes = _at.read_int();

    /* Promote fast failure - verify following assumptions:
     * 1) rx buffer overflow detection
     * 2) rx buffer can only be occupied by data to 1 socket
     *
     * This is required as mbedOS provided chipset driver programming model is not ideal for
     * SIM7020 specific requirements regarding rx data path. */
    MBED_ASSERT(sock->pending_bytes == _rx_buf_offset);
    MBED_ASSERT((_rx_buf_offset + (pending_bytes / 2)) <= sizeof(_rx_buffer));

    const ssize_t read_bytes_err = _at.read_hex_string((char *)(_rx_buffer + _rx_buf_offset),
                                                       pending_bytes);
    if ((pending_bytes / 2) != static_cast<nsapi_size_t>(read_bytes_err)) {
        /* Promote fast failure detection - if t his branch is entered it most propably implies
         * rx buffer overflow below in the host MCU stack. */

        tr_error("P: %d", static_cast<int>(pending_bytes));
        tr_error("R: %d", static_cast<int>(read_bytes_err));

        MBED_ASSERT(false);
    }
    MBED_ASSERT(_at.get_last_error() == NSAPI_ERROR_OK);

    _rx_buf_offset      += (pending_bytes / 2);
    sock->pending_bytes += (pending_bytes / 2);
    MBED_ASSERT(sock->pending_bytes == _rx_buf_offset);

    tr_debug("urc_csonmi store pending bytes: %d\n", sock->pending_bytes);
    sock->_cb(sock->_data);
}

void SIMCom_SIM7020_CellularStack::urc_socket_closed()
{
    const int sock_id = _at.read_int();
    tr_debug("urc_socket_closed sock id: %d", sock_id);
    const int err = _at.read_int();
    tr_debug("urc_socket_closed err: %d", err);

    switch (err) {
            CellularSocket *sock;
        case 4:
            sock = find_socket(sock_id + 1);
            if (sock != NULL) {
                sock->closed = true;

                if (sock->_cb != NULL) {
                    sock->_cb(sock->_data);
                }
            }

            break;
        default:
            tr_debug("urc_socket_closed unhandler error code id: %d", err);
            /* @note: Add possible required implementation for device error code. */
            break;
    }
}

int SIMCom_SIM7020_CellularStack::get_max_socket_count()
{
    return MAX_SOCKET;
}

bool SIMCom_SIM7020_CellularStack::is_protocol_supported(nsapi_protocol_t protocol)
{
    return (protocol == NSAPI_UDP || protocol == NSAPI_TCP);
}

nsapi_error_t SIMCom_SIM7020_CellularStack::socket_close_impl(int sock_id)
{
    CellularSocket *sock = find_socket(sock_id);
    MBED_ASSERT(sock != NULL);

    if (sock->closed) {
        return NSAPI_ERROR_OK;
    }

    _at.cmd_start("AT+CSOCL=");
    _at.write_int(sock_id - 1);
    _at.cmd_stop_read_resp();

    return _at.get_last_error();
}

void SIMCom_SIM7020_CellularStack::handle_open_socket_response(int &modem_connect_id, int &err)
{
    MBED_ASSERT(false);
}

nsapi_error_t SIMCom_SIM7020_CellularStack::create_socket_impl(CellularSocket *socket)
{
    switch (socket->proto) {
        case NSAPI_UDP:
            _at.cmd_start("AT+CSOC=");
            _at.write_int(1);
            _at.write_int(2);
            _at.write_int(1);
            _at.write_int(_cid);
            _at.cmd_stop();

            break;
        case NSAPI_TCP:
            _at.cmd_start("AT+CSOC=");
            _at.write_int(1);
            _at.write_int(1);
            _at.write_int(1);
            _at.write_int(_cid);
            _at.cmd_stop();

            break;
        default:
            return NSAPI_ERROR_PARAMETER;
            break;
    }

    _at.resp_start("+CSOC:");
    socket->id = _at.read_int();
    _at.resp_stop();

    const nsapi_error_t err = _at.get_last_error();
    MBED_ASSERT(err == NSAPI_ERROR_OK);
    MBED_ASSERT(socket->id >= 0);

    // Check for duplicate socket id delivered by modem - should not never happen
    ++(socket->id);
    CellularSocket *sock;
    for (int i = 0; (i < get_max_socket_count()); ++i) {
        sock = _socket[i];
        if (sock && sock->id != -1 && sock->id == socket->id) {
            tr_error("Socket create failed: duplicate %d", socket->id);
            MBED_ASSERT(false);
        }
    }

    tr_debug("Socket create id: %d", socket->id);

    return NSAPI_ERROR_OK;
}

nsapi_size_or_error_t SIMCom_SIM7020_CellularStack::socket_sendto_impl(CellularSocket      *socket,
                                                                       const SocketAddress &address,
                                                                       const void          *data,
                                                                       nsapi_size_t         size)
{
    if ((size > MAX_SEND_SIZE) && (socket->proto == NSAPI_UDP)) {
        return NSAPI_ERROR_PARAMETER;
    }

    switch (socket->proto) {
            char *hexstr;
            int   hexlen;
        case NSAPI_UDP:
            if (socket->remoteAddress != address) {
                /* No existing connection endpoint setup in the modem for this remote peer, we need to create one here. */

                _address = address;
                _at.cmd_start("AT+CSOCON=");
                _at.write_int(socket->id - 1);
                _at.write_int(address.get_port());
                _at.write_string(address.get_ip_address());
                _at.cmd_stop();

                _at.resp_start("+CSOCON:");
                _at.resp_stop();

                if (_at.get_last_error() != NSAPI_ERROR_OK)  {
                    return NSAPI_ERROR_DEVICE_ERROR;
                }

                socket->remoteAddress = address;
            }

            // Tx sequence.

            hexstr         = new char[size * 2 + 1];
            hexlen         = mbed_cellular_util::char_str_to_hex_str((const char *)data, size, hexstr);
            hexstr[hexlen] = 0;

            _at.cmd_start("AT+CSOSEND=");
            _at.write_int(socket->id - 1);
            _at.write_int(hexlen);
            _at.write_string(hexstr, false);
            _at.cmd_stop_read_resp();

            delete [] hexstr;

            if (_at.get_last_error() != NSAPI_ERROR_OK)  {
                return NSAPI_ERROR_DEVICE_ERROR;
            }

            return size;

            break;
        case NSAPI_TCP:
            size   = (size > MAX_SEND_SIZE) ? MAX_SEND_SIZE : size;
            hexstr = new char[size * 2 + 1];
            hexlen = mbed_cellular_util::char_str_to_hex_str((const char *)data, size, hexstr);
            hexstr[hexlen] = 0;

            _at.cmd_start("AT+CSOSEND=");
            _at.write_int(socket->id - 1);
            _at.write_int(hexlen);
            _at.write_string(hexstr, false);
            _at.cmd_stop_read_resp();

            delete [] hexstr;

            if (_at.get_last_error() != NSAPI_ERROR_OK)  {
                return NSAPI_ERROR_DEVICE_ERROR;
            }

            return size;

            break;
        default:
            return NSAPI_ERROR_PARAMETER;
            break;
    }
}

nsapi_size_or_error_t SIMCom_SIM7020_CellularStack::socket_recvfrom_impl(CellularSocket *socket,
                                                                         SocketAddress  *address,
                                                                         void           *buffer,
                                                                         nsapi_size_t    size)
{
    // Read all availabe rx data from the modem to the supplied buffer.
    MBED_ASSERT(size >= socket->pending_bytes);

    tr_debug("socket_recvfrom_impl - input size: %d", static_cast<int>(size));
    tr_debug("socket_recvfrom_impl - socket id: %d\n", static_cast<int>(socket->id));
    tr_debug("socket_recvfrom_impl - pending_bytes: %d\n", static_cast<int>(socket->pending_bytes));

    const size_t rx_available = (socket->pending_bytes > size) ? size : socket->pending_bytes;

    if (rx_available == 0) {
        return NSAPI_ERROR_WOULD_BLOCK;
    }

    tr_debug("socket_recvfrom_impl - stack copy size: %d", static_cast<int>(rx_available));

    memcpy(buffer, _rx_buffer, rx_available);
    socket->pending_bytes = 0;
    _rx_buf_offset        = 0;

    if (address != NULL) {
        address->set_ip_address(_address.get_ip_address());
        address->set_port(_address.get_port());
    }

    return rx_available;
}
