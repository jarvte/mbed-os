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

#include "CellularDevice.h"
#include "CellularLog.h"
#include "UARTSerial.h"

namespace mbed
{

CellularDevice::CellularDevice() : _state_machine(0), _is_connected(false), _nw_status_cb(0), _fh(0), _queue(0),
        _blocking(true), _target_state(CellularStateMachine::STATE_POWER_ON), _cellularSemaphore(0)
{
}

CellularDevice::~CellularDevice()
{
    delete _state_machine;
}

nsapi_error_t CellularDevice::init(FileHandle *fh, events::EventQueue *queue)
{
    MBED_ASSERT(_state_machine == NULL);
    tr_info("CELLULAR_DEVICE: %s", CELLULAR_STRINGIFY(CELLULAR_DEVICE));

    _fh = fh;
    _queue = queue;

    CellularPower* power = open_power(fh);
    if (!power) {
        tr_error("Could not create power");
        return NSAPI_ERROR_NO_MEMORY;
    }
    _state_machine = new CellularStateMachine(power, *queue, this);

    if (!_state_machine) {
        tr_error("Could not create state machine");
        return NSAPI_ERROR_NO_MEMORY;
    }

    return NSAPI_ERROR_OK;
}

CellularStateMachine* CellularDevice::get_state_machine()
{
    return _state_machine;
}

void CellularDevice::set_credentials(const char *apn, const char *uname,
                                 const char *pwd)
{
}

void CellularDevice::set_sim_pin(const char *sim_pin)
{
}

nsapi_error_t CellularDevice::connect(const char *sim_pin, const char *apn,
                                  const char *uname, const char *pwd)
{
    //set_sim_pin(sim_pin);
    //set_credentials(apn, uname, pwd);
    return connect();
}

nsapi_error_t CellularDevice::connect()
{
    nsapi_error_t err = NSAPI_ERROR_OK;

    if (_state_machine == NULL) {
        // If application haven't call init, then it's a simple version and we configure it by ourselves
        UARTSerial* serial = new UARTSerial(MDMTXD, MDMRXD, MBED_CONF_PLATFORM_DEFAULT_SERIAL_BAUD_RATE);
        if (!serial) {
            return NSAPI_ERROR_NO_MEMORY;
        }
        events::EventQueue* queue = new events::EventQueue();
        if (!queue) {
            delete serial;
            return NSAPI_ERROR_NO_MEMORY;
        }
        err = init(serial, queue);
        if (err) {
            delete serial;
            delete queue;
        }

    }

    _target_state = CellularStateMachine::STATE_CONNECTED;
    err = _state_machine->start();

    // should we use connect in asynchronous or synchronous way
    if (_blocking && err == NSAPI_ERROR_OK) {
        int ret_wait = _cellularSemaphore.wait(10 * 60 * 1000); // cellular network searching may take several minutes
        if (ret_wait != 1) {
            tr_info("No cellular connection");
            err = NSAPI_ERROR_NO_CONNECTION;
        }
    }

    return err;
}

nsapi_error_t CellularDevice::disconnect()
{
    _state_machine->stop();
    return NSAPI_ERROR_OK;
}

bool CellularDevice::is_connected()
{
    return _is_connected;
}

const char *CellularDevice::get_ip_address()
{
    CellularNetwork *network = open_network(_fh);
    if (!network) {
        return NULL;
    }
    return network->get_ip_address();
}

const char *CellularDevice::get_netmask()
{
    CellularNetwork *network = open_network(_fh);
    if (!network) {
        return NULL;
    }
    return network->get_netmask();
}

const char *CellularDevice::get_gateway()
{
    CellularNetwork *network = open_network(_fh);
    if (!network) {
        return NULL;
    }
    return network->get_gateway();
}

nsapi_error_t CellularDevice::set_blocking(bool blocking)
{
    _blocking = blocking;
    return NSAPI_ERROR_OK;
}

void CellularDevice::attach(Callback<void(nsapi_event_t, intptr_t)> status_cb)
{
    _nw_status_cb = status_cb;
}

void CellularDevice::network_callback(nsapi_event_t ev, intptr_t ptr)
{
    if (ev == NSAPI_EVENT_CONNECTION_STATUS_CHANGE) {
        if (ptr == NSAPI_STATUS_GLOBAL_UP) {
            _is_connected = true;
        } else {
            _is_connected = false;
        }
    }

    // forward network callback to application is it has registered with attach
    if (_nw_status_cb) {
        _nw_status_cb(ev, ptr);
    }
}

bool CellularDevice::state_machine_callback(int state, int next_state, int error)
{
    tr_info("state_machine_callback: %s ==> %s", _state_machine->get_state_string((CellularStateMachine::CellularState)state),
            _state_machine->get_state_string((CellularStateMachine::CellularState)next_state));


    if (state == CellularStateMachine::STATE_MUX) {
        // If mux is in use we should create mux and it channels here. Then create power, sim and network with filehandles
        // got from mux. Now that we don't have mux we just create sim and power.
        CellularSIM* sim = open_sim(_fh);
        CellularNetwork* nw = open_network(_fh);
        nsapi_error_t* err = nw->init();
        _state_machine->set_sim_and_network(sim, nw);
        return true;
    }


    if (_target_state == state) {
        tr_info("Target state reached: %s", _cellularConnectionFSM.get_state_string(_target_state));
        MBED_ASSERT(_cellularSemaphore.release() == osOK);
        return false; // return false -> state machine is halted
    }

    // return true to continue state machine
    return true;
}

const char* get_sim_pin() const
{

}

} // namespace mbed
