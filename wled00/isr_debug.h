// Event tracking
#include <cstdint>

void clear_events();
void print_events();
void setup_isr_tracking();
void track_event(uint32_t lvl, uint32_t data);
