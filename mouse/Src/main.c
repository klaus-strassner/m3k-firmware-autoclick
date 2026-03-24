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

typedef union {
    struct __PACKED {
        uint8_t btn;
        int8_t whl;
        int16_t x, y;
        uint16_t _pad;
    };
    uint8_t u8[8];
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

    const uint32_t USBx_BASE = (uint32_t) USB_OTG_HS;

    Usb_packet send = { 0 };
    uint8_t btn_prev_final = 0;

    // Timing Constants (Assuming 1ms SOF intervals)
    #define CLICK_INTERVAL_MS 500
    #define BUTTON_HOLD_MS    20
    #define MAX_CLICKS        120

    #define MASK_LMB 0x01
    #define MASK_MMB 0x04

    static uint8_t  macro_state  = 0;
    static uint32_t timer        = 0;
    static uint16_t click_count  = 0;

    USB_OTG_HS->GINTMSK |= USB_OTG_GINTMSK_SOFM;

    while (1) {
        usb_wait_configured();
        USB_OTG_HS->GINTSTS |= USB_OTG_GINTSTS_SOF;
        __WFI(); // Wait for Start of Frame (1ms)

        // 1. Read Physical Buttons
        const uint16_t btn_raw = btn_read();
        const uint8_t btn_NO = (btn_raw & 0xFF);
        const uint8_t btn_NC = (btn_raw >> 8);
        
        // Logical "Debounce" / Physical state
        uint8_t physical_btns = (~btn_NO & 0b111) | (btn_NC & (send.btn & 0b111));
        uint8_t macro_btns = 0;

        // 2. Macro State Machine
        // Trigger macro on MMB Press
        if ((physical_btns & MASK_MMB) && macro_state == 0) {
            macro_state = 1;
            timer = 0;
            click_count = 0;
        }

        switch (macro_state) {
            case 1: // Phase 1: Initial MMB Hold
                timer++;
                macro_btns |= MASK_MMB;
                if (timer >= BUTTON_HOLD_MS) {
                    macro_state = 2;
                    timer = 0;
                }
                break;

            case 2: // Phase 2: Intermittent LMB Clicks
                timer++;
                if (click_count < MAX_CLICKS) {
                    if (timer < BUTTON_HOLD_MS) {
                        macro_btns |= MASK_LMB; // Click DOWN
                    } else if (timer >= CLICK_INTERVAL_MS) {
                        timer = 0; // Reset for next click
                        click_count++;
                    }
                } else {
                    macro_state = 3;
                    timer = 0;
                }
                break;

            case 3: // Phase 3: Final MMB Release
                timer++;
                macro_btns |= MASK_MMB;
                if (timer >= BUTTON_HOLD_MS) {
                    macro_state = 0;
                    timer = 0;
                }
                break;
        }

        // 3. Combine Physical and Macro (OR logic)
        uint8_t final_btns = physical_btns | macro_btns;

        // 4. Send ONLY if state changed
        if (final_btns != btn_prev_final) {
            send.btn = final_btns;
            btn_prev_final = final_btns;

            // USB Transmission Logic
            USBx_DEVICE->DIEPMSK &= ~USB_OTG_DIEPMSK_XFRCM;
            MODIFY_REG(USBx_INEP(1)->DIEPTSIZ,
                    USB_OTG_DIEPTSIZ_PKTCNT | USB_OTG_DIEPTSIZ_XFRSIZ,
                    _VAL2FLD(USB_OTG_DIEPTSIZ_PKTCNT, 1) | _VAL2FLD(USB_OTG_DIEPTSIZ_XFRSIZ, HID_EPIN_SIZE));

            USBx_INEP(1)->DIEPCTL |= USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA;

            USBx_DFIFO(1) = send.u32[0];
            USBx_DFIFO(1) = send.u32[1];
        }
    }
}
