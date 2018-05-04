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

#ifndef _CELLULAR_STATEMACHINE_H
#define _CELLULAR_STATEMACHINE_H

#include "CellularTargets.h"
#if defined(CELLULAR_DEVICE) || defined(DOXYGEN_ONLY)


#include "EventQueue.h"
#include "Thread.h"

#include "CellularNetwork.h"
#include "CellularPower.h"
#include "CellularSIM.h"
#include "CellularUtil.h"


namespace mbed {

class CellularDevice;

const int MAX_PIN_SIZE = 8;
const int MAX_RETRY_ARRAY_SIZE2 = 10;

/** CellularStateMachine class
 *
 *  Finite State Machine for connecting to cellular network and listening network changes.
 */
class CellularStateMachine
{
public:
    /** Power needed blaa blaa
     *
     */
    CellularStateMachine(CellularPower *power, events::EventQueue &queue, CellularDevice *device);
    virtual ~CellularStateMachine();

public:
    /** Cellular connection states
     */
    enum CellularState {
        STATE_INIT = 0,
        STATE_POWER_ON,
        STATE_DEVICE_READY,
        STATE_MUX,
        STATE_SIM_PIN,
        STATE_REGISTERING_NETWORK,
        STATE_ATTACHING_NETWORK,
        STATE_CONNECTING_NETWORK,
        STATE_CONNECTED
    };

public:

    void set_sim_and_network(CellularSIM* sim, CellularNetwork* nw);
    void set_power(CellularPower* pwr);


    /** Set callback for state update
     *  @param status_callback function to call on state changes
     */
    void set_callback(Callback<bool(int, int, int)> status_callback);

    /** Register callback for status reporting
     *
     *  The specified status callback function will be called on status changes
     *  on the network. The parameters on the callback are the event type and
     *  event-type dependent reason parameter.
     *
     *  @param status_cb The callback for status changes
     */
    virtual void attach(Callback<void(nsapi_event_t, intptr_t)> status_cb);

    /** Get event queue that can be chained to main event queue (or use start_dispatch)
     *  @return event queue
     */
    events::EventQueue* get_queue();

    /** Start event queue dispatching
     *  @return see nsapi_error_t, 0 on success
     */
    nsapi_error_t start_dispatch();

    /** Stop event queue dispatching and close cellular interfaces
     */
    void stop();

    /** Change cellular connection to the target state
     *
     *  @return see nsapi_error_t, 0 on success
     */
    nsapi_error_t start();


    nsapi_error_t continue_from_state(CellularState state);


    /** Sets the timeout array for network rejects. After reject next item is tried and after all items are waited and
     *  still fails then current network event will fail.
     *
     *  @param timeout      timeout array using seconds
     *  @param array_len    length of the array
     */
    void set_retry_timeout_array(uint16_t timeout[], int array_len);

    const char* get_state_string(CellularState state);
private:
    bool power_on();
    bool open_sim();
    bool get_network_registration(CellularNetwork::RegistrationType type, CellularNetwork::RegistrationStatus &status, bool &is_registered);
    bool set_network_registration(char *plmn = 0);
    bool get_attach_network(CellularNetwork::AttachStatus &status);
    bool set_attach_network();
    bool is_registered();
    bool device_ready();
    nsapi_error_t is_automatic_registering(bool& auto_reg);

    // state functions to keep state machine simple
    void state_init();
    void state_power_on();
    void state_device_ready();
    void state_mux();
    void state_sim_pin();
    void state_registering();
    void state_attaching();
    void state_connect_to_network();
    void state_connected();
    void enter_to_state(CellularState state);
    void retry_state_or_fail();
    void network_callback(nsapi_event_t ev, intptr_t ptr);


private:
    void registering_urcs();
    void report_failure(const char* msg, nsapi_error_t error);
    void event();
    void ready_urc_cb();

    CellularState _state;
    CellularState _next_state;

    Callback<bool(int, int, int)> _status_callback;
    Callback<void(nsapi_event_t, intptr_t)> _event_status_cb;

    CellularDevice* _cellularDevice;
    CellularNetwork *_network;
    CellularPower *_power;
    CellularSIM *_sim;
    events::EventQueue &_queue;
    rtos::Thread *_queue_thread;

    int _retry_count;
    int _start_time;
    int _event_timeout;

    uint16_t _retry_timeout_array[MAX_RETRY_ARRAY_SIZE2];
    int _retry_array_length;
    int _event_id;
    bool _urcs_set;
};

} // namespace

#endif // CELLULAR_DEVICE || DOXYGEN

#endif /* _CELLULAR_STATEMACHINE_H */
