/**
 * Non-Volatile Memory Controller - Read While Write MCC Implementation
 *
 * @file main.c
 * Author: Microchip Technology Inc.
 *
 * @ingroup main 
 *
 * @brief This is an application implementing the Read While Write functionality of the 
 * AVR EA using MPLAB Code Configurator.
 *
 * @version MAIN file Version 1.0.0
 */
/*
*  © 2023 Microchip Technology Inc. and its subsidiaries.
*
*  Subject to your compliance with these terms, you may use this Microchip software and any
*  derivatives exclusively with Microchip products. You are responsible for complying with
*  third party license terms applicable to your use of third party software (including open
*  source software) that may accompany this Microchip software.
* 
*  SOFTWARE IS “AS IS.” NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO
*  THIS SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
*  OR FITNESS FOR A PARTICULAR PURPOSE.
*  
*  IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL
*  OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
*  SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE
*  DAMAGES ARE FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP’S TOTAL
*  LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT EXCEED AMOUNT OF FEES, IF ANY,
*  YOU PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*
*  MICROCHIP OFFERS NO SUPPORT FOR THE SOFTWARE. YOU MAY CONTACT MICROCHIP AT
*  https://www.microchip.com/en-us/support-and-training/design-help/client-support-services
*  TO INQUIRE ABOUT SUPPORT SERVICES AND APPLICABLE FEES, IF AVAILABLE.
*/

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/system/clock.h"
#include <stdbool.h>
#include <util/delay.h>

/* 
 * The following define uses addresses defined in the linker section.
 * .rww_data, .nrww_data and .nrww_program
 * See in the Readme under Project Properties for more information.
 */
// The .nrww_data section is configured to be at address 0x1F00 (word address 0x0F80)
#define NRWW_DATA_SECTION __attribute__((used, section(".nrww_data")))

#define RWW_DATA_SIZE (2 * PROGMEM_PAGE_SIZE)
#define NRWW_DATA_SIZE (2 * PROGMEM_PAGE_SIZE)
#define BUFFER_SIZE 256

#define DEBOUNCE_TIME 20 // Debounce time in ms

// RWW variables
const RWW_DATA_SECTION uint8_t rwwArray[RWW_DATA_SIZE] = {0};

// NRWW variables
const NRWW_DATA_SECTION uint8_t nrwwArray[NRWW_DATA_SIZE] = {0};

// Buffer variables
uint8_t buffer[BUFFER_SIZE] = {0};
uint8_t pageBuffer[PROGMEM_PAGE_SIZE] = {0};
volatile uint8_t writeIndex = 0;
volatile uint8_t readIndex = 0;
static volatile uint32_t data = 0;

volatile uint16_t rwwLastDataAddress = 0;
volatile uint16_t nrwwLastDataAddress = 0;

typedef enum BUTTON_enum
{
    PRESSED,
    RELEASED
} BUTTON_t;

static uint8_t buttonDebounceCounter = 0; // Counter variable to track debounce for on-board button (SW0)
BUTTON_t buttonState = RELEASED; // Cleaned-up version of the SW0 button input signal

typedef enum DATA_FLASH_enum
{
    IDLE = 0,
    INIT_NRWW,
    ERASE_NRWW,
    WRITE_NRWW,
    INIT_RWW,
    ERASE_RWW,
    WRITE_RWW,
} DATA_FLASH_t;

// Function prototypes
void FillBuffer(void);
void DebounceSW0(void);


int main(void)
{
    // Pointer to rww flash program memory mapped in data space
    uint16_t *rwwFlashPointer;
    // Pointer to nrww flash program memory mapped in data space
    uint16_t *nrwwFlashPointer;
    
    static BUTTON_t previousButtonState = RELEASED;
    
    DATA_FLASH_t eraseWriteState = IDLE;
    
    TCB0_CaptureCallbackRegister(FillBuffer);
    TCB1_CaptureCallbackRegister(DebounceSW0);
    SYSTEM_Initialize();
    
    data = ((FLASH_Read(((flash_address_t)(& rwwArray[255]) & 0x7FFF))) << 2);
    
    while (1)
    {
        switch (eraseWriteState)
            {
            case IDLE:
                if (PRESSED == buttonState)
                {
                    LED0_SetLow();
                }
                else if (RELEASED == buttonState)
                {
                    LED0_SetHigh();
                }
                
                if (previousButtonState != buttonState)
                {
                    if (PRESSED == buttonState)
                    {
                        // Sets the system state to INIT_NRWW to start a new Erase/Write cycle
                        eraseWriteState = INIT_NRWW;
                    }
                    
                    previousButtonState = buttonState;
                }
                break;
            case INIT_NRWW:
                // Sets nrwwFlashPointer address to NRWW data space
                nrwwFlashPointer = (uint16_t *)((uint16_t)(&nrwwArray[0])& 0x7FFF);
                
                TCB0_ISR_SetLow();
                FILL_BUFFER_SetLow();
                FLPER_SetLow();
                FLPW_SetLow();
                
                // Starts TCB0 timer that will be filling up the data buffer
                TCB0_Start();
                
                // Sets the system state to ERASE_NRWW to start the Erase cycle
                eraseWriteState = ERASE_NRWW;
                
                break;
            case ERASE_NRWW:
                nrwwLastDataAddress = ((((uint16_t) & nrwwArray[0])& 0x7FFF) + NRWW_DATA_SIZE);
                
                // Prepares the NRWW Data area by erasing any previously stored data
                while ((flash_address_t)nrwwFlashPointer < nrwwLastDataAddress)
                {
                    FLPER_SetHigh();
                    FLASH_PageErase((flash_address_t)nrwwFlashPointer);
                    while (FLASH_IsBusy())
                    {
                        ; // Waits for flash erase to complete
                    }
                    FLPER_SetLow();
                    
                    nrwwFlashPointer = nrwwFlashPointer + (PROGMEM_PAGE_SIZE / sizeof (nrwwFlashPointer));
                }
                
                // Sets nrwwFlashPointer address to NRWW data space
                nrwwFlashPointer = (uint16_t *)(((uint16_t)(&nrwwArray) & 0x7FFF));
                
                // Sets the system state to WRITE_NRWW to start the write to the RWW data space
                eraseWriteState = WRITE_NRWW;
                break;
            case WRITE_NRWW:
                if ((flash_address_t)nrwwFlashPointer < nrwwLastDataAddress)
                {
                    // Checks if we have a full page to fill
                    if ((writeIndex - readIndex) >= PROGMEM_PAGE_SIZE)
                    {
                        // Fill "page" buffer
                        FILL_BUFFER_SetHigh();
                        for (uint8_t i = 0; i < PROGMEM_PAGE_SIZE; i++)
                        {
                            pageBuffer[i] = buffer[readIndex++];
                        }
                        FILL_BUFFER_SetLow();
                        
                        FLPW_SetHigh();
                        // Writes the flash page
                        FLASH_RowWrite((flash_address_t)nrwwFlashPointer, pageBuffer);
                        while (FLASH_IsBusy())
                        {
                            ; // Waits for flash page write operation to complete
                        }
                        FLPW_SetLow();
                        
                        nrwwFlashPointer = nrwwFlashPointer + (PROGMEM_PAGE_SIZE / sizeof (nrwwFlashPointer));
                    }
                }
                else
                {
                    // Sets the system state to INIT_RWW to start a new Erase/Write cycle
                    eraseWriteState = INIT_RWW;
                    
                    TCB0_Stop();
                    
                    // Resets buffer
                    writeIndex = readIndex;
                }
                break;
            case INIT_RWW:
                // Sets rwwFlashPointer address to RWW data space
                rwwFlashPointer = (uint16_t *)((uint16_t)(&rwwArray[0])& 0x7FFF);
                
                // Start TCB0 timer that will be filling up the data buffer
                TCB0_Start();
                
                // Sets the system state to ERASE_RWW to start Erase cycle
                eraseWriteState = ERASE_RWW;
                break;
            case ERASE_RWW:
                rwwLastDataAddress = ((((uint16_t) & rwwArray[0])& 0x7FFF) + RWW_DATA_SIZE);
                // Prepare the RWW Data area by erasing any previously stored data
                while ((flash_address_t)rwwFlashPointer < rwwLastDataAddress)
                {
                    FLPER_SetHigh();
                    FLASH_PageErase((flash_address_t)rwwFlashPointer);
                    while (FLASH_IsBusy())
                    {
                        ; // Waits for flash erase to complete
                    }
                    FLPER_SetLow();
                    rwwFlashPointer = rwwFlashPointer + (PROGMEM_PAGE_SIZE / sizeof (rwwFlashPointer));
                }
                
                // Sets rwwFlashPointer address to RWW data space
                rwwFlashPointer = (uint16_t *)(((uint16_t)(&rwwArray) & 0x7FFF));
                
                // Sets the system state to WRITE_RWW to start Flash Write to the RWW data space
                eraseWriteState = WRITE_RWW;
                
                break;
            case WRITE_RWW:
                if ((flash_address_t)rwwFlashPointer < rwwLastDataAddress)
                {
                    // Checks if we have a full page to fill
                    if ((writeIndex - readIndex) >= PROGMEM_PAGE_SIZE)
                    {
                        // Fill "page" buffer
                        FILL_BUFFER_SetHigh();
                        for (uint8_t i = 0; i < PROGMEM_PAGE_SIZE; i++)
                        {
                            pageBuffer[i] = buffer[readIndex++];
                        }
                        FILL_BUFFER_SetLow();
                        
                        // Writes the flash page
                        FLASH_RowWrite((flash_address_t)rwwFlashPointer, pageBuffer);
                        FLPW_SetHigh();
                        while (FLASH_IsBusy())
                            ; // Waits for flash page write operation to complete
                        FLPW_SetLow();
                        
                        rwwFlashPointer = rwwFlashPointer + (PROGMEM_PAGE_SIZE / sizeof (rwwFlashPointer));
                    }
                }
                else
                {
                    // Sets the system state to IDLE to start the process over again
                    eraseWriteState = IDLE;
                    TCB0_Stop();
                    
                    // Resets buffer
                    writeIndex = readIndex;
                }
                break;
            default:
                break;
        }
    }
}


void FillBuffer(void)
{
    // Fills the buffer with some data
    TCB0_ISR_Toggle();
    buffer[writeIndex++] = (data >> 2);
    data++;
}


void DebounceSW0(void)
{
    // Gets current button state
    bool buttonIsPressed = !SW0_GetValue();
    
    if(0 == buttonDebounceCounter) // Debounce is done
    {
        if(buttonIsPressed) // New SW0 pressed
        {
            // Sets button state to pressed
            buttonState = PRESSED;
            // Sets counter to debounce time
            buttonDebounceCounter = DEBOUNCE_TIME;
        }
        else{
            // Sets button state to released
                buttonState = RELEASED;
        }
    }
    else // Debounce counter still counting down
    {
        if (!buttonIsPressed) // Button is released
        {
            // Decrements debounce counter and checks if it has reached 0
            --buttonDebounceCounter;
            if (0 == buttonDebounceCounter)
            {
                // Sets button state to released
                buttonState = RELEASED;
            }
        }
        else // Button held or repressed
        {
            // Sets counter back to debounce time
            buttonDebounceCounter = DEBOUNCE_TIME;
        }
    }
}