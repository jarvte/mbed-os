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

#include <SIMCom/SIM7020/SIMCom_SIM7020.h>
#include <SIMCom/SIM7020/SIMCom_SIM7020_CellularContext.h>
#include <SIMCom/SIM7020/SIMCom_SIM7020_CellularInformation.h>
#include <SIMCom/SIM7020/SIMCom_SIM7020_CellularNetwork.h>

using namespace mbed;

#define DEVICE_READY_URC "CPIN:"

static const intptr_t cellular_properties[AT_CellularBase::PROPERTY_MAX] = {
    AT_CellularNetwork::RegistrationModeLAC,    // C_EREG
    AT_CellularNetwork::RegistrationModeLAC,    // C_GREG
    AT_CellularNetwork::RegistrationModeLAC,    // C_REG
    0,  // AT_CGSN_WITH_TYPE
    0,  // AT_CGDATA
    1,  // AT_CGAUTH
    1,  // AT_CNMI
    1,  // AT_CSMP
    1,  // AT_CMGF
    1,  // AT_CSDH
    1,  // PROPERTY_IPV4_STACK
    0,  // PROPERTY_IPV6_STACK
    0,  // PROPERTY_IPV4V6_STACK
    1,  // PROPERTY_NON_IP_PDP_TYPE
    1,  // PROPERTY_AT_CGEREP
};

SIMCom_SIM7020::SIMCom_SIM7020(FileHandle *fh) : AT_CellularDevice(fh)
{
    AT_CellularBase::set_cellular_properties(cellular_properties);
}

SIMCom_SIM7020::~SIMCom_SIM7020()
{
}

AT_CellularNetwork *SIMCom_SIM7020::open_network_impl(ATHandler &at)
{
    return new SIMCom_SIM7020_CellularNetwork(at);
}

AT_CellularContext *SIMCom_SIM7020::create_context_impl(ATHandler &at, const char *apn, bool cp_req, bool nonip_req)
{
    return new SIMCom_SIM7020_CellularContext(at, this, apn, cp_req, nonip_req);
}

AT_CellularInformation *SIMCom_SIM7020::open_information_impl(ATHandler &at)
{
    return new SIMCom_SIM7020_CellularInformation(at);
}

void SIMCom_SIM7020::set_ready_cb(Callback<void()> callback)
{
    _at->set_urc_handler(DEVICE_READY_URC, callback);
}

#if MBED_CONF_SIMCOM_SIM7020_PROVIDE_DEFAULT
#include "UARTSerial.h"
CellularDevice *CellularDevice::get_default_instance()
{
    static UARTSerial serial(MBED_CONF_SIMCOM_SIM7020_TX,
                             MBED_CONF_SIMCOM_SIM7020_RX,
                             MBED_CONF_SIMCOM_SIM7020_BAUDRATE);

    static SIMCom_SIM7020 device(&serial);
    return &device;
}
#endif
