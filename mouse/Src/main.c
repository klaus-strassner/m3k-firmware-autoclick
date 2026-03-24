#include <assert.h>
#include <m3k_resource.h>
#include <paw3399.h>
#include <stdint.h>
#include "stm32f7xx.h"
#include "usbd_hid.h"
#include "usb.h"
#include "anim.h"
#include "btn_whl.h"
#include "clock.h"
#include "config.h"
#include "delay.h"

#define TIMEOUT_SECS 5 // seconds of holding buttons for programming mode

typedef union {
    struct __PACKED { // use the order in the report descriptor
        uint8_t btn;
        int8_t whl;
        int16_t x, y;
        uint16_t _pad; // zero pad to 8 bytes total
    };
    uint8_t u8[8]; // btn, wheel, xlo, xhi, ylo, yhi, 0, 0
    uint32_t u32[2];
} Usb_packet;
static_assert(sizeof(Usb_packet) == 2*sizeof(uint32_t), "Usb_packet wrong size");

int main(void) {
    extern uint32_t _sflash;
    SCB->VTOR = (uint32_t) (&_sflash);
    SCB_EnableICache();
    SCB_EnableDCache();

    clk_init();
    delay_init();
    btn_whl_init();
    usb_init(0);
    usb_wait_configured();
    spi_init();
    paw3399_init();

    const uint32_t USBx_BASE = (uint32_t) USB_OTG_HS; // used in macros USBx_*
    // fifo space when empty, should equal 0x174, from init_usb
    const uint32_t fifo_space = (USBx_INEP(1)->DTXFSTS
            & USB_OTG_DTXFSTS_INEPTFSAV);

    Usb_packet new = { 0 }; // what's new this loop
    Usb_packet send = { 0 }; // what's transmitted
    uint8_t btn_prev = 0;

    // --- LATENCY MACRO CONFIGURATION ---
    // Assuming 1000Hz polling rate for NVIDIA RLA monitor port.
    #define LOOPS_PER_MS 1
    #define CLICK_INTERVAL_LOOPS (500 * LOOPS_PER_MS) // 500 loops = 0.5s
    #define BUTTON_HOLD_LOOPS    (20 * LOOPS_PER_MS)  // 20 loops = 20ms
    #define MAX_CLICKS           120                  // 120 clicks = 60s

    #define MASK_LMB 0x01
    #define MASK_RMB 0x02
    #define MASK_MMB 0x04

    static uint8_t  macro_state  = 0;
    static uint32_t loop_counter = 0;
    static uint16_t click_count  = 0;
    // -----------------------------------

    USB_OTG_HS->GINTMSK |= USB_OTG_GINTMSK_SOFM;
    while (1) {
        usb_wait_configured();

        USB_OTG_HS->GINTSTS |= USB_OTG_GINTSTS_SOF;
        
        // Wait For Interrupt (Synchronizes to the USB Start of Frame)
        __WFI();

        const uint16_t btn_raw = btn_read();
        const uint8_t btn_NO = (btn_raw & 0xFF);
        const uint8_t btn_NC = (btn_raw >> 8);
        btn_prev = new.btn;
        
        // Physical button state is resolved here
        new.btn = (~btn_NO & 0b111) | (btn_NC & btn_prev);

        // --- INJECT LATENCY MACRO LOGIC ---
        // 1. Detect physical MMB press to start the sequence
        if ((new.btn & MASK_MMB) && macro_state == 0) {
            macro_state = 1;
            loop_counter = 0;
            click_count = 0;
        }

        // 2. The Loop-Counting State Machine
        if (macro_state == 1) {
            // PHASE 1: Send initial MMB to start recording
            loop_counter++;
            if (loop_counter < BUTTON_HOLD_LOOPS) {
                new.btn |= MASK_MMB; // Force MMB active
            } else {
                new.btn &= ~MASK_MMB; // Release
                macro_state = 2;
                loop_counter = 0;
            }
        }
        else if (macro_state == 2) {
            // PHASE 2: Send LMB every 500ms
            loop_counter++;
            if (click_count < MAX_CLICKS) {
                if (loop_counter < BUTTON_HOLD_LOOPS) {
                    new.btn |= MASK_LMB; // Force LMB active briefly
                } else if (loop_counter >= CLICK_INTERVAL_LOOPS) {
                    loop_counter = 0; // Reset counter for next click
                    click_count++;
                } else {
                    new.btn &= ~MASK_LMB; // Ensure LMB stays released between clicks
                }
            } else {
                macro_state = 3;
                loop_counter = 0;
            }
        }
        else if (macro_state == 3) {
            // PHASE 3: Send final MMB to stop recording
            loop_counter++;
            if (loop_counter < BUTTON_HOLD_LOOPS) {
                new.btn |= MASK_MMB; // Force MMB active
            } else {
                new.btn &= ~MASK_MMB; // Release
                macro_state = 0;      // Return to Idle
            }
        }
        // --- END MACRO LOGIC ---

        // The firmware will now send the overridden 'new.btn' to the PC
        if (new.btn != send.btn) {
            send.btn = new.btn;

            USBx_DEVICE->DIEPMSK &= ~USB_OTG_DIEPMSK_XFRCM;

            MODIFY_REG(USBx_INEP(1)->DIEPTSIZ,
                    USB_OTG_DIEPTSIZ_PKTCNT | USB_OTG_DIEPTSIZ_XFRSIZ,
                    _VAL2FLD(USB_OTG_DIEPTSIZ_PKTCNT, 1) | _VAL2FLD(USB_OTG_DIEPTSIZ_XFRSIZ, HID_EPIN_SIZE));

            USBx_INEP(1)->DIEPCTL |= USB_OTG_DIEPCTL_CNAK
                    | USB_OTG_DIEPCTL_EPENA;

            USBx_DFIFO(1) = send.u32[0] & mask;
            USBx_DFIFO(1) = send.u32[1];
        }
    }
    return 0;
}
