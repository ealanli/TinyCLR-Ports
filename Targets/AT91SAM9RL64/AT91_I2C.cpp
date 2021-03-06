// Copyright Microsoft Corporation
// Copyright GHI Electronics, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "AT91.h"

struct AT91_I2c_Configuration {

    int32_t                  address;
    uint8_t                  clockRate;     // primary clock factor to generate the i2c clock
    uint8_t                  clockRate2;   // additional clock factors, if more than one is needed for the clock (optional)

    bool                     initialized;
};

#define I2C_TRANSACTION_TIMEOUT 2000000

static AT91_I2c_Configuration g_I2cConfiguration;
static TinyCLR_I2c_Provider i2cProvider;
static TinyCLR_Api_Info i2cApi;

const TinyCLR_Api_Info* AT91_I2c_GetApi() {
    i2cProvider.Parent = &i2cApi;
    i2cProvider.Index = 0;
    i2cProvider.Acquire = &AT91_I2c_Acquire;
    i2cProvider.Release = &AT91_I2c_Release;
    i2cProvider.SetActiveSettings = &AT91_I2c_SetActiveSettings;
    i2cProvider.Read = &AT91_I2c_ReadTransaction;
    i2cProvider.Write = &AT91_I2c_WriteTransaction;
    i2cProvider.WriteRead = &AT91_I2c_WriteReadTransaction;

    i2cApi.Author = "GHI Electronics, LLC";
    i2cApi.Name = "GHIElectronics.TinyCLR.NativeApis.AT91.I2cProvider";
    i2cApi.Type = TinyCLR_Api_Type::I2cProvider;
    i2cApi.Version = 0;
    i2cApi.Count = 1;
    i2cApi.Implementation = &i2cProvider;

    return &i2cApi;
}

TinyCLR_Result AT91_I2c_ReadTransaction(const TinyCLR_I2c_Provider* self, uint8_t* buffer, size_t& length, TinyCLR_I2c_TransferStatus& result) {
    int32_t timeout = I2C_TRANSACTION_TIMEOUT;

    uint32_t address;
    uint32_t control = 0;

    size_t bytesToTransfer = length;
    size_t bytesTransferred = 0;

    AT91_I2C& I2C = AT91::I2C();

    address = (g_I2cConfiguration.address << AT91_I2C::TWI_MMR_DADR_SHIFT) | AT91_I2C::TWI_MMR_MREAD_R;


    I2C.TWI_CWGR = g_I2cConfiguration.clockRate | (g_I2cConfiguration.clockRate << AT91_I2C::TWI_CWGR_CHDIV_SHIFT) | (g_I2cConfiguration.clockRate2 << AT91_I2C::TWI_CWGR_CKDIV_SHIFT);

    control = AT91_I2C::TWI_CR_MSEN | AT91_I2C::TWI_CR_SVDIS;

    control |= AT91_I2C::TWI_CR_START;

    I2C.TWI_MMR = address;

    I2C.TWI_CR = control;

    while (bytesToTransfer > 0 && timeout > 0) {
        if (bytesToTransfer == 1) {
            I2C.TWI_CR = AT91_I2C::TWI_CR_STOP;
        }

        while (!(I2C.TWI_SR & AT91_I2C::TWI_SR_RXRDY) && timeout > 0) {
            AT91_Time_Delay(nullptr, 1);

            timeout--;
        }

        buffer[bytesTransferred] = I2C.TWI_RHR;
        bytesToTransfer--;
        bytesTransferred++;
        // Reset Timeout
        if (timeout > 0) {
            timeout = I2C_TRANSACTION_TIMEOUT;
        }
    }

    // Reset Timeout
    if (timeout > 0) {
        timeout = I2C_TRANSACTION_TIMEOUT;
    }

    while (!(I2C.TWI_SR & AT91_I2C::TWI_SR_TXCOMP) && timeout > 0) {
        AT91_Time_Delay(nullptr, 1);

        timeout--;
    }

    I2C.TWI_CR = AT91_I2C::TWI_CR_MSDIS;

    length = bytesTransferred;

    if (timeout > 0)
        result = TinyCLR_I2c_TransferStatus::FullTransfer;
    else
        result = TinyCLR_I2c_TransferStatus::ClockStretchTimeout;

    return timeout > 0 ? TinyCLR_Result::Success : TinyCLR_Result::TimedOut;
}

TinyCLR_Result AT91_I2c_WriteTransaction(const TinyCLR_I2c_Provider* self, const uint8_t* buffer, size_t& length, TinyCLR_I2c_TransferStatus& result) {
    int32_t timeout = I2C_TRANSACTION_TIMEOUT;

    uint32_t address;
    uint32_t control = 0;

    size_t bytesToTransfer = length;
    size_t bytesTransferred = 0;

    AT91_I2C& I2C = AT91::I2C();

    address = g_I2cConfiguration.address << AT91_I2C::TWI_MMR_DADR_SHIFT;

    I2C.TWI_CWGR = g_I2cConfiguration.clockRate | (g_I2cConfiguration.clockRate << AT91_I2C::TWI_CWGR_CHDIV_SHIFT) | (g_I2cConfiguration.clockRate2 << AT91_I2C::TWI_CWGR_CKDIV_SHIFT);

    control = AT91_I2C::TWI_CR_MSEN | AT91_I2C::TWI_CR_SVDIS;

    control |= AT91_I2C::TWI_CR_START;

    I2C.TWI_MMR = address;

    I2C.TWI_CR = control;

    while (bytesToTransfer > 0 && timeout > 0) {
        while (!(I2C.TWI_SR & AT91_I2C::TWI_SR_TXRDY) && timeout > 0) {
            AT91_Time_Delay(nullptr, 1);

            timeout--;
        }

        // Reset Timeout
        if (timeout > 0) {
            timeout = I2C_TRANSACTION_TIMEOUT;
        }

        I2C.TWI_THR = buffer[bytesTransferred];

        bytesTransferred++;
        bytesToTransfer--;

    }

    // Reset Timeout
    if (timeout > 0) {
        timeout = I2C_TRANSACTION_TIMEOUT;
    }

    while (!(I2C.TWI_SR & AT91_I2C::TWI_SR_TXRDY) && timeout > 0) {
        AT91_Time_Delay(nullptr, 1);

        timeout--;
    }

    I2C.TWI_CR = AT91_I2C::TWI_CR_STOP;

    // Reset Timeout
    if (timeout > 0) {
        timeout = I2C_TRANSACTION_TIMEOUT;
    }

    while (!(I2C.TWI_SR & AT91_I2C::TWI_SR_TXCOMP) && timeout > 0) {
        AT91_Time_Delay(nullptr, 1);

        timeout--;
    }

    I2C.TWI_CR = AT91_I2C::TWI_CR_MSDIS;

    length = bytesTransferred;

    if (timeout > 0)
        result = TinyCLR_I2c_TransferStatus::FullTransfer;
    else
        result = TinyCLR_I2c_TransferStatus::ClockStretchTimeout;

    return timeout > 0 ? TinyCLR_Result::Success : TinyCLR_Result::TimedOut;
}

TinyCLR_Result AT91_I2c_WriteReadTransaction(const TinyCLR_I2c_Provider* self, const uint8_t* writeBuffer, size_t& writeLength, uint8_t* readBuffer, size_t& readLength, TinyCLR_I2c_TransferStatus& result) {
    AT91_I2c_WriteTransaction(self, writeBuffer, writeLength, result);

    if (result == TinyCLR_I2c_TransferStatus::FullTransfer)
        AT91_I2c_ReadTransaction(self, readBuffer, readLength, result);

    return result == TinyCLR_I2c_TransferStatus::FullTransfer ? TinyCLR_Result::Success : TinyCLR_Result::TimedOut;
}

#define CLOCK_RATE_CONSTANT     4
#define MIN_CLK_RATE    (AT91_SYSTEM_PERIPHERAL_CLOCK_HZ/1000)/(128 *255+CLOCK_RATE_CONSTANT)
#define MAX_CLK_RATE    400   //kHz

TinyCLR_Result AT91_I2c_SetActiveSettings(const TinyCLR_I2c_Provider* self, int32_t slaveAddress, TinyCLR_I2c_BusSpeed busSpeed) {
    uint32_t rateKhz;
    uint8_t clockRate, clockRate2;

    if (busSpeed == TinyCLR_I2c_BusSpeed::FastMode)
        rateKhz = 400; // FastMode
    else if (busSpeed == TinyCLR_I2c_BusSpeed::StandardMode)
        rateKhz = 100; // StandardMode
    else
        return TinyCLR_Result::NotSupported;

    if (rateKhz < MIN_CLK_RATE) {
        clockRate = 255;
        clockRate2 = 7;
    }
    else if (rateKhz >= MAX_CLK_RATE) {

        clockRate = (AT91_SYSTEM_PERIPHERAL_CLOCK_HZ / (2 * 1000)) / MAX_CLK_RATE - CLOCK_RATE_CONSTANT;
        clockRate2 = 0;
    }
    else {

        uint32_t power = 1;
        uint32_t clkDiv;
        uint32_t clkLHDiv;

        clkDiv = 0;
        clkLHDiv = (AT91_SYSTEM_PERIPHERAL_CLOCK_HZ / (2 * 1000)) / rateKhz - CLOCK_RATE_CONSTANT;

        if (clkLHDiv > 255) {
            clkLHDiv += CLOCK_RATE_CONSTANT;
            for (clkDiv = 1; clkDiv <= 7; clkDiv++) {
                clkLHDiv /= 2;
                power *= 2; // save the calculation
                if (clkLHDiv <= 255)
                    break;
            }

            clkLHDiv = (((AT91_SYSTEM_PERIPHERAL_CLOCK_HZ / (2 * 1000)) / rateKhz) - CLOCK_RATE_CONSTANT) / power;
        }
        clockRate = clkLHDiv;
        clockRate2 = clkDiv;
    }

    g_I2cConfiguration.clockRate = (uint8_t)clockRate; // low byte
    g_I2cConfiguration.clockRate2 = (uint8_t)(clockRate2); // high byte
    g_I2cConfiguration.address = slaveAddress;

    return TinyCLR_Result::Success;
}

static const AT91_Gpio_Pin g_at91_i2c_scl_pin[] = AT91_I2C_SCL_PINS;
static const AT91_Gpio_Pin g_at91_i2c_sda_pin[] = AT91_I2C_SDA_PINS;

TinyCLR_Result AT91_I2c_Acquire(const TinyCLR_I2c_Provider* self) {
    AT91_I2C& I2C = AT91::I2C();
    AT91_PMC &pmc = AT91::PMC();

    if (!g_I2cConfiguration.initialized) {
        if (!AT91_Gpio_OpenPin(g_at91_i2c_sda_pin[self->Index].number) || !AT91_Gpio_OpenPin(g_at91_i2c_scl_pin[self->Index].number))
            return TinyCLR_Result::SharingViolation;

        AT91_Gpio_ConfigurePin(g_at91_i2c_sda_pin[self->Index].number, AT91_Gpio_Direction::Input, g_at91_i2c_sda_pin[self->Index].peripheralSelection, AT91_Gpio_ResistorMode::Inactive);
        AT91_Gpio_ConfigurePin(g_at91_i2c_scl_pin[self->Index].number, AT91_Gpio_Direction::Input, g_at91_i2c_scl_pin[self->Index].peripheralSelection, AT91_Gpio_ResistorMode::Inactive);

        pmc.EnablePeriphClock(AT91C_ID_TWI);
        I2C.TWI_MMR = 0x7e << AT91_I2C::TWI_MMR_DADR_SHIFT;

        g_I2cConfiguration.initialized = true;
    }

    return TinyCLR_Result::Success;
}

TinyCLR_Result AT91_I2c_Release(const TinyCLR_I2c_Provider* self) {
    AT91_I2C& I2C = AT91::I2C();

    I2C.TWI_CR = AT91_I2C::TWI_CR_SWRST;
    if (g_I2cConfiguration.initialized) {


        AT91_Interrupt_Disable(AT91C_ID_TWI);

        AT91_PMC &pmc = AT91::PMC();
        pmc.DisablePeriphClock(AT91C_ID_TWI);

        // disable
        I2C.TWI_CR = AT91_I2C::TWI_CR_MSDIS;

        // disable all the interrupt
        I2C.TWI_IDR = AT91_I2C::TWI_IDR_NACK | AT91_I2C::TWI_IDR_RXRDY | AT91_I2C::TWI_IDR_TXCOMP | AT91_I2C::TWI_IDR_TXRDY;


        g_I2cConfiguration.initialized = false;

        AT91_Gpio_ClosePin(g_at91_i2c_sda_pin[self->Index].number);
        AT91_Gpio_ClosePin(g_at91_i2c_scl_pin[self->Index].number);
    }

    return TinyCLR_Result::Success;
}

void AT91_I2c_Reset() {
    AT91_I2c_Release(&i2cProvider);
}

