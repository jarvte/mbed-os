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
#if MBED_CONF_APP_CELLULAR_MUX_ENABLED

#ifndef CELLULAR_MUX_DATA_SERVICE_H_
#define CELLULAR_MUX_DATA_SERVICE_H_

#define MUX_DLCI_INVALID_ID       0    /* Invalid DLCI ID. Used to invalidate MuxDataService object. */

//#include "cellular_mux.h"
#include <stdbool.h>
#include "FileHandle.h"

namespace mbed {

class Mux;

class MuxDataService : public FileHandle
{
    friend class Mux;
public:

    /** Enqueue user data for transmission.
     *
     *  @note: This API is only meant to be used for the multiplexer (user) data service tx. Supplied buffer can be
     *         reused/freed upon call return.
     *
     *  @param buffer Begin of the user data.
     *  @param size   The number of bytes to write.
     *  @return       The number of bytes written.
     */
    virtual ssize_t write(const void* buffer, size_t size);

    /** Read user data into a buffer.
     *
     *  @note: This API is only meant to be used for the multiplexer (user) data service rx.
     *
     *  @param buffer The buffer to read to.
     *  @param size   The number of bytes to read.
     *  @return       The number of bytes read, -EAGAIN if no data available for read.
     */
    virtual ssize_t read(void *buffer, size_t size);

    /** Check for poll event flags
     *
     *  The input parameter can be used or ignored - could always return all events, or could check just the events
     *  listed in events.
     *
     *  Call is nonblocking - returns instantaneous state of events.
     *
     * @param events Bitmask of poll events we're interested in - POLLIN/POLLOUT and so on.
     * @return       Bitmask of poll events that have occurred.
     */
    virtual short poll(short events) const;

    /** Not supported by the implementation. */
    virtual off_t seek(off_t offset, int whence = SEEK_SET);

    /** Not supported by the implementation. */
    virtual int close();

    /** Register a callback on completion of enqueued write and read operations.
     *
     *  @note: The registered callback is called within thread context supplied in eventqueue_attach.
     *
     *  @param func Function to call upon event generation.
     */
    virtual void sigio(Callback<void()> func);

    /** Constructor. */
    MuxDataService(Mux &mux);

private:
    /* Owner of this object */
    Mux &_mux;

    /* Deny copy constructor. */
    MuxDataService(const MuxDataService& obj);

    /* Deny assignment operator. */
    MuxDataService& operator=(const MuxDataService& obj);

    uint8_t          _dlci;     /* DLCI number. Valid range 1 - 63. */
    Callback<void()> _sigio_cb; /* Registered signal callback. */
};

} // namespace mbed

#endif /* CELLULAR_MUX_DATA_SERVICE_H_ */

#endif // #if MBED_CONF_APP_CELLULAR_MUX_ENABLED
