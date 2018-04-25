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

#include "GEMALTO_CINTERION_CellularNetwork.h"
#include "GEMALTO_CINTERION_CellularStack.h"

using namespace mbed;

GEMALTO_CINTERION_CellularNetwork::GEMALTO_CINTERION_CellularNetwork(ATHandler &atHandler) : AT_CellularNetwork(atHandler)
{
}

GEMALTO_CINTERION_CellularNetwork::~GEMALTO_CINTERION_CellularNetwork()
{
}

#if !defined(NSAPI_PPP_AVAILABLE) || (NSAPI_PPP_AVAILABLE == 0)
NetworkStack *GEMALTO_CINTERION_CellularNetwork::get_stack()
{
    if (!_stack) {
        _stack = new GEMALTO_CINTERION_CellularStack(_at, get_apn(), _cid, _ip_stack_type);
    }
    return _stack;
}
#endif

bool GEMALTO_CINTERION_CellularNetwork::get_modem_stack_type(nsapi_ip_stack_t requested_stack)
{
    return requested_stack == IPV4_STACK ? true : false;
}

bool GEMALTO_CINTERION_CellularNetwork::has_registration(RegistrationType reg_type)
{
    return (reg_type == C_REG || reg_type == C_GREG);
}

const char *GEMALTO_CINTERION_CellularNetwork::get_apn() const
{
    return _apn;
}

nsapi_error_t GEMALTO_CINTERION_CellularNetwork::set_access_technology_impl(RadioAccessTechnology opsAct)
{
    _op_act = RAT_UNKNOWN;
    return NSAPI_ERROR_UNSUPPORTED;
}
