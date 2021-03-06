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

#include <algorithm>
#include "LPC24.h"

struct LPC24_Uart_Controller {
    uint8_t                             TxBuffer[LPC24_UART_TX_BUFFER_SIZE];
    uint8_t                             RxBuffer[LPC24_UART_RX_BUFFER_SIZE];

    size_t                              txBufferCount;
    size_t                              txBufferIn;
    size_t                              txBufferOut;

    size_t                              rxBufferCount;
    size_t                              rxBufferIn;
    size_t                              rxBufferOut;

    bool                                isOpened;
    bool                                handshakeEnable;

    TinyCLR_Uart_ErrorReceivedHandler   errorEventHandler;
    TinyCLR_Uart_DataReceivedHandler    dataReceivedEventHandler;

    const TinyCLR_Uart_Provider*        provider;

};

static LPC24_Uart_Controller g_LPC24_Uart_Controller[TOTAL_UART_CONTROLLERS];

#define SET_BITS(Var,Shift,Mask,fieldsMask) {Var = setFieldValue(Var,Shift,Mask,fieldsMask);}

static uint8_t uartProviderDefs[TOTAL_UART_CONTROLLERS * sizeof(TinyCLR_Uart_Provider)];
static TinyCLR_Uart_Provider* uartProviders[TOTAL_UART_CONTROLLERS];
static TinyCLR_Api_Info uartApi;

uint32_t setFieldValue(volatile uint32_t oldVal, uint32_t shift, uint32_t mask, uint32_t val) {
    volatile uint32_t temp = oldVal;

    temp &= ~mask;
    temp |= val << shift;
    return temp;
}

const TinyCLR_Api_Info* LPC24_Uart_GetApi() {

    for (int i = 0; i < TOTAL_UART_CONTROLLERS; i++) {
        uartProviders[i] = (TinyCLR_Uart_Provider*)(uartProviderDefs + (i * sizeof(TinyCLR_Uart_Provider)));
        uartProviders[i]->Parent = &uartApi;
        uartProviders[i]->Index = i;
        uartProviders[i]->Acquire = &LPC24_Uart_Acquire;
        uartProviders[i]->Release = &LPC24_Uart_Release;
        uartProviders[i]->SetActiveSettings = &LPC24_Uart_SetActiveSettings;
        uartProviders[i]->Flush = &LPC24_Uart_Flush;
        uartProviders[i]->Read = &LPC24_Uart_Read;
        uartProviders[i]->Write = &LPC24_Uart_Write;
        uartProviders[i]->SetPinChangedHandler = &LPC24_Uart_SetPinChangedHandler;
        uartProviders[i]->SetErrorReceivedHandler = &LPC24_Uart_SetErrorReceivedHandler;
        uartProviders[i]->SetDataReceivedHandler = &LPC24_Uart_SetDataReceivedHandler;
        uartProviders[i]->GetBreakSignalState = LPC24_Uart_GetBreakSignalState;
        uartProviders[i]->SetBreakSignalState = LPC24_Uart_SetBreakSignalState;
        uartProviders[i]->GetCarrierDetectState = LPC24_Uart_GetCarrierDetectState;
        uartProviders[i]->GetClearToSendState = LPC24_Uart_GetClearToSendState;
        uartProviders[i]->GetDataReadyState = LPC24_Uart_GetDataReadyState;
        uartProviders[i]->GetIsDataTerminalReadyEnabled = LPC24_Uart_GetIsDataTerminalReadyEnabled;
        uartProviders[i]->SetIsDataTerminalReadyEnabled = LPC24_Uart_SetIsDataTerminalReadyEnabled;
        uartProviders[i]->GetIsRequestToSendEnabled = LPC24_Uart_GetIsRequestToSendEnabled;
        uartProviders[i]->SetIsRequestToSendEnabled = LPC24_Uart_SetIsRequestToSendEnabled;
    }

    uartApi.Author = "GHI Electronics, LLC";
    uartApi.Name = "GHIElectronics.TinyCLR.NativeApis.LPC24.UartProvider";
    uartApi.Type = TinyCLR_Api_Type::UartProvider;
    uartApi.Version = 0;
    uartApi.Count = TOTAL_UART_CONTROLLERS;
    uartApi.Implementation = uartProviders;

    return &uartApi;
}


void LPC24_Uart_PinConfiguration(int portNum, bool enable) {
    DISABLE_INTERRUPTS_SCOPED(irq);

    uint32_t txPin = LPC24_Uart_GetTxPin(portNum);
    uint32_t rxPin = LPC24_Uart_GetRxPin(portNum);
    uint32_t ctsPin = LPC24_Uart_GetCtsPin(portNum);
    uint32_t rtsPin = LPC24_Uart_GetRtsPin(portNum);

    LPC24_Gpio_PinFunction txPinMode = LPC24_Uart_GetTxAlternateFunction(portNum);
    LPC24_Gpio_PinFunction rxPinMode = LPC24_Uart_GetRxAlternateFunction(portNum);
    LPC24_Gpio_PinFunction ctsPinMode = LPC24_Uart_GetCtsAlternateFunction(portNum);
    LPC24_Gpio_PinFunction rtsPinMode = LPC24_Uart_GetRtsAlternateFunction(portNum);

    if (enable) {
        // Connect pin to UART
        LPC24_Gpio_ConfigurePin(txPin, LPC24_Gpio_Direction::Input, txPinMode, LPC24_Gpio_PinMode::Inactive);
        // Connect pin to UART
        LPC24_Gpio_ConfigurePin(rxPin, LPC24_Gpio_Direction::Input, rxPinMode, LPC24_Gpio_PinMode::Inactive);

        LPC24_Uart_TxBufferEmptyInterruptEnable(portNum, true);

        LPC24_Uart_RxBufferFullInterruptEnable(portNum, true);

        if (g_LPC24_Uart_Controller[portNum].handshakeEnable) {
            LPC24_Gpio_ConfigurePin(ctsPin, LPC24_Gpio_Direction::Input, ctsPinMode, LPC24_Gpio_PinMode::Inactive);
            LPC24_Gpio_ConfigurePin(rtsPin, LPC24_Gpio_Direction::Input, rtsPinMode, LPC24_Gpio_PinMode::Inactive);
        }

    }
    else {

        LPC24_Uart_TxBufferEmptyInterruptEnable(portNum, false);
        // TODO Add config for uart pin protected state
        LPC24_Gpio_ConfigurePin(txPin, LPC24_Gpio_Direction::Input, LPC24_Gpio_PinFunction::PinFunction0, LPC24_Gpio_PinMode::Inactive);

        LPC24_Uart_RxBufferFullInterruptEnable(portNum, false);
        // TODO Add config for uart pin protected state
        LPC24_Gpio_ConfigurePin(rxPin, LPC24_Gpio_Direction::Input, LPC24_Gpio_PinFunction::PinFunction0, LPC24_Gpio_PinMode::Inactive);

        if (g_LPC24_Uart_Controller[portNum].handshakeEnable) {
            LPC24_Gpio_ConfigurePin(ctsPin, LPC24_Gpio_Direction::Input, LPC24_Gpio_PinFunction::PinFunction0, LPC24_Gpio_PinMode::Inactive);
            LPC24_Gpio_ConfigurePin(rtsPin, LPC24_Gpio_Direction::Input, LPC24_Gpio_PinFunction::PinFunction0, LPC24_Gpio_PinMode::Inactive);
        }
    }
}

void LPC24_Uart_SetErrorEvent(int32_t portNum, TinyCLR_Uart_Error error) {
    if (g_LPC24_Uart_Controller[portNum].errorEventHandler != nullptr)
        g_LPC24_Uart_Controller[portNum].errorEventHandler(g_LPC24_Uart_Controller[portNum].provider, error);
}

void LPC24_Uart_ReceiveData(int portNum, uint32_t LSR_Value, uint32_t IIR_Value) {
    INTERRUPT_STARTED_SCOPED(isr);

    DISABLE_INTERRUPTS_SCOPED(irq);

    LPC24XX_USART& USARTC = LPC24XX::UART(portNum);

    // Read data from Rx FIFO
    if (USARTC.SEL2.IER.UART_IER & (LPC24XX_USART::UART_IER_RDAIE)) {
        if ((LSR_Value & LPC24XX_USART::UART_LSR_RFDR) || (IIR_Value == LPC24XX_USART::UART_IIR_IID_Irpt_RDA) || (IIR_Value == LPC24XX_USART::UART_IIR_IID_Irpt_TOUT)) {
            do {
                uint8_t rxdata = (uint8_t)USARTC.SEL1.RBR.UART_RBR;

                if (0 == (LSR_Value & (LPC24XX_USART::UART_LSR_PEI | LPC24XX_USART::UART_LSR_OEI | LPC24XX_USART::UART_LSR_FEI))) {
                    if (g_LPC24_Uart_Controller[portNum].rxBufferCount == LPC24_UART_RX_BUFFER_SIZE) {
                        LPC24_Uart_SetErrorEvent(portNum, TinyCLR_Uart_Error::ReceiveFull);

                        continue;
                    }

                    g_LPC24_Uart_Controller[portNum].RxBuffer[g_LPC24_Uart_Controller[portNum].rxBufferIn++] = rxdata;

                    g_LPC24_Uart_Controller[portNum].rxBufferCount++;

                    if (g_LPC24_Uart_Controller[portNum].rxBufferIn == LPC24_UART_RX_BUFFER_SIZE)
                        g_LPC24_Uart_Controller[portNum].rxBufferIn = 0;

                    if (g_LPC24_Uart_Controller[portNum].dataReceivedEventHandler != nullptr)
                        g_LPC24_Uart_Controller[portNum].dataReceivedEventHandler(g_LPC24_Uart_Controller[portNum].provider, 1);
                }

                LSR_Value = USARTC.UART_LSR;

                if (LSR_Value & 0x04) {
                    LPC24_Uart_SetErrorEvent(portNum, TinyCLR_Uart_Error::ReceiveParity);
                }
                else if ((LSR_Value & 0x08) || (LSR_Value & 0x80)) {
                    LPC24_Uart_SetErrorEvent(portNum, TinyCLR_Uart_Error::Frame);
                }
                else if (LSR_Value & 0x02) {
                    LPC24_Uart_SetErrorEvent(portNum, TinyCLR_Uart_Error::BufferOverrun);
                }
            } while (LSR_Value & LPC24XX_USART::UART_LSR_RFDR);
        }
    }
}
void LPC24_Uart_TransmitData(int portNum, uint32_t LSR_Value, uint32_t IIR_Value) {
    INTERRUPT_STARTED_SCOPED(isr);

    DISABLE_INTERRUPTS_SCOPED(irq);

    LPC24XX_USART& USARTC = LPC24XX::UART(portNum);

    // Send data
    if ((LSR_Value & LPC24XX_USART::UART_LSR_TE) || (IIR_Value == LPC24XX_USART::UART_IIR_IID_Irpt_THRE)) {
        // Check if CTS is high
        if (LPC24_Uart_TxHandshakeEnabledState(portNum)) {
            if (g_LPC24_Uart_Controller[portNum].txBufferCount > 0) {
                uint8_t txdata = g_LPC24_Uart_Controller[portNum].TxBuffer[g_LPC24_Uart_Controller[portNum].txBufferOut++];

                g_LPC24_Uart_Controller[portNum].txBufferCount--;

                if (g_LPC24_Uart_Controller[portNum].txBufferOut == LPC24_UART_TX_BUFFER_SIZE)
                    g_LPC24_Uart_Controller[portNum].txBufferOut = 0;

                USARTC.SEL1.THR.UART_THR = txdata; // write TX data

            }
            else {
                LPC24_Uart_TxBufferEmptyInterruptEnable(portNum, false); // Disable interrupt when no more data to send.
            }
        }
    }
}

void LPC24_Uart_InterruptHandler(void *param) {
    INTERRUPT_STARTED_SCOPED(isr);

    DISABLE_INTERRUPTS_SCOPED(irq);

    uint32_t portNum = (uint32_t)param;

    LPC24XX_USART& USARTC = LPC24XX::UART(portNum);
    volatile uint32_t LSR_Value = USARTC.UART_LSR;                     // Store LSR value since it's Read-to-Clear
    volatile uint32_t IIR_Value = USARTC.SEL3.IIR.UART_IIR & LPC24XX_USART::UART_IIR_IID_mask;

    if (LSR_Value & 0x04) {
        LPC24_Uart_SetErrorEvent(portNum, TinyCLR_Uart_Error::ReceiveParity);
    }
    else if ((LSR_Value & 0x08) || (LSR_Value & 0x80)) {
        LPC24_Uart_SetErrorEvent(portNum, TinyCLR_Uart_Error::Frame);
    }
    else if (LSR_Value & 0x02) {
        LPC24_Uart_SetErrorEvent(portNum, TinyCLR_Uart_Error::BufferOverrun);
    }

    LPC24_Uart_ReceiveData(portNum, LSR_Value, IIR_Value);

    LPC24_Uart_TransmitData(portNum, LSR_Value, IIR_Value);
}


TinyCLR_Result LPC24_Uart_Acquire(const TinyCLR_Uart_Provider* self) {
    int32_t portNum = self->Index;

    if (portNum >= TOTAL_UART_CONTROLLERS)
        return TinyCLR_Result::ArgumentInvalid;

    DISABLE_INTERRUPTS_SCOPED(irq);

    g_LPC24_Uart_Controller[portNum].txBufferCount = 0;
    g_LPC24_Uart_Controller[portNum].txBufferIn = 0;
    g_LPC24_Uart_Controller[portNum].txBufferOut = 0;

    g_LPC24_Uart_Controller[portNum].rxBufferCount = 0;
    g_LPC24_Uart_Controller[portNum].rxBufferIn = 0;
    g_LPC24_Uart_Controller[portNum].rxBufferOut = 0;

    g_LPC24_Uart_Controller[portNum].provider = self;

    switch (portNum) {
    case 0:
        LPC24XX::SYSCON().PCONP |= PCONP_PCUART0;
        break;

    case 1:
        LPC24XX::SYSCON().PCONP |= PCONP_PCUART1;
        break;

    case 2:
        LPC24XX::SYSCON().PCONP |= PCONP_PCUART2;
        break;

    case 3:
        LPC24XX::SYSCON().PCONP |= PCONP_PCUART3;
        break;
    }

    return TinyCLR_Result::Success;
}

void LPC24_Uart_SetClock(int32_t portNum, int32_t pclkSel) {
    pclkSel &= 0x03;

    switch (portNum) {
    case 0:

        LPC24XX::SYSCON().PCLKSEL0 &= ~(0x03 << 6);
        LPC24XX::SYSCON().PCLKSEL0 |= (pclkSel << 6);

        break;

    case 1:

        LPC24XX::SYSCON().PCLKSEL0 &= ~(0x03 << 8);
        LPC24XX::SYSCON().PCLKSEL0 |= (pclkSel << 8);

        break;

    case 2:
        LPC24XX::SYSCON().PCLKSEL1 &= ~(0x03 << 16);
        LPC24XX::SYSCON().PCLKSEL1 |= (pclkSel << 16);
        break;

    case 3:
        LPC24XX::SYSCON().PCLKSEL1 &= ~(0x03 << 18);
        LPC24XX::SYSCON().PCLKSEL1 |= (pclkSel << 18);
        break;

    }
}
TinyCLR_Result LPC24_Uart_SetActiveSettings(const TinyCLR_Uart_Provider* self, uint32_t baudRate, uint32_t dataBits, TinyCLR_Uart_Parity parity, TinyCLR_Uart_StopBitCount stopBits, TinyCLR_Uart_Handshake handshaking) {

    DISABLE_INTERRUPTS_SCOPED(irq);

    int32_t portNum = self->Index;

    LPC24XX_USART& USARTC = LPC24XX::UART(portNum);

    uint32_t divisor;
    uint32_t fdr;
    bool   fRet = true;

    switch (baudRate) {

    case 2400: LPC24_Uart_SetClock(portNum, 0); fdr = 0x41; divisor = 0x177; break;

    case 4800: LPC24_Uart_SetClock(portNum, 0); fdr = 0xE3; divisor = 0xC1; break;

    case 9600: LPC24_Uart_SetClock(portNum, 0); fdr = 0xC7; divisor = 0x4A; break;

    case 14400: LPC24_Uart_SetClock(portNum, 0); fdr = 0xA1; divisor = 0x47; break;

    case 19200: LPC24_Uart_SetClock(portNum, 0); fdr = 0xC7; divisor = 0x25; break;

    case 38400: LPC24_Uart_SetClock(portNum, 0); fdr = 0xB3; divisor = 0x17; break;

    case 57600: LPC24_Uart_SetClock(portNum, 0); fdr = 0x92; divisor = 0x10; break;

    case 115200: LPC24_Uart_SetClock(portNum, 0); fdr = 0x92; divisor = 0x08; break;

    case 230400: LPC24_Uart_SetClock(portNum, 0); fdr = 0x92; divisor = 0x04; break;

    case 460800: LPC24_Uart_SetClock(portNum, 1); fdr = 0x92; divisor = 0x08; break;

    case 921600: LPC24_Uart_SetClock(portNum, 1); fdr = 0x92; divisor = 0x04; break;

    default:
        LPC24_Uart_SetClock(portNum, 1);
        divisor = ((LPC24XX_USART::c_ClockRate / (baudRate * 16)));
        fdr = 0x10;

    }

    // CWS: Disable interrupts
    USARTC.UART_LCR = 0; // prepare to Init UART
    USARTC.SEL2.IER.UART_IER &= ~(LPC24XX_USART::UART_IER_INTR_ALL_SET);          // Disable all UART interrupts
    /* CWS: Set baud rate to baudRate bps */
    USARTC.UART_LCR |= LPC24XX_USART::UART_LCR_DLAB;                                          // prepare to access Divisor
    USARTC.SEL1.DLL.UART_DLL = divisor & 0xFF;      //GET_LSB(divisor);                                                      // Set baudrate.
    USARTC.SEL2.DLM.UART_DLM = (divisor >> 8) & 0xFF; // GET_MSB(divisor);
    USARTC.UART_LCR &= ~LPC24XX_USART::UART_LCR_DLAB;                                              // prepare to access RBR, THR, IER
    // CWS: Set port for 8 bit, 1 stop, no parity

    USARTC.UART_FDR = fdr;

    // DataBit range 5-8
    if (5 <= dataBits && dataBits <= 8) {
        SET_BITS(USARTC.UART_LCR,
            LPC24XX_USART::UART_LCR_WLS_shift,
            LPC24XX_USART::UART_LCR_WLS_mask,
            dataBits - 5);
    }
    else {   // not supported
     // set up 8 data bits incase return value is ignored

        return TinyCLR_Result::NotSupported;
    }

    switch (stopBits) {
    case TinyCLR_Uart_StopBitCount::Two:
        USARTC.UART_LCR |= LPC24XX_USART::UART_LCR_NSB_15_STOPBITS;

        if (dataBits == 5)
            return TinyCLR_Result::NotSupported;

        break;

    case TinyCLR_Uart_StopBitCount::One:
        USARTC.UART_LCR |= LPC24XX_USART::UART_LCR_NSB_1_STOPBITS;

        break;

    case TinyCLR_Uart_StopBitCount::OnePointFive:
        USARTC.UART_LCR |= LPC24XX_USART::UART_LCR_NSB_15_STOPBITS;

        if (dataBits != 5)
            return TinyCLR_Result::NotSupported;

        break;

    default:

        return TinyCLR_Result::NotSupported;
    }

    switch (parity) {

    case TinyCLR_Uart_Parity::Space:
        USARTC.UART_LCR |= LPC24XX_USART::UART_LCR_SPE;

    case TinyCLR_Uart_Parity::Even:
        USARTC.UART_LCR |= (LPC24XX_USART::UART_LCR_EPE | LPC24XX_USART::UART_LCR_PBE);
        break;

    case TinyCLR_Uart_Parity::Mark:
        USARTC.UART_LCR |= LPC24XX_USART::UART_LCR_SPE;

    case  TinyCLR_Uart_Parity::Odd:
        USARTC.UART_LCR |= LPC24XX_USART::UART_LCR_PBE;
        break;

    case TinyCLR_Uart_Parity::None:
        USARTC.UART_LCR &= ~LPC24XX_USART::UART_LCR_PBE;
        break;

    default:

        return TinyCLR_Result::NotSupported;
    }

    if (handshaking != TinyCLR_Uart_Handshake::None && portNum != 2) // Only port 2 support handshaking
        return TinyCLR_Result::NotSupported;


    switch (handshaking) {
    case TinyCLR_Uart_Handshake::RequestToSend:
        USARTC.UART_MCR |= (1 << 6) | (1 << 7);
        g_LPC24_Uart_Controller[portNum].handshakeEnable = true;
        break;

    case TinyCLR_Uart_Handshake::XOnXOff:
    case TinyCLR_Uart_Handshake::RequestToSendXOnXOff:
        return TinyCLR_Result::NotSupported;
    }

    // CWS: Set the RX FIFO trigger level (to 8 bytes), reset RX, TX FIFO
    USARTC.SEL3.FCR.UART_FCR = (LPC24XX_USART::UART_FCR_RFITL_08 << LPC24XX_USART::UART_FCR_RFITL_shift) |
        LPC24XX_USART::UART_FCR_TFR |
        LPC24XX_USART::UART_FCR_RFR |
        LPC24XX_USART::UART_FCR_FME;


    LPC24_Interrupt_Activate(LPC24XX_USART::getIntNo(portNum), (uint32_t*)&LPC24_Uart_InterruptHandler, (void*)self->Index);
    LPC24_Interrupt_Enable(LPC24XX_USART::getIntNo(portNum));

    LPC24_Uart_PinConfiguration(portNum, true);

    g_LPC24_Uart_Controller[portNum].isOpened = true;

    return TinyCLR_Result::Success;
}

TinyCLR_Result LPC24_Uart_Release(const TinyCLR_Uart_Provider* self) {
    DISABLE_INTERRUPTS_SCOPED(irq);

    int32_t portNum = self->Index;

    LPC24XX_USART& USARTC = LPC24XX::UART(portNum);

    if (g_LPC24_Uart_Controller[portNum].isOpened == true) {


        LPC24_Interrupt_Disable(LPC24XX_USART::getIntNo(portNum));

        LPC24_Uart_PinConfiguration(portNum, false);

        if (g_LPC24_Uart_Controller[portNum].handshakeEnable) {
            USARTC.UART_MCR &= ~((1 << 6) | (1 << 7));
        }

        // CWS: Disable interrupts
        USARTC.UART_LCR = 0; // prepare to Init UART
        USARTC.SEL2.IER.UART_IER &= ~(LPC24XX_USART::UART_IER_INTR_ALL_SET);         // Disable all UART interrupt

    }

    g_LPC24_Uart_Controller[portNum].txBufferCount = 0;
    g_LPC24_Uart_Controller[portNum].txBufferIn = 0;
    g_LPC24_Uart_Controller[portNum].txBufferOut = 0;

    g_LPC24_Uart_Controller[portNum].rxBufferCount = 0;
    g_LPC24_Uart_Controller[portNum].rxBufferIn = 0;
    g_LPC24_Uart_Controller[portNum].rxBufferOut = 0;

    g_LPC24_Uart_Controller[portNum].isOpened = false;
    g_LPC24_Uart_Controller[portNum].handshakeEnable = false;

    switch (portNum) {
    case 0:
        LPC24XX::SYSCON().PCONP &= ~PCONP_PCUART0;
        break;

    case 1:
        LPC24XX::SYSCON().PCONP &= ~PCONP_PCUART1;
        break;

    case 2:
        LPC24XX::SYSCON().PCONP &= ~PCONP_PCUART2;
        break;

    case 3:
        LPC24XX::SYSCON().PCONP &= ~PCONP_PCUART3;
        break;
    }

    return TinyCLR_Result::Success;
}

void LPC24_Uart_TxBufferEmptyInterruptEnable(int portNum, bool enable) {
    DISABLE_INTERRUPTS_SCOPED(irq);

    LPC24XX_USART& USARTC = LPC24XX::UART(portNum);

    if (enable) {
        LPC24XX::VIC().ForceInterrupt(LPC24XX_USART::getIntNo(portNum));// force interrupt as this chip has a bug????
        USARTC.SEL2.IER.UART_IER |= (LPC24XX_USART::UART_IER_THREIE);
    }
    else {
        USARTC.SEL2.IER.UART_IER &= ~(LPC24XX_USART::UART_IER_THREIE);
    }
}

void LPC24_Uart_RxBufferFullInterruptEnable(int portNum, bool enable) {
    DISABLE_INTERRUPTS_SCOPED(irq);

    LPC24XX_USART& USARTC = LPC24XX::UART(portNum);

    if (enable) {
        USARTC.SEL2.IER.UART_IER |= (LPC24XX_USART::UART_IER_RDAIE);
    }
    else {
        USARTC.SEL2.IER.UART_IER &= ~(LPC24XX_USART::UART_IER_RDAIE);
    }
}

bool LPC24_Uart_TxHandshakeEnabledState(int portNum) {
    return true; // If this handshake input is not being used, it is assumed to be good
}

TinyCLR_Result LPC24_Uart_Flush(const TinyCLR_Uart_Provider* self) {
    int32_t portNum = self->Index;

    if (g_LPC24_Uart_Controller[portNum].isOpened == false)
        return TinyCLR_Result::NotAvailable;

    // Make sute interrupt is enable
    LPC24_Uart_TxBufferEmptyInterruptEnable(portNum, true);

    while (g_LPC24_Uart_Controller[portNum].txBufferCount > 0) {
        LPC24_Time_Delay(nullptr, 1);
    }

    return TinyCLR_Result::Success;
}

TinyCLR_Result LPC24_Uart_Read(const TinyCLR_Uart_Provider* self, uint8_t* buffer, size_t& length) {
    int32_t portNum = self->Index;
    size_t i = 0;;

    DISABLE_INTERRUPTS_SCOPED(irq);

    if (g_LPC24_Uart_Controller[portNum].isOpened == false)
        return TinyCLR_Result::NotAvailable;

    length = std::min(g_LPC24_Uart_Controller[portNum].rxBufferCount, length);

    while (i < length) {
        buffer[i] = g_LPC24_Uart_Controller[portNum].RxBuffer[g_LPC24_Uart_Controller[portNum].rxBufferOut];

        g_LPC24_Uart_Controller[portNum].rxBufferOut++;
        i++;
        g_LPC24_Uart_Controller[portNum].rxBufferCount--;

        if (g_LPC24_Uart_Controller[portNum].rxBufferOut == LPC24_UART_RX_BUFFER_SIZE)
            g_LPC24_Uart_Controller[portNum].rxBufferOut = 0;
    }

    return TinyCLR_Result::Success;
}

TinyCLR_Result LPC24_Uart_Write(const TinyCLR_Uart_Provider* self, const uint8_t* buffer, size_t& length) {
    int32_t portNum = self->Index;
    int32_t i = 0;

    DISABLE_INTERRUPTS_SCOPED(irq);

    if (g_LPC24_Uart_Controller[portNum].isOpened == false)
        return TinyCLR_Result::NotAvailable;

    if (g_LPC24_Uart_Controller[portNum].txBufferCount == LPC24_UART_TX_BUFFER_SIZE) {
        LPC24_Uart_SetErrorEvent(portNum, TinyCLR_Uart_Error::TransmitFull);

        return TinyCLR_Result::Busy;
    }

    length = std::min(LPC24_UART_TX_BUFFER_SIZE - g_LPC24_Uart_Controller[portNum].txBufferCount, length);


    while (i < length) {

        g_LPC24_Uart_Controller[portNum].TxBuffer[g_LPC24_Uart_Controller[portNum].txBufferIn] = buffer[i];

        g_LPC24_Uart_Controller[portNum].txBufferCount++;

        i++;

        g_LPC24_Uart_Controller[portNum].txBufferIn++;

        if (g_LPC24_Uart_Controller[portNum].txBufferIn == LPC24_UART_TX_BUFFER_SIZE)
            g_LPC24_Uart_Controller[portNum].txBufferIn = 0;
    }

    if (length > 0) {
        LPC24_Uart_TxBufferEmptyInterruptEnable(portNum, true); // Enable Tx to start transfer
    }

    return TinyCLR_Result::Success;
}

TinyCLR_Result LPC24_Uart_SetPinChangedHandler(const TinyCLR_Uart_Provider* self, TinyCLR_Uart_PinChangedHandler handler) {
    //TODO
    return TinyCLR_Result::Success;
}
TinyCLR_Result LPC24_Uart_SetErrorReceivedHandler(const TinyCLR_Uart_Provider* self, TinyCLR_Uart_ErrorReceivedHandler handler) {
    int32_t portNum = self->Index;

    g_LPC24_Uart_Controller[portNum].errorEventHandler = handler;

    return TinyCLR_Result::Success;
}

TinyCLR_Result LPC24_Uart_SetDataReceivedHandler(const TinyCLR_Uart_Provider* self, TinyCLR_Uart_DataReceivedHandler handler) {
    int32_t portNum = self->Index;

    g_LPC24_Uart_Controller[portNum].dataReceivedEventHandler = handler;

    return TinyCLR_Result::Success;
}

TinyCLR_Result LPC24_Uart_GetBreakSignalState(const TinyCLR_Uart_Provider* self, bool& state) {
    return TinyCLR_Result::NotImplemented;
}

TinyCLR_Result LPC24_Uart_SetBreakSignalState(const TinyCLR_Uart_Provider* self, bool state) {
    return TinyCLR_Result::NotImplemented;
}

TinyCLR_Result LPC24_Uart_GetCarrierDetectState(const TinyCLR_Uart_Provider* self, bool& state) {
    return TinyCLR_Result::NotImplemented;
}

TinyCLR_Result LPC24_Uart_GetClearToSendState(const TinyCLR_Uart_Provider* self, bool& state) {
    return TinyCLR_Result::NotImplemented;
}

TinyCLR_Result LPC24_Uart_GetDataReadyState(const TinyCLR_Uart_Provider* self, bool& state) {
    return TinyCLR_Result::NotImplemented;
}

TinyCLR_Result LPC24_Uart_GetIsDataTerminalReadyEnabled(const TinyCLR_Uart_Provider* self, bool& state) {
    return TinyCLR_Result::NotImplemented;
}

TinyCLR_Result LPC24_Uart_SetIsDataTerminalReadyEnabled(const TinyCLR_Uart_Provider* self, bool state) {
    return TinyCLR_Result::NotImplemented;
}

TinyCLR_Result LPC24_Uart_GetIsRequestToSendEnabled(const TinyCLR_Uart_Provider* self, bool& state) {
    return TinyCLR_Result::NotImplemented;
}

TinyCLR_Result LPC24_Uart_SetIsRequestToSendEnabled(const TinyCLR_Uart_Provider* self, bool state) {
    return TinyCLR_Result::NotImplemented;
}

void LPC24_Uart_Reset() {
    for (auto i = 0; i < TOTAL_UART_CONTROLLERS; i++) {
        LPC24_Uart_Release(uartProviders[i]);
    }
}

