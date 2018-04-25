/*
 * Copyright (c) 2018, Arm Limited and affiliates.
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

#include <GEMALTO_ELS61_CellularPower.h>

#define DEVICE_READY_URC "+PBREADY"

using namespace mbed;

GEMALTO_ELS61_CellularPower::GEMALTO_ELS61_CellularPower(ATHandler &atHandler) : AT_CellularPower(atHandler)
{
}

nsapi_error_t GEMALTO_ELS61_CellularPower::set_device_ready_urc_cb(Callback<void()> callback)
{
    return _at.set_urc_handler(DEVICE_READY_URC, callback);
}

void GEMALTO_ELS61_CellularPower::remove_device_ready_urc_cb(Callback<void()> callback)
{
    _at.remove_urc_handler(DEVICE_READY_URC, callback);
}
