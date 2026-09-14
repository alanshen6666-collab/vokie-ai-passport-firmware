#include <assert.h>
#include <limits.h>
#include <string.h>

#include "ui_battery.h"

int main(void)
{
    // Invalid/not-ready values are unknown, never low or a clamped full charge.
    const int invalid[] = {-1, INT_MIN, 101, 255, INT_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        const ui_battery_view_t view = ui_battery_view(invalid[i], -1);
        assert(strcmp(view.text, "--%") == 0);
        assert(view.bars == 0 && !view.low);
    }

    // Check every valid SOC: bounded, monotonic fill and the low-charge edge.
    unsigned previous_bars = 0;
    for (int soc = 0; soc <= 100; ++soc) {
        const ui_battery_view_t view = ui_battery_view(soc, -1);
        assert(view.bars >= previous_bars && view.bars <= 4);
        assert(view.low == (soc <= 20));
        assert(strlen(view.text) >= 2 && strlen(view.text) <= 4);
        previous_bars = view.bars;
    }
    assert(strcmp(ui_battery_view(0, -1).text, "0%") == 0);
    assert(strcmp(ui_battery_view(9, -1).text, "9%") == 0);
    assert(strcmp(ui_battery_view(20, -1).text, "20%") == 0);
    assert(strcmp(ui_battery_view(100, -1).text, "100%") == 0);
    assert(ui_battery_view(0, -1).bars == 0);
    assert(ui_battery_view(1, -1).bars == 1);
    assert(ui_battery_view(25, -1).bars == 1);
    assert(ui_battery_view(26, -1).bars == 2);
    assert(ui_battery_view(50, -1).bars == 2);
    assert(ui_battery_view(51, -1).bars == 3);
    assert(ui_battery_view(75, -1).bars == 3);
    assert(ui_battery_view(76, -1).bars == 4);

    // A read failure clears the value, and a later good read restores it.
    ui_battery_view_t view = ui_battery_view(10, -1);
    assert(view.low);
    view = ui_battery_view(-1, -1);
    assert(!view.low && strcmp(view.text, "--%") == 0);
    view = ui_battery_view(80, -1);
    assert(!view.low && strcmp(view.text, "80%") == 0);
    // Invalid SOC still conveys the measured voltage, without invented fill
    // or a false low-battery alert; valid SOC always has priority.
    view = ui_battery_view(-1, 3941);
    assert(strcmp(view.text, "3.94V") == 0 && view.bars == 0 && !view.low);
    view = ui_battery_view(254, 3919);
    assert(strcmp(view.text, "3.91V") == 0);
    assert(strcmp(ui_battery_view(255, 4200).text, "4.20V") == 0);
    assert(strcmp(ui_battery_view(80, 3941).text, "80%") == 0);
    assert(strcmp(ui_battery_view(0, 3941).text, "0%") == 0);
    assert(strcmp(ui_battery_view(-1, 0).text, "--%") == 0);
    assert(strcmp(ui_battery_view(-1, INT_MAX).text, "--%") == 0);
    return 0;
}
