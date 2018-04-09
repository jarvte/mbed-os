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

#ifndef GEMALTO_ELS61_CELLULAR_NETWORK_H_
#define GEMALTO_ELS61_CELLULAR_NETWORK_H_

#include "GEMALTO_CINTERION_CellularNetwork.h"

namespace mbed {

class GEMALTO_ELS61_CellularNetwork : public GEMALTO_CINTERION_CellularNetwork
{
public:
    GEMALTO_ELS61_CellularNetwork(ATHandler &atHandler);
    virtual ~GEMALTO_ELS61_CellularNetwork();

protected:
    /**
     * Check if modem supports given registration type.

     * @param reg_type enum RegistrationType
     * @return true if given registration type is supported by modem
     */
    virtual bool has_registration(RegistrationType reg_type);

    virtual bool get_modem_stack_type(nsapi_ip_stack_t requested_stack);

    /**
     * Sets access technology to be scanned.
     *
     * @param opsAct Access technology
     *
     * @return zero on success
     */
    virtual nsapi_error_t set_access_technology_impl(RadioAccessTechnology opsAct);

private:
    const char *get_apn() const;
};
} // namespace mbed
#endif // GEMALTO_ELS61_CELLULAR_NETWORK_H_
