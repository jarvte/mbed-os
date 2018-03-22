/**
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech
 ___ _____ _   ___ _  _____ ___  ___  ___ ___
/ __|_   _/_\ / __| |/ / __/ _ \| _ \/ __| __|
\__ \ | |/ _ \ (__| ' <| _| (_) |   / (__| _|
|___/ |_/_/ \_\___|_|\_\_| \___/|_|_\\___|___|
embedded.connectivity.solutions===============

Description: LoRaWAN stack layer that controls both MAC and PHY underneath

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis ( Semtech ), Gregory Cristian ( Semtech ) and Daniel Jaeckle ( STACKFORCE )


Copyright (c) 2017, Arm Limited and affiliates.

SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef MBED_OS_LORAWAN_MAC_MLME_H_
#define MBED_OS_LORAWAN_MAC_MLME_H_

#include "lorawan/system/lorawan_data_structures.h"
#include "lorastack/phy/LoRaPHY.h"

// forward declaration
class LoRaMac;

class LoRaMacMlme {

public:

    /** Constructor
     *
     * Sets local handles to NULL. These handles will be set when the subsystem
     * is activated by the MAC layer.
     */
    LoRaMacMlme();

    /** Destructor
     *
     * Does nothing
     */
    ~LoRaMacMlme();

    /**
     * @brief reset_confirmation Resets the confirmation struct
     */
    void reset_confirmation();

    /** Activating MLME subsystem
     *
     * Stores pointers to MAC and PHY layer handles
     *
     * @param phy    pointer to PHY layer
     */
    void activate_mlme_subsystem(LoRaPHY *phy);

    /** Grants access to MLME confirmation data
     *
     * @return               a reference to MLME confirm data structure
     */
    loramac_mlme_confirm_t& get_confirmation();

    /** Grants access to MLME indication data
     *
     * @return               a reference to MLME indication data structure
     */
    loramac_mlme_indication_t& get_indication();

    /**
     * @brief set_tx_continuous_wave Puts the system in continuous transmission mode
     * @param [in] channel A Channel to use
     * @param [in] datarate A datarate to use
     * @param [in] tx_power A RF output power to use
     * @param [in] max_eirp A maximum possible EIRP to use
     * @param [in] antenna_gain Antenna gain to use
     * @param [in] timeout Time in seconds while the radio is kept in continuous wave mode
     */
    void set_tx_continuous_wave(uint8_t channel, int8_t datarate, int8_t tx_power,
                                float max_eirp, float antenna_gain, uint16_t timeout);

private:

    /**
     * Pointer to PHY handle
     */
    LoRaPHY *_lora_phy;

    /**
     * Structure to hold MLME indication data.
     */
    loramac_mlme_indication_t indication;

    /**
     * Structure to hold MLME confirm data.
     */
    loramac_mlme_confirm_t confirmation;
};

#endif /* MBED_OS_LORAWAN_MAC_MLME_H_ */
