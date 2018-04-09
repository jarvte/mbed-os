/*
 * Copyright (c) 2017, Arm Limited and affiliates.
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

#include <cstdio>
#include "rtos/Thread.h"
//#define MBED_TRACE_MAX_LEVEL TRACE_LEVEL_DEBUG
#include "GEMALTO_CINTERION_CellularStack.h"
#include "CellularLog.h"

#define SOCKET_MAX 10
#define UDP_PACKET_SIZE 1460 // from Cinterion AT manual

#define CONNECTION_PROFILE_ID 0

using namespace mbed;

GEMALTO_CINTERION_CellularStack::GEMALTO_CINTERION_CellularStack(ATHandler &atHandler, const char *apn, 
    int cid, nsapi_ip_stack_t stack_type) : AT_CellularStack(atHandler, cid, stack_type), _connectionProfileID(-1),
    _apn(apn)
{
    _at.set_urc_handler("^SISW:", mbed::Callback<void()>(this, &GEMALTO_CINTERION_CellularStack::urc_sisw));
    _at.set_urc_handler("^SISR:", mbed::Callback<void()>(this, &GEMALTO_CINTERION_CellularStack::urc_sisr));
}

GEMALTO_CINTERION_CellularStack::~GEMALTO_CINTERION_CellularStack()
{
}

nsapi_error_t GEMALTO_CINTERION_CellularStack::socket_listen(nsapi_socket_t handle, int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

nsapi_error_t GEMALTO_CINTERION_CellularStack::socket_accept(void *server, void **socket, SocketAddress *addr)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

void GEMALTO_CINTERION_CellularStack::urc_sisw()
{
    int sock_id = _at.read_int();
    int urc_code = _at.read_int();
    int err = _at.read_int();
    tr_info("TX event: socket=%d, urc=%d, err=%d", sock_id, urc_code, err);
    for (int i = 0; i < get_max_socket_count(); i++) {
        CellularSocket *sock = _socket[i];
        if (sock && sock->id == sock_id) {
            if (urc_code == 1) { // data available
                if (sock->_cb) {
                    //sock->rx_avail = true;
                    sock->_cb(sock->_data);
                }
            } else if (urc_code == 2) { // socket closed
                sock->created = false;
            }
            break;
        }
    }
}

void GEMALTO_CINTERION_CellularStack::urc_sisr()
{
    int sock_id= _at.read_int();
    int urc_code = _at.read_int();
    (void)urc_code;
    int err = _at.read_int();
    (void)err;
    tr_info("RX event: socket=%d, urc=%d, err=%d", sock_id, urc_code, err);
    for (int i = 0; i < get_max_socket_count(); i++) {
        CellularSocket *sock = _socket[i];
        if (sock && sock->id == sock_id) {
            if (urc_code == 1) { // data available
                if (sock->_cb) {
                    sock->rx_avail = true;
                    sock->_cb(sock->_data);
                }
            } else if (urc_code == 2) { // socket closed
                sock->created = false;
            }
            break;
        }
    }
}

int GEMALTO_CINTERION_CellularStack::get_max_socket_count()
{
    return SOCKET_MAX;
}

int GEMALTO_CINTERION_CellularStack::get_max_packet_size()
{
    return UDP_PACKET_SIZE;
}

bool GEMALTO_CINTERION_CellularStack::is_protocol_supported(nsapi_protocol_t protocol)
{
    return (protocol == NSAPI_UDP);
}

nsapi_error_t GEMALTO_CINTERION_CellularStack::socket_close_impl(int sock_id)
{
    tr_info("Close socket %d", sock_id);
    _at.cmd_start("AT^SISC=");
    _at.write_int(sock_id);
    _at.cmd_stop();
    _at.resp_start();
    _at.resp_stop();
    tr_info("Socket closed %d (err %d)", sock_id, _at.get_last_error());
    return _at.get_last_error();
}

// To open socket:
// 1. Select URC mode or polling mode with AT^SCFG
// 2. create a GPRS connection profile with AT^SICS (must have PDP)
// 3. create service profile with AT^SISS and map connectionID to serviceID
// 4. open internet session with AT^SISO (ELS61 tries to attach to a packet domain)
nsapi_error_t GEMALTO_CINTERION_CellularStack::create_socket_impl(CellularSocket *socket)
{
    tr_info("Create socket %d", socket->id);
    // setup internet connection profile
    if (_connectionProfileID == -1) {
        int connectionProfileID = CONNECTION_PROFILE_ID;
        int ipVersion;
        SocketAddress addr(get_ip_address());
        if (addr.get_ip_version() == NSAPI_IPv4) {
            ipVersion = 0;
        } else if (addr.get_ip_version() == NSAPI_IPv6) {
            ipVersion = 6;
        } else {
            return NSAPI_ERROR_NO_SOCKET;
        }

        char conParamType[12];
        std::sprintf(conParamType, "GPRS%d", ipVersion);
        _at.cmd_start("AT^SICS?");
        _at.cmd_stop();
        bool foundConnection = false;
        bool foundAPN = false;
        _at.resp_start("^SICS:");
        while (_at.info_resp()) {
            int id = _at.read_int();
            tr_debug("SICS %d", id);
            if (id == connectionProfileID) {
                char paramTag[16];
                int paramTagLen = _at.read_string(paramTag, sizeof(paramTag));
                if (paramTagLen > 0) {
                    tr_debug("paramTag %s", paramTag);
                    char paramValue[100+1]; // APN may be up to 100 chars
                    int paramValueLen = _at.read_string(paramValue, sizeof(paramValue));
                    if (paramValueLen >= 0) {
                        tr_debug("paramValue %s", paramValue);
                        if (strcmp(paramTag, "conType") == 0) {
                            tr_debug("conType %s", paramValue);
                            if (strcmp(paramValue, conParamType) == 0) {
                                foundConnection = true;
                            }
                        }
                        if (strcmp(paramTag, "apn") == 0) {
                            tr_debug("apn %s", paramValue);
                            if (strcmp(paramValue, _apn?_apn:"") == 0) {
                                foundAPN = true;
                            }
                        }
                    }
                }
            }
        }
        _at.resp_stop();

        if (!foundConnection) {
            tr_info("Update conType %s", conParamType);
            _at.cmd_start("AT^SICS=");
            _at.write_int(connectionProfileID);
            _at.write_string("conType");
            _at.write_string(conParamType);
            _at.cmd_stop();
            _at.resp_start();
            _at.resp_stop();
        }

        if (!foundAPN && _apn) {
            tr_info("Update APN %s", _apn?_apn:"");
            _at.cmd_start("AT^SICS=");
            _at.write_int(connectionProfileID);
            _at.write_string("apn");
            _at.write_string(_apn);
            _at.cmd_stop();
            _at.resp_start();
            _at.resp_stop();
        }

        // use URC mode
        _at.cmd_start("AT^SCFG=\"Tcp/withURCs\",\"on\"");
        _at.cmd_stop();
        _at.resp_start();
        _at.resp_stop();

        _connectionProfileID = connectionProfileID;

        tr_info("Created connID %d (err %d)", _connectionProfileID, _at.get_last_error());
    }

    // setup internet session profile
    int internetSessionID = socket->id;
    bool foundSrvType = false;
    bool foundConIdType = false;
    _at.cmd_start("AT^SISS?");
    _at.cmd_stop();
    _at.resp_start("^SISS:");
    while (_at.info_resp()) {
        int id = _at.read_int();
        tr_debug("SISS id=%d", id);
        if (id == internetSessionID) {
            char paramTag[16];
            int paramTagLen = _at.read_string(paramTag, sizeof(paramTag));
            if (paramTagLen > 0) {
                //tr_info("paramTag %s", paramTag);
                char paramValue[100+1]; // APN may be up to 100 chars
                int paramValueLen = _at.read_string(paramValue, sizeof(paramValue));
                if (paramValueLen >= 0) {
                    //tr_info("paramValue %s", paramValue);
                    if (strcmp(paramTag, "srvType") == 0) {
                        if (strcmp(paramValue, "Socket") == 0) {
                            tr_debug("srvType %s", paramValue);
                            foundSrvType = true;
                        }
                    }
                    if (strcmp(paramTag, "address") == 0) {
                        if (strncmp(paramValue, "sock", sizeof("sock")) == 0) {
                            tr_debug("address %s", paramValue);
                            foundSrvType = true;
                        }
                    }
                    if (strcmp(paramTag, "conId") == 0) {
                        char buf[10];
                        std::sprintf(buf, "%d", _connectionProfileID);
                        tr_debug("conId %s", paramValue);
                        if (strcmp(paramValue, buf) == 0) {
                            foundConIdType = true;
                        }
                    }
                }
            }
        }
    }
    _at.resp_stop();

    if (!foundSrvType) {
        _at.cmd_start("AT^SISS=");
        _at.write_int(internetSessionID);
        _at.write_string("srvType");
        _at.write_string("Socket");
        _at.cmd_stop();
        _at.resp_start();
        _at.resp_stop();
    }

    if (!foundConIdType) {
        _at.cmd_start("AT^SISS=");
        _at.write_int(internetSessionID);
        _at.write_string("conId");
        _at.write_int(_connectionProfileID);
        _at.cmd_stop();
        _at.resp_start();
        _at.resp_stop();

    }

    tr_info("Created socket (err %d)", _at.get_last_error());

    return _at.get_last_error();
}

nsapi_size_or_error_t GEMALTO_CINTERION_CellularStack::socket_sendto_impl(CellularSocket *socket, 
    const SocketAddress &address, const void *data, nsapi_size_t size)
{
    tr_info("Socket %d sendto %d bytes, addr %s, created %d (addr %s => %s)", 
        socket->id, size, address.get_ip_address(), socket->created, socket->remoteAddress, address);
        if (size > UDP_PACKET_SIZE) {
        tr_warn("Sending UDP packet size %d (max %d)", size, UDP_PACKET_SIZE);
        size = UDP_PACKET_SIZE;
    }
    if (!socket->created || socket->remoteAddress != address) {
        if (socket->created) {
            socket_close_impl(socket->id);
            _at.clear_error();
        }
        //_at.enable_debug(true);
        if (socket->remoteAddress != address) {
            const char *sockProto;
            if (socket->proto == NSAPI_UDP) {
                sockProto = "udp";
            } else {
                return NSAPI_ERROR_NO_SOCKET;
            }
            char ip[NSAPI_IPv6_SIZE + 2]; // +2 for brackets
            if (address.get_ip_version() == NSAPI_IPv4) {
                std::sprintf(ip, "%s", address.get_ip_address());
            } else {
                std::sprintf(ip, "[%s]", address.get_ip_address());
            }
            char sockAddr[sizeof("sockudp://") + sizeof(ip) + sizeof(":") + sizeof("65535") + 
                sizeof(";port=") + sizeof("65535") + 1];
            sprintf(sockAddr, "sock%s://%s:%u;port=%u", sockProto, ip, address.get_port(), 
                socket->localAddress.get_port());
            _at.cmd_start("AT^SISS=");
            _at.write_int(socket->id);
            _at.write_string("address");
            _at.write_string(sockAddr);
            _at.cmd_stop();
            _at.resp_start();
            _at.resp_stop();
            socket->remoteAddress = address;
        }
        _at.cmd_start("AT^SISO=");
        _at.write_int(socket->id);
        _at.cmd_stop();
        _at.resp_start();
        _at.resp_stop();
        if (_at.get_last_error()) {
            tr_error("Socket open failed!");
            return NSAPI_ERROR_NO_SOCKET;
        }
        socket->created = true;
        /*_at.resp_start("^SISW:");
        int sock_id = _at.read_int();
        int urc_code = _at.read_int();
        tr_info("TX ready: socket=%d, urc=%d (err=%d)", sock_id, urc_code, _at.get_last_error());
        (void)sock_id;
        (void)urc_code;*/
        tr_info("Socket would block %d (err %d)", socket->id, _at.get_last_error());
        return NSAPI_ERROR_WOULD_BLOCK;
        //rtos::Thread::wait(1000);
    }
    //_at.clear_error();
    //_at.enable_debug(true);
    tr_info("Socket send %d, len=%d (err %d)", socket->id, size, _at.get_last_error());
    _at.cmd_start("AT^SISW=");
    _at.write_int(socket->id);
    _at.write_int(size);
    _at.cmd_stop();

    _at.resp_start("^SISW:");
    if (!_at.info_resp()) {
        tr_error("No socket response!");
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    int socketID = _at.read_int();
    if (socketID != socket->id) {
        tr_error("Socket failed %d != %d", socketID, socket->id);
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    int acceptLen = _at.read_int();
    if (acceptLen == -1) {
        tr_error("Socket send failed!");
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    int unackData = _at.read_int();
    (void)unackData;
    tr_info("Socket %d accept %d, unacklen %d (err %d)", socket->id, acceptLen, unackData, _at.get_last_error());
    /*if (unackData == -1) { // if not present then it could be an URC ?
        tr_info("Socket sendto URC: socket=%d, code=%d", socketID, acceptLen);
        goto retry_sendto;
    }*/
    _at.write_string((char*)data, false);
    //_at.resp_stop();
    tr_info("Socket %d wrote %d bytes (err %d)", socket->id, acceptLen, _at.get_last_error());
    _at.enable_debug(false);
    return (_at.get_last_error() == NSAPI_ERROR_OK) ? acceptLen : NSAPI_ERROR_DEVICE_ERROR;
}

nsapi_size_or_error_t GEMALTO_CINTERION_CellularStack::socket_recvfrom_impl(CellularSocket *socket, SocketAddress *address,
        void *buffer, nsapi_size_t size)
{
    tr_info("Socket %d recv %d, rx_avail %d", socket->id, size, socket->rx_avail);
    rtos::Thread::wait(5000);
    if (size > UDP_PACKET_SIZE) {
        tr_warn("Socket recv packet size %d", size);
        size = UDP_PACKET_SIZE;
    }

    if (!socket->rx_avail) {
        _at.process_oob(); // check for ^SISR URC
        if (!socket->rx_avail) {
            tr_info("Socket would block");
            //return NSAPI_ERROR_WOULD_BLOCK;
        }
    }
    _at.cmd_start("AT^SISR=");
    _at.write_int(socket->id);
    _at.write_int(size);
    _at.cmd_stop();

    _at.resp_start("^SISR:");
    if (!_at.info_resp()) {
        tr_error("No socket response!");
        return NSAPI_ERROR_WOULD_BLOCK;
    }
    int socketID = _at.read_int();
    if (socketID != socket->id) {
        tr_error("Socket recvfrom failed!");
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    nsapi_size_or_error_t len = _at.read_int();
    if (len == 0) {
        tr_info("Socket would block");
        return NSAPI_ERROR_WOULD_BLOCK;
    }
    if (len == -1) {
        tr_error("Socket recvfrom failed!");
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    int remainLen = _at.read_int();
    if (remainLen <= 0) {
        socket->rx_avail = false;
    }

    ssize_t recvLen  = _at.read_bytes((uint8_t*)buffer, len);

    _at.resp_stop();

    tr_info("Socket %d, recvLen=%d, len=%d, size=%d (err %d)", socket->id, recvLen, len, size, _at.get_last_error());

    if (address) {
        *address = socket->remoteAddress; // expect response from socket remote address
    }

    return (_at.get_last_error() == NSAPI_ERROR_OK) ? recvLen : NSAPI_ERROR_DEVICE_ERROR;
}
