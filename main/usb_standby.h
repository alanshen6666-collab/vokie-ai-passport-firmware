#pragma once

// Keep the USB Serial/JTAG console usable while a host keeps the bus active,
// without blocking battery light sleep once the cable is unplugged. Calling
// twice is harmless; the module is a no-op when power management is disabled.
void usb_standby_init(void);
