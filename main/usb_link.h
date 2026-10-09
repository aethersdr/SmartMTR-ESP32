// USB side of AetherKnob: one vendor HID interface carrying the CTR2 USB link
// v0 (AetherSDR docs/ctr2-usb-relay-design.md), and the radio task that owns
// it. AetherSDR's CTR2 relay pipes the link to the radio, so the SmartSDR
// client in flex_api.c talks to the radio exactly as it would over TCP.
#pragma once

#include "flex_api.h"

// Installs TinyUSB and starts the radio task.
void usb_link_start(void);

// Queues a control action for the radio task. Safe from any task; drops the
// action if the queue is full (the knob is turning faster than USB drains).
void usb_link_post_action(const knob_action_t *action);
