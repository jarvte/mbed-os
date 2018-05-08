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
#include "nsapi_ppp.h"

#ifndef MBED_CONF_APP_SIM_PIN_CODE
# define MBED_CONF_APP_SIM_PIN_CODE    "1234"
#endif

namespace mbed
{

CellularDevice::CellularDevice(events::EventQueue *at_queue) : _state_machine(0), _is_connected(false), _nw_status_cb(0), _fh(0), _queue(0),
        _blocking(true), _target_state(CellularStateMachine::STATE_POWER_ON), _cellularSemaphore(0), _at_queue(at_queue)
{
    memset(_sim_pin, 0, sizeof(_sim_pin));
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
    _state_machine->set_sim_callback(callback(this, &CellularDevice::sim_pin_callback));
    _state_machine->set_callback(callback(this, &CellularDevice::state_machine_callback));
    _state_machine->attach(callback(this, &CellularDevice::network_callback));

    _at_queue->chain(queue);
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
    _state_machine->set_credentials(apn, uname, pwd);
}

void CellularDevice::set_sim_pin(const char *sim_pin)
{
    strncpy(_sim_pin, sim_pin, sizeof(_sim_pin));
    _sim_pin[sizeof(_sim_pin)-1] = '\0';
}

void CellularDevice::set_plmn(const char* plmn)
{
    _state_machine->set_plmn(plmn);
}

nsapi_error_t CellularDevice::connect(const char *sim_pin, const char *apn,
                                  const char *uname, const char *pwd)
{
    set_sim_pin(sim_pin);
    set_credentials(apn, uname, pwd);
    return connect();
}

nsapi_error_t CellularDevice::connect()
{
    nsapi_error_t err = NSAPI_ERROR_OK;

    if (_state_machine == NULL) {
        // If application haven't call init, then we configure it by ourselves
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
        } else {
            _state_machine->start_dispatch();
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
    close_all_interfaces();

    delete  _state_machine;
    _state_machine = NULL;
    _is_connected = false;
    return NSAPI_ERROR_OK;
}

bool CellularDevice::is_connected()
{
    return _is_connected;
}

const char *CellularDevice::get_ip_address()
{
#if NSAPI_PPP_AVAILABLE
    return nsapi_ppp_get_ip_addr(_fh);
#else
    NetworkStack *st = get_stack();
    if (st) {
        return st->get_ip_address();
    }
    return NULL;
#endif
}

const char *CellularDevice::get_netmask()
{
    return NULL;
}

const char *CellularDevice::get_gateway()
{
    return NULL;
}

nsapi_error_t CellularDevice::set_blocking(bool blocking)
{
    _blocking = blocking;
    return NSAPI_ERROR_OK;
}

NetworkStack *CellularDevice::get_stack()
{
    return open_network(_fh)->get_stack();
}

void CellularDevice::attach(Callback<void(nsapi_event_t, intptr_t)> status_cb)
{
    _nw_status_cb = status_cb;
}

char* CellularDevice::sim_pin_callback(CellularSIM::SimState state)
{
    if (state == CellularSIM::SimStatePinNeeded) {
        if (strlen(_sim_pin)) {
            return _sim_pin;
        } else {
            return MBED_CONF_APP_SIM_PIN_CODE;
        }
    } else if (state == CellularSIM::SimStatePukNeeded) {
        return NULL; // here if you have puk code you should return puk and new pin in format 'puk,new_pin'
    }

    return NULL;
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
        nsapi_error_t err = nw->init();
        _state_machine->set_sim(sim);
        _state_machine->set_network(nw);
        return true;
    }

    if (_target_state == state) {
        tr_info("Target state reached: %s", _state_machine->get_state_string(_target_state));
        MBED_ASSERT(_cellularSemaphore.release() == osOK);
        return false; // return false -> state machine is halted
    }

    // return true to continue state machine
    return true;
}

} // namespace mbed
