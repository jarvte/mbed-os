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

namespace mbed
{

CellularDevice::CellularDevice() : _state_machine(0), _fh(0)
{
}

nsapi_error_t CellularDevice::init(FileHandle *fh, events::EventQueue &queue)
{
    tr_info("CELLULAR_DEVICE: %s", CELLULAR_STRINGIFY(CELLULAR_DEVICE));

    CellularPower* power = open_power(fh);
    if (!power) {
        tr_error("Could not create power");
        return NSAPI_ERROR_NO_MEMORY;
    }
    _state_machine = new CellularStateMachine(power, queue);

    if (!_state_machine) {
        tr_error("Could not create state machine");
        return NSAPI_ERROR_NO_MEMORY;
    }

    _fh = fh;

    return NSAPI_ERROR_OK;
}

CellularStateMachine* CellularDevice::get_state_machine()
{
    return _state_machine;
}

nsapi_error_t CellularDevice::CellularDevice::connect()
{
    return _state_machine->continue_to_state(CellularStateMachine::STATE_CONNECTED);
}

void CellularDevice::set_sim_pin(const char* pin)
{
    _state_machine->set_sim_pin(pin);
}

} // namespace mbed
