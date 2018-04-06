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

#include "CellularConnectionFSM.h"

#ifdef CELLULAR_DEVICE

#ifndef MBED_TRACE_MAX_LEVEL
#define MBED_TRACE_MAX_LEVEL TRACE_LEVEL_INFO
#endif
#include "CellularLog.h"
#include "CellularCommon.h"

// timeout to wait for AT responses
#define TIMEOUT_POWER_ON     (1*1000)
#define TIMEOUT_SIM_PIN      (1*1000)
#define TIMEOUT_NETWORK      (10*1000)
#define TIMEOUT_REGISTRATION (180*1000)

// maximum time when retrying network register, attach and connect in seconds ( 20minutes )
#define TIMEOUT_NETWORK_MAX (20*60)

#define RETRY_COUNT_DEFAULT 3

namespace mbed {

CellularConnectionFSM::CellularConnectionFSM() :
        _serial(0), _state(STATE_INIT), _next_state(_state), _status_callback(0), _event_status_cb(0), _network(0), _power(0), _sim(0),
        _queue(8 * EVENTS_EVENT_SIZE), _queue_thread(0), _cellularDevice(0), _retry_count(0), _event_timeout(-1),
        _at_queue(8 * EVENTS_EVENT_SIZE), _eventID(0)
{
    memset(_sim_pin, 0, sizeof(_sim_pin));
#if MBED_CONF_CELLULAR_RANDOM_MAX_START_DELAY == 0
    _start_time = 0;
#else
    // so that not every device don't start at the exact same time (for example after power outage)
    _start_time = rand() % (MBED_CONF_CELLULAR_RANDOM_MAX_START_DELAY);
#endif // MBED_CONF_CELLULAR_RANDOM_MAX_START_DELAY

    // set initial retry values in seconds
    _retry_timeout_array[0] = 1;
    _retry_timeout_array[1] = 2;
    _retry_timeout_array[2] = 4;
    _retry_timeout_array[3] = 16;
    _retry_timeout_array[4] = 32;
    _retry_timeout_array[5] = 60;
    _retry_timeout_array[6] = 120;
    _retry_timeout_array[7] = 360;
    _retry_timeout_array[8] = 600;
    _retry_timeout_array[9] = TIMEOUT_NETWORK_MAX;
    _retry_array_length = MAX_RETRY_ARRAY_SIZE;
}

CellularConnectionFSM::~CellularConnectionFSM()
{
    stop();
    delete _cellularDevice;
}

void CellularConnectionFSM::stop()
{
    tr_info("CellularConnectionUtil::stop");
    _cellularDevice->close_power();
    _cellularDevice->close_network();
    if (_queue_thread) {
        _queue_thread->terminate();
        _queue_thread = NULL;
    }
}

nsapi_error_t CellularConnectionFSM::init()
{
    tr_info("CELLULAR_DEVICE: %s", CELLULAR_STRINGIFY(CELLULAR_DEVICE));
    _cellularDevice = new CELLULAR_DEVICE(_at_queue);
    if (!_cellularDevice) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }

    _power = _cellularDevice->open_power(_serial);
    if (!_power) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }
    _network = _cellularDevice->open_network(_serial);
    if (!_network) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }

    _sim = _cellularDevice->open_sim(_serial);
    if (!_sim) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }

    _at_queue.chain(&_queue);

    _retry_count = 0;
    _state = STATE_INIT;
    _next_state = STATE_INIT;

    return _network->init();
}

bool CellularConnectionFSM::power_on()
{
    MBED_ASSERT(1);
    nsapi_error_t err = _power->on();
    if (err != NSAPI_ERROR_OK && err != NSAPI_ERROR_UNSUPPORTED) {
        tr_warn("Cellular start failed. Power off/on.");
        err = _power->off();
        if (err != NSAPI_ERROR_OK && err != NSAPI_ERROR_UNSUPPORTED) {
            tr_error("Cellular power down failed!");
        }
        return false;
    }
    return true;
}

void CellularConnectionFSM::set_sim_pin(const char * sim_pin)
{
    strncpy(_sim_pin, sim_pin, sizeof(_sim_pin));
}

bool CellularConnectionFSM::open_sim()
{
    CellularSIM::SimState state = CellularSIM::SimStateUnknown;
    // wait until SIM is readable
    // here you could add wait(secs) if you know start delay of your SIM
    if (_sim->get_sim_state(state) != NSAPI_ERROR_OK) {
        tr_info("Waiting for SIM (err while reading)...");
        return false;
    }

    switch (state) {
        case CellularSIM::SimStateReady:
            tr_info("SIM Ready");
            break;
        case CellularSIM::SimStatePinNeeded: {
            if (strlen(_sim_pin)) {
                tr_info("SIM pin required, entering pin: %s", _sim_pin);
                nsapi_error_t err = _sim->set_pin(_sim_pin);
                if (err) {
                    tr_error("SIM pin set failed with: %d, bailing out...", err);
                }
            } else {
                tr_warn("PIN required but No SIM pin provided.");
            }
        }
            break;
        case CellularSIM::SimStatePukNeeded:
            tr_info("SIM PUK code needed...");
            break;
        case CellularSIM::SimStateUnknown:
            tr_info("SIM, unknown state...");
            break;
        default:
            MBED_ASSERT(1);
            break;
    }

    if (_event_status_cb) {
        _event_status_cb(NSAPI_EVENT_CELLULAR_STATUS_CHANGE, CellularSIMStatusChanged);
    }

    return state == CellularSIM::SimStateReady;
}

bool CellularConnectionFSM::set_network_registration(char *plmn)
{
    if (_network->set_registration(plmn) != NSAPI_ERROR_OK) {
        tr_error("Failed to set network registration.");
        return false;
    }
    return true;
}

bool CellularConnectionFSM::is_registered()
{
    CellularNetwork::RegistrationStatus status;
    bool is_registered = false;

    for (int type = 0; type < CellularNetwork::C_MAX; type++) {
        if (get_network_registration((CellularNetwork::RegistrationType) type, status, is_registered)) {
            tr_debug("get_network_registration: type=%d, status=%d", type, status);
            if (is_registered) {
                break;
            }
        }
    }

    return is_registered;
}

bool CellularConnectionFSM::get_network_registration(CellularNetwork::RegistrationType type,
        CellularNetwork::RegistrationStatus &status, bool &is_registered)
{
    is_registered = false;
    bool is_roaming = false;
    nsapi_error_t err = _network->get_registration_status(type, status);
    if (err != NSAPI_ERROR_OK) {
        if (err != NSAPI_ERROR_UNSUPPORTED) {
            tr_warn("Get network registration failed (type %d)!", type);
        }
        return false;
    }
    switch (status) {
        case CellularNetwork::RegisteredRoaming:
            is_roaming = true;
            // fall-through
        case CellularNetwork::RegisteredHomeNetwork:
            is_registered = true;
            break;
        case CellularNetwork::RegisteredSMSOnlyRoaming:
            is_roaming = true;
            // fall-through
        case CellularNetwork::RegisteredSMSOnlyHome:
            tr_warn("SMS only network registration!");
            break;
        case CellularNetwork::RegisteredCSFBNotPreferredRoaming:
            is_roaming = true;
            // fall-through
        case CellularNetwork::RegisteredCSFBNotPreferredHome:
            tr_warn("Not preferred network registration!");
            break;
        case CellularNetwork::AttachedEmergencyOnly:
            tr_warn("Emergency only network registration!");
            break;
        case CellularNetwork::RegistrationDenied:
        case CellularNetwork::NotRegistered:
        case CellularNetwork::Unknown:
        case CellularNetwork::SearchingNetwork:
        default:
            break;
    }

    if (is_roaming) {
        tr_warn("Roaming cellular network!");
    }

    return true;
}

bool CellularConnectionFSM::get_attach_network(CellularNetwork::AttachStatus &status)
{
    nsapi_error_t err = _network->get_attach(status);
    if (err != NSAPI_ERROR_OK) {
        return false;
    }
    return true;
}

bool CellularConnectionFSM::set_attach_network()
{
    nsapi_error_t attach_err = _network->set_attach();
    if (attach_err != NSAPI_ERROR_OK) {
        return false;
    }
    return true;
}

void CellularConnectionFSM::report_failure(const char* msg)
{
    tr_error("Cellular network failed: %s", msg);
    if (_status_callback) {
        _status_callback(_state, _next_state);
    }
}

const char* CellularConnectionFSM::get_state_string(CellularState state)
{
    static const char *strings[] = { "Init", "Power", "Device ready", "SIM pin", "Registering network", "Attaching network", "Connecting network", "Connected"};
    return strings[state];
}

bool CellularConnectionFSM::is_automatic_registering()
{
    CellularNetwork::NWRegisteringMode mode;
    nsapi_error_t err = _network->get_network_registering_mode(mode);
    tr_debug("automatic registering mode: %d", mode);
    if (err == NSAPI_ERROR_OK && mode == CellularNetwork::NWModeAutomatic) {
        return true;
    }
    return false;
}


nsapi_error_t CellularConnectionFSM::continue_from_state(CellularState state)
{
    _state = state;
    if (!_queue.call_in(0, callback(this, &CellularConnectionFSM::event))) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }

    return NSAPI_ERROR_OK;
}

nsapi_error_t CellularConnectionFSM::continue_to_state(CellularState state)
{
    if (state < _state) {
        _state = state;
    } else {
        // update next state so that we don't continue from previous state
        _state = _next_state;
    }
    if (!_queue.call_in(0, callback(this, &CellularConnectionFSM::event))) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }

    return NSAPI_ERROR_OK;
}

void CellularConnectionFSM::enter_to_state(CellularState state)
{
    _next_state = state;
    _retry_count = 0;
}

void CellularConnectionFSM::retry_state_or_fail()
{
    if (++_retry_count < MAX_RETRY_ARRAY_SIZE) {
        tr_info("Retry State %s, retry %d/%d", get_state_string(_state), _retry_count, MAX_RETRY_ARRAY_SIZE);
        _event_timeout = _retry_timeout_array[_retry_count];
    } else {
        report_failure(get_state_string(_state));
        return;
    }
}

void CellularConnectionFSM::state_init()
{
    _event_timeout = _start_time;
    tr_info("Init state, waiting %d ms before POWER state)", _start_time);
    enter_to_state(STATE_POWER_ON);
}

void CellularConnectionFSM::state_power_on()
{
    _cellularDevice->set_timeout(TIMEOUT_POWER_ON);
    tr_info("Cellular power ON (timeout %d ms)", TIMEOUT_POWER_ON);
    if (power_on()) {
        enter_to_state(STATE_DEVICE_READY);
    } else {
        // retry to power on device
        retry_state_or_fail();
    }
}

void CellularConnectionFSM::state_device_ready()
{
    _cellularDevice->set_timeout(TIMEOUT_POWER_ON);
    if (_power->set_at_mode() == NSAPI_ERROR_OK) { // TODO: ask once, then wait for urc is possible as 3gpp does not define any first at command or such what we could listen
                                                   //       This could be done by adding module specific at command which to listen
        tr_info("Cellular device ready");
        if (_event_status_cb) {
            _event_status_cb(NSAPI_EVENT_CELLULAR_STATUS_CHANGE, CellularDeviceReady);
        }

        bool success = false;
        for (int type = 0; type < CellularNetwork::C_MAX; type++) {
            if (!_network->set_registration_urc((CellularNetwork::RegistrationType)type, true)) {
                 success = true;
            }
        }
        if (!success) {
            tr_error("Failed to set any URC's for registration");
            report_failure(get_state_string(_state));
            return;
        }

        enter_to_state(STATE_SIM_PIN);
    } else {
        retry_state_or_fail();
    }
}

void CellularConnectionFSM::state_sim_pin()
{
    _cellularDevice->set_timeout(TIMEOUT_SIM_PIN);
    tr_info("Sim state (timeout %d ms)", TIMEOUT_SIM_PIN);
    if (open_sim()) {
        enter_to_state(STATE_REGISTERING_NETWORK);
    } else {
        retry_state_or_fail();
    }
}

void CellularConnectionFSM::state_registering()
{
    _cellularDevice->set_timeout(TIMEOUT_NETWORK);

    if (is_registered()) {
        // we are already registered, go to attach
        enter_to_state(STATE_ATTACHING_NETWORK);
    } else {
        if (!is_automatic_registering()) { // when we support plmn add this :  || plmn
            // automatic registering is not on, set registration and retry
            set_network_registration();
        }
        retry_state_or_fail();
    }
}

void CellularConnectionFSM::state_attaching()
{
    _cellularDevice->set_timeout(TIMEOUT_NETWORK);
    CellularNetwork::AttachStatus attach_status;
    if (get_attach_network(attach_status)) {
        if (attach_status == CellularNetwork::Attached) {
            enter_to_state(STATE_CONNECTING_NETWORK);
        } else {
            set_attach_network();
            retry_state_or_fail();
        }
    } else {
        retry_state_or_fail();
    }
}

void CellularConnectionFSM::state_connect_to_network()
{
    _cellularDevice->set_timeout(TIMEOUT_NETWORK);
    tr_info("Connect to cellular network (timeout %d ms)", TIMEOUT_NETWORK);
    nsapi_error_t err = _network->connect();
    if (err == NSAPI_ERROR_OK) {
        // when using modems stack connect is synchronous
        _next_state = STATE_CONNECTED;
    } else {
        retry_state_or_fail();
    }
}

void CellularConnectionFSM::state_connected()
{
    _cellularDevice->set_timeout(TIMEOUT_NETWORK);
    tr_debug("Cellular ready! (timeout %d ms)", TIMEOUT_NETWORK);
    if (_status_callback) {
        _status_callback(_state, _next_state);
    }
}

void CellularConnectionFSM::event()
{
    _event_timeout = -1;
    switch (_state) {
        case STATE_INIT:
            state_init();
            break;
        case STATE_POWER_ON:
            state_power_on();
            break;
        case STATE_DEVICE_READY:
            state_device_ready();
            break;
        case STATE_SIM_PIN:
            state_sim_pin();
            break;
        case STATE_REGISTERING_NETWORK:
            state_registering();
            break;
        case STATE_ATTACHING_NETWORK:
            state_attaching();
            break;
        case STATE_CONNECTING_NETWORK:
            state_connect_to_network();
            break;
        case STATE_CONNECTED:
            state_connected();
            break;
        default:
            MBED_ASSERT(0);
            break;
    }

    if (_next_state != _state || _event_timeout >= 0) {
        if (_next_state != _state) { // state exit condition
            if (_status_callback) {
                if (!_status_callback(_state, _next_state)) {
                    return;
                }
            }
        } else {
            tr_info("Cellular event in %d seconds", _event_timeout);
        }
        _state = _next_state;
        if (_event_timeout == -1) {
            _event_timeout = 0;
        }
        _eventID = _queue.call_in(_event_timeout*1000, callback(this, &CellularConnectionFSM::event));
        if (!_eventID) {
            report_failure("Cellular event failure!");
            return;
        }
    }
}

nsapi_error_t CellularConnectionFSM::start_dispatch()
{
    MBED_ASSERT(!_queue_thread);

    _queue_thread = new rtos::Thread(osPriorityNormal, 1024);
    if (!_queue_thread) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }
    if (_queue_thread->start(callback(&_queue, &events::EventQueue::dispatch_forever)) != osOK) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }

    return NSAPI_ERROR_OK;
}

void CellularConnectionFSM::set_serial(UARTSerial *serial)
{
    _serial = serial;
}

void CellularConnectionFSM::set_callback(mbed::Callback<bool(int, int)> status_callback)
{
    _status_callback = status_callback;
}

void CellularConnectionFSM::attach(mbed::Callback<void(nsapi_event_t, intptr_t)> status_cb)
{
    _event_status_cb = status_cb;
    _network->attach(callback(this, &CellularConnectionFSM::network_callback));
}

void CellularConnectionFSM::network_callback(nsapi_event_t ev, intptr_t ptr)
{

    tr_debug("FSM: network_callback called with event: %d, intptr: %d", ev, ptr);
    if (ev == NSAPI_EVENT_CELLULAR_STATUS_CHANGE) {
        if (ptr == CellularRegistrationStatusChanged && _state == STATE_REGISTERING_NETWORK) {
            // check for registration status
            if (is_registered()) {
                _queue.cancel(_eventID);
                continue_from_state(STATE_ATTACHING_NETWORK);
            }
        }
    }

    if (_event_status_cb) {
        _event_status_cb(ev, ptr);
    }
}

events::EventQueue *CellularConnectionFSM::get_queue()
{
    return &_queue;
}

CellularNetwork* CellularConnectionFSM::get_network()
{
    return _network;
}

CellularDevice* CellularConnectionFSM::get_device()
{
    return _cellularDevice;
}

CellularSIM* CellularConnectionFSM::get_sim()
{
    return _sim;
}

NetworkStack *CellularConnectionFSM::get_stack()
{
    return _cellularDevice->get_stack();
}

void CellularConnectionFSM::set_retry_timeout_array(uint16_t timeout[], int array_len)
{
    _retry_array_length = array_len > MAX_RETRY_ARRAY_SIZE ? MAX_RETRY_ARRAY_SIZE : array_len;

    for (int i = 0; i < _retry_array_length; i++) {
        _retry_timeout_array[i] = timeout[i];
    }
}

} // namespace

#endif // CELLULAR_DEVICE
