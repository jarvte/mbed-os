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

#include "CellularStateMachine.h"

#ifdef CELLULAR_DEVICE

#ifndef MBED_TRACE_MAX_LEVEL
#define MBED_TRACE_MAX_LEVEL TRACE_LEVEL_INFO
#endif
#include "CellularLog.h"
#include "CellularCommon.h"
#include "CellularDevice.h"

// timeout to wait for AT responses
#define TIMEOUT_POWER_ON     (1*1000)
#define TIMEOUT_SIM_PIN      (1*1000)
#define TIMEOUT_NETWORK      (10*1000)
#define TIMEOUT_CONNECT      (60*1000)
#define TIMEOUT_REGISTRATION (180*1000)

// maximum time when retrying network register, attach and connect in seconds ( 20minutes )
#define TIMEOUT_NETWORK_MAX (20*60)

#define RETRY_COUNT_DEFAULT 3

namespace mbed
{

CellularStateMachine::CellularStateMachine(CellularPower *power, events::EventQueue &queue, CellularDevice *device) :
         _state(STATE_INIT), _next_state(_state), _status_callback(0), _event_status_cb(0), _cellularDevice(device), _network(0),
        _power(power), _sim(0), _queue(queue), _queue_thread(0), _retry_count(0),
        _event_timeout(-1), _event_id(0), _urcs_set(false)
{
    memset(_sim_pin, 0, sizeof(_sim_pin));
#if MBED_CONF_CELLULAR_RANDOM_MAX_START_DELAY == 0
    _start_time = 0;
#else
    // so that not every device don't start at the exact same time (for example after power outage)
    _start_time = rand() % (MBED_CONF_CELLULAR_RANDOM_MAX_START_DELAY);
#endif // MBED_CONF_CELLULAR_RANDOM_MAX_START_DELAY

    // set initial retry values in seconds
    _retry_timeout_array[0] = 1; // double time on each retry in order to keep network happy
    _retry_timeout_array[1] = 2;
    _retry_timeout_array[2] = 4;
    _retry_timeout_array[3] = 8;
    _retry_timeout_array[4] = 16;
    _retry_timeout_array[5] = 32;
    _retry_timeout_array[6] = 64;
    _retry_timeout_array[7] = 128; // if around two minutes was not enough then let's wait much longer
    _retry_timeout_array[8] = 600;
    _retry_timeout_array[9] = TIMEOUT_NETWORK_MAX;
    _retry_array_length = MAX_RETRY_ARRAY_SIZE2;
}

CellularStateMachine::~CellularStateMachine()
{
    stop();
}

void CellularStateMachine::set_sim_and_network(CellularSIM* sim, CellularNetwork* nw)
{
    _sim = sim;
    _network = nw;
}

void CellularStateMachine::set_power(CellularPower* pwr)
{
    _power = pwr;
}

void CellularStateMachine::stop()
{
    tr_info("CellularConnectionUtil::stop");
    if (_queue_thread) {
        _queue_thread->terminate();
        delete _queue_thread;
        _queue_thread = NULL;
    }
}

bool CellularStateMachine::power_on()
{
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

bool CellularStateMachine::open_sim()
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

            // query sim from the CellularDevice/application
            char* sim_pin =
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
        _event_status_cb((nsapi_event_t)CellularSIMStatusChanged, state);
    }

    return state == CellularSIM::SimStateReady;
}

bool CellularStateMachine::set_network_registration(char *plmn)
{
    nsapi_error_t err = _network->set_registration(plmn);
    if (err != NSAPI_ERROR_OK) {
        tr_error("Failed to set network registration with: %d", err);
        return false;
    }
    return true;
}

bool CellularStateMachine::is_registered()
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

bool CellularStateMachine::get_network_registration(CellularNetwork::RegistrationType type,
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

bool CellularStateMachine::get_attach_network(CellularNetwork::AttachStatus &status)
{
    nsapi_error_t err = _network->get_attach(status);
    if (err != NSAPI_ERROR_OK) {
        return false;
    }
    return true;
}

bool CellularStateMachine::set_attach_network()
{
    nsapi_error_t attach_err = _network->set_attach();
    if (attach_err != NSAPI_ERROR_OK) {
        return false;
    }
    return true;
}

void CellularStateMachine::report_failure(const char* msg, nsapi_error_t error)
{
    tr_error("Cellular network failed: %s with error: %d", msg, error);
    if (_status_callback) {
        _status_callback(_state, _next_state, error);
    }
}

const char* CellularStateMachine::get_state_string(CellularState state)
{
#if MBED_CONF_MBED_TRACE_ENABLE
    static const char *strings[] = { "Init", "Power", "Device ready", "Mux", "SIM pin", "Registering network", "Attaching network", "Connecting network", "Connected"};
    return strings[state];
#else
    return "";
#endif // #if MBED_CONF_MBED_TRACE_ENABLE
}

nsapi_error_t CellularStateMachine::is_automatic_registering(bool& auto_reg)
{
    CellularNetwork::NWRegisteringMode mode;
    nsapi_error_t err = _network->get_network_registering_mode(mode);
    if (err == NSAPI_ERROR_OK) {
        tr_debug("automatic registering mode: %d", mode);
        auto_reg = (mode == CellularNetwork::NWModeAutomatic);
    }
    return err;
}

nsapi_error_t CellularStateMachine::continue_from_state(CellularState state)
{
    tr_info("Continue state from %s to %s", get_state_string((CellularStateMachine::CellularState)_state),
            get_state_string((CellularStateMachine::CellularState)state));
    _state = state;
    _next_state = state;
    _retry_count = 0;
    if (!_queue.call_in(0, callback(this, &CellularStateMachine::event))) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }

    return NSAPI_ERROR_OK;
}

nsapi_error_t CellularStateMachine::start()
{
    _retry_count = 0;
    _state = STATE_INIT;
    if (!_queue.call_in(0, callback(this, &CellularStateMachine::event))) {
        stop();
        return NSAPI_ERROR_NO_MEMORY;
    }

    return NSAPI_ERROR_OK;
}

void CellularStateMachine::enter_to_state(CellularState state)
{
    _next_state = state;
    _retry_count = 0;
}

void CellularStateMachine::retry_state_or_fail()
{
    if (++_retry_count < MAX_RETRY_ARRAY_SIZE2) {
        tr_debug("Retry State %s, retry %d/%d", get_state_string(_state), _retry_count, MAX_RETRY_ARRAY_SIZE2);
        _event_timeout = _retry_timeout_array[_retry_count];
    } else {
        report_failure(get_state_string(_state), NSAPI_ERROR_NO_CONNECTION);
        return;
    }
}

void CellularStateMachine::state_init()
{
    _event_timeout = _start_time;
    tr_info("Init state, waiting %d ms before POWER state)", _start_time);
    enter_to_state(STATE_POWER_ON);
}

void CellularStateMachine::state_power_on()
{
    // TODO: set timeout in callback of the statemachine or give cellulardevice in constructor?
    _cellularDevice->set_timeout(TIMEOUT_POWER_ON);
    tr_info("Cellular power ON (timeout %d ms)", TIMEOUT_POWER_ON);
    if (power_on()) {
        enter_to_state(STATE_DEVICE_READY);
    } else {
        // retry to power on device
        retry_state_or_fail();
    }
}

bool CellularStateMachine::device_ready()
{
    tr_info("Cellular device ready");
    if (_event_status_cb) {
        _event_status_cb((nsapi_event_t)CellularDeviceReady, 0);
    }

    _power->remove_device_ready_urc_cb(mbed::callback(this, &CellularStateMachine::ready_urc_cb));

    return true;
}

void CellularStateMachine::state_device_ready()
{
    _cellularDevice->set_timeout(TIMEOUT_POWER_ON);
    tr_info("state_device_ready");
    if (_power->set_at_mode() == NSAPI_ERROR_OK) {
        tr_info("state_device_ready, set_at_mode success");
        if (device_ready()) {
            enter_to_state(STATE_MUX);
        }
    } else {
        tr_info("state_device_ready, set_at_mode failed...");
        if (_retry_count == 0) {
            (void)_power->set_device_ready_urc_cb(mbed::callback(this, &CellularStateMachine::ready_urc_cb));
        }
        retry_state_or_fail();
    }
}

void CellularStateMachine::state_mux()
{
    _next_state = STATE_SIM_PIN;
}

void CellularStateMachine::state_sim_pin()
{
    _cellularDevice->set_timeout(TIMEOUT_SIM_PIN);
    tr_info("Sim state (timeout %d ms)", TIMEOUT_SIM_PIN);
    if (open_sim()) {
        enter_to_state(STATE_REGISTERING_NETWORK);
    } else {
        retry_state_or_fail();
    }
}

void CellularStateMachine::registering_urcs()
{
    if (_urcs_set) {
        return;
    } else {
        bool success = false;
        for (int type = 0; type < CellularNetwork::C_MAX; type++) {
            if (!_network->set_registration_urc((CellularNetwork::RegistrationType)type, true)) {
                success = true;
            }
        }
        if (!success) {
            tr_error("Failed to set any URC's for registration");
            retry_state_or_fail();
            return;
        }
        _urcs_set = true;
        tr_info("registering urc's done");
    }
}

void CellularStateMachine::state_registering()
{
    _cellularDevice->set_timeout(TIMEOUT_NETWORK);

    registering_urcs();

    if (is_registered()) {
        // we are already registered, go to attach
        enter_to_state(STATE_ATTACHING_NETWORK);
    } else {
        bool auto_reg = false;
        nsapi_error_t err = is_automatic_registering(auto_reg);
        if (err == NSAPI_ERROR_OK && !auto_reg) { // when we support plmn add this :  || plmn
            // automatic registering is not on, set registration and retry
            _cellularDevice->set_timeout(TIMEOUT_REGISTRATION);
            set_network_registration();
        }
        retry_state_or_fail();
    }
}

void CellularStateMachine::state_attaching()
{
    _cellularDevice->set_timeout(TIMEOUT_CONNECT);
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

void CellularStateMachine::state_connect_to_network()
{
    _cellularDevice->set_timeout(TIMEOUT_CONNECT);
    tr_info("Connect to cellular network (timeout %d ms)", TIMEOUT_CONNECT);
    if (_network->connect() == NSAPI_ERROR_OK) {
        //_cellularDevice->set_timeout(TIMEOUT_NETWORK);
        tr_debug("Connected to cellular network, set at timeout (timeout %d ms)", TIMEOUT_NETWORK);
        // when using modems stack connect is synchronous
        _next_state = STATE_CONNECTED;
    } else {
        retry_state_or_fail();
    }
}

void CellularStateMachine::state_connected()
{
    _cellularDevice->set_timeout(TIMEOUT_NETWORK);
    tr_debug("Cellular ready! (timeout %d ms)", TIMEOUT_NETWORK);
    if (_status_callback) {
        _status_callback(_state, _next_state, NSAPI_ERROR_OK);
    }
}

void CellularStateMachine::event()
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
        case STATE_MUX:
            state_mux();
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
            tr_info("Cellular state from %s to %s", get_state_string((CellularStateMachine::CellularState)_state),
                    get_state_string((CellularStateMachine::CellularState)_next_state));
            if (_status_callback) {
                if (!_status_callback(_state, _next_state, NSAPI_ERROR_OK)) {
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
        _event_id = _queue.call_in(_event_timeout*1000, callback(this, &CellularStateMachine::event));
        if (!_event_id) {
            report_failure("Cellular event failure!", NSAPI_ERROR_NO_CONNECTION);
            return;
        }
    }
}

nsapi_error_t CellularStateMachine::start_dispatch()
{
    MBED_ASSERT(!_queue_thread);

    _queue_thread = new rtos::Thread(osPriorityNormal, 2048);
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

void CellularStateMachine::set_callback(Callback<bool(int, int, int)> status_callback)
{
    _status_callback = status_callback;
}

void CellularStateMachine::attach(Callback<void(nsapi_event_t, intptr_t)> status_cb)
{
    _event_status_cb = status_cb;
    _network->attach(callback(this, &CellularStateMachine::network_callback));
}

void CellularStateMachine::network_callback(nsapi_event_t ev, intptr_t ptr)
{
    tr_info("FSM: network_callback called with event: %d, intptr: %d", ev, ptr);
    if ((cellular_connection_status_t)ev == CellularRegistrationStatusChanged && _state == STATE_REGISTERING_NETWORK) {
        // expect packet data so only these states are valid
        if (ptr == CellularNetwork::RegisteredHomeNetwork && CellularNetwork::RegisteredRoaming) {
            _queue.cancel(_event_id);
            continue_from_state(STATE_ATTACHING_NETWORK);
        }
    }

    if (_event_status_cb) {
        _event_status_cb(ev, ptr);
    }
}

void CellularStateMachine::ready_urc_cb()
{
    tr_debug("Device ready URC func called");
    if (_state == STATE_DEVICE_READY && _power->set_at_mode() == NSAPI_ERROR_OK) {
        tr_debug("State was STATE_DEVICE_READY and at mode ready, cancel state and move to next");
        _queue.cancel(_event_id);
        if (device_ready()) {
            continue_from_state(STATE_SIM_PIN);
        }
    }
}

events::EventQueue *CellularStateMachine::get_queue()
{
    return &_queue;
}

void CellularStateMachine::set_retry_timeout_array(uint16_t timeout[], int array_len)
{
    _retry_array_length = array_len > MAX_RETRY_ARRAY_SIZE2 ? MAX_RETRY_ARRAY_SIZE2 : array_len;

    for (int i = 0; i < _retry_array_length; i++) {
        _retry_timeout_array[i] = timeout[i];
    }
}

} // namespace

#endif // CELLULAR_DEVICE
