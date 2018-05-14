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

#ifndef CELLULAR_DEVICE_H_
#define CELLULAR_DEVICE_H_

#include "FileHandle.h"

#include "CellularBase.h"
#include "CellularSIM.h"
#include "CellularNetwork.h"
#include "CellularSMS.h"
#include "CellularPower.h"
#include "CellularInformation.h"
#include "EventQueue.h"
#include "NetworkStack.h"

#include "CellularStateMachine.h"

const int MAX_PIN_SIZE = 8;

namespace mbed
{

/**
 *  Class CellularDevice
 *
 *  An abstract interface that defines opening and closing of cellular interfaces.
 *  Deleting/Closing of opened interfaces can be done only via this class.
 */
class CellularDevice : public CellularBase
{
public:
    /** Constructor
     *
     *  @param at_queue queue used in ATHandler classes.
     */
    CellularDevice(events::EventQueue *at_queue);

    /** virtual Destructor
     */
    virtual ~CellularDevice();

public:
    /** Initializes CellularDevice by creating CellularPower and CellularStateMachine.
     *  Not mandatory to call. If this is not called then connect creates filehandle and queue.
     *
     *  @param fh       file handle used when creating CellularPower to communicate with modem
     *  @param queue    queue used in state machine
     *  @return         zero on success
     */
    nsapi_error_t init(FileHandle *fh, events::EventQueue *queue);

    CellularStateMachine* get_state_machine();

    /** Create new CellularNetwork interface.
     *
     *  @param fh    file handle used in communication to modem. Can be for example UART handle.
     *  @return      New instance of interface CellularNetwork.
     */
    virtual CellularNetwork *open_network(FileHandle *fh) = 0;

    /** Create new CellularSMS interface.
     *
     *  @param fh    file handle used in communication to modem. Can be for example UART handle.
     *  @return      New instance of interface CellularSMS.
     */
    virtual CellularSMS *open_sms(FileHandle *fh) = 0;

    /** Create new CellularPower interface.
     *
     *  @param fh    file handle used in communication to modem. Can be for example UART handle.
     *  @return      New instance of interface CellularPower.
     */
    virtual CellularPower *open_power(FileHandle *fh) = 0;

    /** Create new CellularSIM interface.
     *
     *  @param fh    file handle used in communication to modem. Can be for example UART handle.
     *  @return      New instance of interface CellularSIM.
     */
    virtual CellularSIM *open_sim(FileHandle *fh) = 0;

    /** Create new CellularInformation interface.
     *
     *  @param fh    file handle used in communication to modem. Can be for example UART handle.
     *  @return      New instance of interface CellularInformation.
     */
    virtual CellularInformation *open_information(FileHandle *fh) = 0;

    /** Closes the opened CellularNetwork by deleting the CellularNetwork instance.
     */
    virtual void close_network() = 0;

    /** Closes the opened CellularSMS by deleting the CellularSMS instance.
     */
    virtual void close_sms() = 0;

    /** Closes the opened CellularPower by deleting the CellularPower instance.
     */
    virtual void close_power() = 0;

    /** Closes the opened CellularSIM by deleting the CellularSIM instance.
     */
    virtual void close_sim() = 0;

    /** Closes the opened CellularInformation by deleting the CellularInformation instance.
     */
    virtual void close_information() = 0;

    /** Closes all interfaces.
     */
    virtual void close_all_interfaces() = 0;

    /** Set the default response timeout.
     *
     *  @param timeout    milliseconds to wait response from modem
     */
    virtual void set_timeout(int timeout) = 0;

    /** Turn modem debug traces on
     *
     *  @param on         set true to enable debug traces
     */
    virtual void modem_debug_on(bool on) = 0;

    /** Sets the operator plmn which is used when registering to a network specified by plmn. If plmn is not set then automatic
     *  registering is used when registering to a cellular network. Does not start any operations.
     *
     *  @param plmn operator in numeric format. See more from 3GPP TS 27.007 chapter 7.3.
     */
    void set_plmn(const char* plmn);

public: // from CellularBase
    /** Set the Cellular network credentials
     *
     *  Please check documentation of connect() for default behaviour of APN settings.
     *
     *  @param apn      Access point name
     *  @param uname    optionally, Username
     *  @param pwd      optionally, password
     */
    virtual void set_credentials(const char *apn, const char *uname = 0, const char *pwd = 0);

    /** Set the pin code for SIM card
     *
     *  @param sim_pin      PIN for the SIM card
     */
    virtual void set_sim_pin(const char *sim_pin);

    /** Start the interface
     *
     *  Attempts to connect to a Cellular network.
     *
     *  @param sim_pin     PIN for the SIM card
     *  @param apn         optionally, access point name
     *  @param uname       optionally, Username
     *  @param pwd         optionally, password
     *  @return            NSAPI_ERROR_OK on success, or negative error code on failure
     */
    virtual nsapi_error_t connect(const char *sim_pin, const char *apn = 0,
                                  const char *uname = 0,
                                  const char *pwd = 0);

    /** Start the interface
     *
     *  Attempts to connect to a Cellular network.
     *  If the SIM requires a PIN, and it is not set/invalid, NSAPI_ERROR_AUTH_ERROR is returned.
     *
     *  @return            NSAPI_ERROR_OK on success, or negative error code on failure
     */
    virtual nsapi_error_t connect();

    /** Stop the interface
     *
     *  @return         0 on success, or error code on failure
     */
    virtual nsapi_error_t disconnect();

    /** Check if the connection is currently established or not
     *
     * @return true/false   If the cellular module have successfully acquired a carrier and is
     *                      connected to an external packet data network using PPP, isConnected()
     *                      API returns true and false otherwise.
     */
    virtual bool is_connected();

    /** Get the local IP address
     *
     *  @return         Null-terminated representation of the local IP address
     *                  or null if no IP address has been received
     */
    virtual const char *get_ip_address();

    /** Get the local network mask
     *
     *  @return         Null-terminated representation of the local network mask
     *                  or null if no network mask has been received
     */
    virtual const char *get_netmask();

    /** Get the local gateways
     *
     *  @return         Null-terminated representation of the local gateway
     *                  or null if no network mask has been received
     */
    virtual const char *get_gateway();

    /** Register callback for status reporting
     *
     *  The specified status callback function will be called on status changes
     *  on the network. The parameters on the callback are the event type and
     *  event-type dependent reason parameter.
     *
     *  @param status_cb The callback for status changes
     */
    virtual void attach(mbed::Callback<void(nsapi_event_t, intptr_t)> status_cb);

    /** If one needs to enter SIM pin/puk code it's queried with this callback.
     *  There is a default implementation in this class already.
     */
    void set_sim_callback(Callback<const char*(CellularSIM::SimState)> sim_pin_cb);

public: // from NetworkInterface

    /** Set blocking status of connect() which by default should be blocking
     *
     *  @param blocking true if connect is blocking
     *  @return         0 on success, negative error code on failure
     */
    virtual nsapi_error_t set_blocking(bool blocking);

    /** Provide access to the NetworkStack object
     *
     *  @return The underlying NetworkStack object
     */
    virtual NetworkStack *get_stack();

private:

    void network_callback(nsapi_event_t ev, intptr_t ptr);
    bool state_machine_callback(int state, int next_state, int error);
    const char* sim_pin_callback(CellularSIM::SimState state);
    nsapi_error_t create_and_init();

    CellularStateMachine* _state_machine;
    bool _is_connected;
    Callback<void(nsapi_event_t, intptr_t)> _nw_status_cb;
    Callback<const char*(CellularSIM::SimState)> _sim_pin_cb;

    FileHandle *_fh;
    events::EventQueue *_queue;
    bool _blocking;

    CellularStateMachine::CellularState _target_state;
    rtos::Semaphore _cellularSemaphore;
    events::EventQueue *_at_queue;

    // until we have to support CellularBase we need to store pin, apn, uname and pwd temporarily here
    char _sim_pin[MAX_PIN_SIZE+1];
    bool _self_created;

    const char *_apn;
    const char *_uname;
    const char *_pwd;


};

} // namespace mbed

#endif // CELLULAR_DEVICE_H_
