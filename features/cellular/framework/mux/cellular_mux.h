/*
 * Copyright (c) 2006-2017, Arm Limited and affiliates.
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

#ifndef MUX_H
#define MUX_H

#include <stdbool.h>
#include "FileHandle.h"
#include "PlatformMutex.h"
#include "Semaphore.h"
#include "EventQueue.h"
#include "cellular_mux_data_service.h"

#define MUX_CRC_TABLE_LEN         256u /* CRC table length in number of bytes. */

#ifndef MBED_CONF_MUX_DLCI_COUNT
#define MBED_CONF_MUX_DLCI_COUNT  3u   /* Number of supported DLCI IDs. */
#endif
#ifndef MBED_CONF_MUX_BUFFER_SIZE
#define MBED_CONF_MUX_BUFFER_SIZE 104u  /* Size of TX/Rx buffers in number of bytes. */
#endif

/* More RAM needs to allocated if more than 4 DLCI ID to be supported see @ref tx_callback_context for details. */
MBED_STATIC_ASSERT(MBED_CONF_MUX_DLCI_COUNT <= 4u, "");

/* @todo:
I assume that we need to export some kind of #defines for EVENT_SIZE and MAX_EVENT_COUNT (max number of events that can
be queued at the same time by the module inside EventQueue, so that the application designer can calculate the RAM
storage requirements correctly at compile time.
*/


namespace mbed {

class Mux
{
    friend class MuxDataService;
public:

    /* Definition for multiplexer establishment status type. */
    enum MuxEstablishStatus {
        MUX_ESTABLISH_SUCCESS = 0, /* Peer accepted the request. */
        MUX_ESTABLISH_REJECT,      /* Peer rejected the request. */
        MUX_ESTABLISH_TIMEOUT,     /* Timeout occurred for the request. */
        MUX_ESTABLISH_MAX          /* Enumeration upper bound. */
    };

    /* Definition for multiplexer establishment return code type. */
    enum MuxReturnStatus {
        MUX_STATUS_SUCCESS = 0,
        MUX_STATUS_INPROGRESS,
        MUX_STATUS_INVALID_RANGE,
        MUX_STATUS_MUX_NOT_OPEN,
        MUX_STATUS_NO_RESOURCE,
        MUX_STATUS_MAX
    };

    /** Class constructor.
     */
    Mux();

    /** Class destructor.
     */
    virtual ~Mux();

    /** Module init. */
    void module_init();

    /** Establish the multiplexer control channel.
     *
     *  @note: Relevant request specific parameters are fixed at compile time within multiplexer component.
     *  @note: Call returns when response from the peer is received, timeout or write error occurs.
     *
     *  @param status Operation completion code.
     *
     *  @return MUX_STATUS_SUCCESS     Operation completed, check status for completion code.
     *  @return MUX_STATUS_INPROGRESS  Operation not started, control channel open already in progress.
     *  @return MUX_STATUS_NO_RESOURCE Operation not started, multiplexer control channel already open.
     */
    MuxReturnStatus mux_start(MuxEstablishStatus &status);

    /** Establish a DLCI.
     *
     *  @note: Relevant request specific parameters are fixed at compile time within multiplexer component.
     *  @note: Call returns when response from the peer is received or timeout occurs.
     *
     *  @warning: Not allowed to be called from callback context.
     *
     *  @param dlci_id ID of the DLCI to establish. Valid range 1 - 63.
     *  @param status  Operation completion code.
     *  @param obj     Valid object upon status having MUX_ESTABLISH_SUCCESS, NULL upon failure.
     *
     *  @return MUX_STATUS_SUCCESS       Operation completed, check status for completion code.
     *  @return MUX_STATUS_INPROGRESS    Operation not started, DLCI establishment already in progress.
     *  @return MUX_STATUS_INVALID_RANGE Operation not started, DLCI ID not in valid range.
     *  @return MUX_STATUS_MUX_NOT_OPEN  Operation not started, no established multiplexer control channel exists.
     *  @return MUX_STATUS_NO_RESOURCE   Operation not started, dlci_id or all available DLCI ID resources,
     *                                   already in use.
     */
    MuxReturnStatus dlci_establish(uint8_t dlci_id, MuxEstablishStatus &status, FileHandle **obj);

    /** Attach serial interface to the object.
     *
     *  @param serial Serial interface to be used.
     */
    void serial_attach(FileHandle *serial);

    /** Attach EventQueue interface to the object.
     *
     *  @param event_queue Event queue interface to be used.
     */
    void eventqueue_attach(events::EventQueue *event_queue);

private:

    /* Definition for Rx event type. */
    enum RxEvent {
        RX_READ = 0,
        RX_RESUME,
        RX_EVENT_MAX
    };

    /* Definition for Tx state machine. */
    enum TxState {
        TX_IDLE = 0,
        TX_RETRANSMIT_ENQUEUE,
        TX_RETRANSMIT_DONE,
        TX_INTERNAL_RESP,
        TX_NORETRANSMIT,
        TX_STATE_MAX
    };

    /* Definition for Rx state machine. */
    enum RxState {
        RX_FRAME_START = 0,
        RX_HEADER_READ,
        RX_TRAILER_READ,
        RX_SUSPEND,
        RX_STATE_MAX
    };

    /* Definition for frame type within rx path. */
    enum FrameRxType {
        FRAME_RX_TYPE_SABM = 0,
        FRAME_RX_TYPE_UA,
        FRAME_RX_TYPE_DM,
        FRAME_RX_TYPE_DISC,
        FRAME_RX_TYPE_UIH,
        FRAME_RX_TYPE_NOT_SUPPORTED,
        FRAME_RX_TYPE_MAX
    };

    /* Definition for frame type within tx path. */
    enum FrameTxType {
        FRAME_TX_TYPE_SABM = 0,
        FRAME_TX_TYPE_DM,
        FRAME_TX_TYPE_UIH,
        FRAME_TX_TYPE_MAX
    };

    /** Registered time-out expiration event. */
    void on_timeout();

    /** Registered deferred call event in safe (thread context) supplied in eventqueue_attach. */
    void on_deferred_call();

    /** Registered sigio callback from FileHandle. */
    void on_sigio();

    /** Calculate fcs.
     *
     *  @param buffer    Input buffer.
     *  @param input_len Input length in number of bytes.
     *
     *  @return Calculated fcs.
     */
    uint8_t fcs_calculate(const uint8_t *buffer,  uint8_t input_len);

    /** Construct sabm request message.
     *
     *  @param dlci_id ID of the DLCI to establish
     */
    void sabm_request_construct(uint8_t dlci_id);

    /** Construct dm response message.
     */
    void dm_response_construct();

    /** Construct user information frame.
     *
     *  @param dlci_id ID of the DLCI to establish
     *  @param buffer
     *  @param size
     */
    void user_information_construct(uint8_t dlci_id, const void *buffer, size_t size);

    /** Do write operation if pending data available.
     */
    void write_do();

    /** Generate Rx event.
     *
     *  @param event Rx event
     */
    void rx_event_do(RxEvent event);

    /** Rx event frame start read state.
     *
     *  @return Number of bytes read, -EAGAIN if no data available.
     */
    ssize_t on_rx_read_state_frame_start();

    /** Rx event header read state.
     *
     *  @return Number of bytes read, -EAGAIN if no data available.
     */
    ssize_t on_rx_read_state_header_read();

    /** Rx event trailer read state.
     *
     *  @return Number of bytes read, -EAGAIN if no data available.
     */
    ssize_t on_rx_read_state_trailer_read();

    /** Rx event suspend read state.
     *
     *  @return Number of bytes read, -EAGAIN if no data available.
     */
    ssize_t on_rx_read_state_suspend();

    /** Process received SABM frame. */
    void on_rx_frame_sabm();

    /** Process received UA frame. */
    void on_rx_frame_ua();

    /** Process received DM frame. */
    void on_rx_frame_dm();

    /** Process received DISC frame. */
    void on_rx_frame_disc();

    /** Process received UIH frame. */
    void on_rx_frame_uih();

    /** Process received frame, which is not supported. */
    void on_rx_frame_not_supported();

    /** Process valid received frame. */
    void valid_rx_frame_decode();

    /** SABM frame tx path post processing. */
    void on_post_tx_frame_sabm();

    /** DM frame tx path post processing. */
    void on_post_tx_frame_dm();

    /** UIH frame tx path post processing. */
    void on_post_tx_frame_uih();

    /** Resolve rx frame type.
     *
     *  @return Frame type.
     */
    Mux::FrameRxType frame_rx_type_resolve();

    /** Resolve tx frame type.
     *
     *  @return Frame type.
     */
    Mux::FrameTxType frame_tx_type_resolve();

    /** Begin the frame retransmit sequence. */
    void frame_retransmit_begin();

    /** TX state entry functions. */
    void tx_retransmit_enqueu_entry_run();
    void tx_retransmit_done_entry_run();
    void tx_idle_entry_run();
    void tx_internal_resp_entry_run();
    void tx_noretransmit_entry_run();
    typedef void (Mux::*tx_state_entry_func_t)();

    /** TX state exit function. */
    void tx_idle_exit_run();
    typedef void (Mux::*tx_state_exit_func_t)();

    /** Change Tx state machine state.
     *
     *  @param new_state  State to transit.
     *  @param entry_func State entry function.
     *  @param exit_func  State exit function.
     */
    void tx_state_change(TxState new_state, tx_state_entry_func_t entry_func, tx_state_exit_func_t exit_func);

    /** RX state entry functions. */
    void rx_header_read_entry_run();
    typedef void (Mux::*rx_state_entry_func_t)();

    /** Null action function. */
    void null_action();

    /** Change Rx state machine state.
     *
     *  @param new_state  State to transit.
     *  @param entry_func State entry function.
     */
    void rx_state_change(RxState new_state, rx_state_entry_func_t entry_func);

    /** Begin DM frame transmit sequence. */
    void dm_response_send();

    /** Append DLCI ID to storage.
     *
     *  @param dlci_id ID of the DLCI to append.
     */
    void dlci_id_append(uint8_t dlci_id);

    /** Get file handle based on DLCI ID.
     *
     *  @param dlci_id ID of the DLCI used as the key
     *
     *  @return Valid object reference or NULL if not found.
     */
    MuxDataService* file_handle_get(uint8_t dlci_id);

    /** Evaluate is DLCI ID in use.
     *
     *  @param dlci_id ID of the DLCI to evaluate
     *
     *  @return True if in use, false otherwise.
     */
    bool is_dlci_in_use(uint8_t dlci_id);

    /** Evaluate is DLCI ID queue full.
     *
     *  @return True if full, false otherwise.
     */
    bool is_dlci_q_full();

    /** Begin pending self iniated multiplexer open sequence. */
    void pending_self_iniated_mux_open_start();

    /** Begin pending self iniated DLCI establishment sequence. */
    void pending_self_iniated_dlci_open_start();

    /** Begin pending peer iniated DLCI establishment sequence.
     *
     *  @param dlci_id ID of the DLCI to establish.
     */
    void pending_peer_iniated_dlci_open_start(uint8_t dlci_id);

    /** Enqueue user data for transmission.
     *
     *  @note: This API is only meant to be used for the multiplexer (user) data service tx. Supplied buffer can be
     *         reused/freed upon call return.
     *
     *  @param dlci_id ID of the DLCI to use.
     *  @param buffer  Begin of the user data.
     *  @param size    The number of bytes to write.
     *  @return        The number of bytes written, negative error on failure.
     */
    ssize_t user_data_tx(uint8_t dlci_id, const void* buffer, size_t size);

    /** Read user data into a buffer.
     *
     *  @note: This API is only meant to be used for the multiplexer (user) data service rx.
     *
     *  @param buffer The buffer to read in to.
     *  @param size   The number of bytes to read.
     *  @return       The number of bytes read, -EAGAIN if no data availabe for read.
     */
    ssize_t user_data_rx(void* buffer, size_t size);

    /** Check for poll event flags
     *
     * @return Bitmask of poll events that have occurred - POLLIN/POLLOUT.
     */
    short poll();

    /** Clear TX callback pending bit.
     *
     *  @param bit Bit to clear.
     */
    void tx_callback_pending_bit_clear(uint8_t bit);

    /** Set TX callback pending bit for supplied DLCI ID.
     *
     *  @param dlci_id DLCI ID for bit to set.
     */
    void tx_callback_pending_bit_set(uint8_t dlci_id);

    /** Advance the current TX callback index bit.
     *
     *  @return The current TX callback index bit after advanced.
     */
    uint8_t tx_callback_index_advance();

    /** Get the TX callback pending bitmask.
     *
     *  @return TX callback pending bitmask.
     */
    uint8_t tx_callback_pending_mask_get();

    /** Dispatch TX callback based on supplied bit.
     *
     *  @param bit Bit identifier of callback to dispatch.
     */
    void tx_callback_dispatch(uint8_t bit);

    /** Run main processing loop for resolving pending TX callbacks and dispatching them if they exist.
     */
    void tx_callbacks_run();

    /** Get data service object based on supplied bit id.
     *
     *  @param bit Bit identifier of data service object to get.
     *  @return    Data service object reference.
     */
    MuxDataService& tx_callback_lookup(uint8_t bit);

    /** Get minimum of 2 supplied paramaters.
     *
     *  @param size_1 1st param for comparison.
     *  @param size_2 2nd param for comparison.
     *  @return       Minimum of supplied paramaters.
     */
    size_t min(uint8_t size_1, size_t size_2);

    /** Enqueue callback to event queue.
     */
    void event_queue_enqueue();

    /** Verify is FCS valid in RX frame.
     *
     *  @return True upon valid FCS, false otherwise.
     */
    bool is_rx_fcs_valid();

    /* Deny copy constructor. */
    Mux(const Mux& obj);

    /* Deny assignment operator. */
    Mux& operator=(const Mux& obj);

    /* Definition for Tx context type. */
    struct tx_context_t {
        int timer_id;                   /* Timer id. */
        union {
            uint32_t align_4_byte;                      /* Force 4-byte alignment. */
            uint8_t  buffer[MBED_CONF_MUX_BUFFER_SIZE]; /* Tx buffer. */
        };
        uint8_t retransmit_counter;     /* Frame retransmission counter. */
        uint8_t bytes_remaining;        /* Bytes remaining in the buffer to write. */
        uint8_t offset;                 /* Offset in the buffer where to write from. */
        uint8_t tx_callback_context;    /* Context for the TX callback dispatching logic as follows:
                                           - 4 LO bits contain the pending callback mask
                                           - 4 HI bits contain the current bit used for masking */
        TxState tx_state;               /* Tx state machine current state. */

    };

    /* Definition for Rx context type. */
    struct rx_context_t {
        union {
            uint32_t align_4_byte;                      /* Force 4-byte alignment. */
            uint8_t  buffer[MBED_CONF_MUX_BUFFER_SIZE]; /* Rx buffer. */
        };
        uint8_t offset;         /* Offset in the buffer where to read to. */
        uint8_t read_length;    /* Amount to read in number of bytes. */
        RxState rx_state;       /* Rx state machine current state. */
    };

    /* Definition for state type. */
    struct state_t {
        uint16_t is_mux_open              : 1; /* True when multiplexer is open. */
        uint16_t is_mux_open_pending      : 1; /* True when multiplexer open is pending. */
        uint16_t is_mux_open_running      : 1; /* True when multiplexer open is running. */
        uint16_t is_dlci_open_pending     : 1; /* True when DLCI open is pending. */
        uint16_t is_dlci_open_running     : 1; /* True when DLCI open is running. */
        uint16_t is_system_thread_context : 1; /* True when current context is system thread context. */
        uint16_t is_tx_callback_context   : 1; /* True when current context is TX callback context. */
        uint16_t is_user_tx_pending       : 1; /* True when user TX is pending. */
        uint16_t is_user_rx_ready         : 1; /* True when user RX is ready/available. */
    };

    FileHandle*          _serial;                                /* Serial used. */
    events::EventQueue*  _event_q;                               /* Event queue used. */
    rtos::Semaphore      _semaphore;                             /* Semaphore used. */
    PlatformMutex        _mutex;                                 /* Mutex used. */
    MuxDataService       *_mux_objects[MBED_CONF_MUX_DLCI_COUNT];/* Number of supported DLCIs. */
    tx_context_t         _tx_context;                            /* Tx context. */
    rx_context_t         _rx_context;                            /* Rx context. */
    state_t              _state;                                 /* General state context. */
    static const uint8_t _crctable[MUX_CRC_TABLE_LEN];           /* CRC table used for frame FCS. */
    uint8_t              _shared_memory;                         /* Shared memory used passing data between user and
                                                                     system threads. */
};

} // namespace mbed

#endif

#endif // MBED_CONF_APP_CELLULAR_MUX_ENABLED
