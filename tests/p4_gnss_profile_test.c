#include "board_list.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const orcsdr_board_profile_t* b=&g_board_lilygo_t_display_p4_amoled;
    /* Trace vendor HardwareUart(tx,rx) call, not module-label direction. */
    assert(b->has_gnss && b->gnss_uart==1 && b->gnss_rx==22 && b->gnss_tx==23);
    assert(b->gnss_baud==115200 && b->gnss_startup_line.dev==0 &&
           b->gnss_startup_line.pin==11 && b->gnss_startup_level);
    const orcsdr_board_profile_t* absent[]={&g_board_m5stack_tab5,
        &g_board_waveshare_p4_module_dev_kit,&g_board_waveshare_p4_wifi6_db,
        &g_board_waveshare_p4_wifi6_poe_eth,&g_board_waveshare_p4_eth};
    for(unsigned i=0;i<sizeof(absent)/sizeof(absent[0]);++i)assert(!absent[i]->has_gnss);
    puts("P4_GNSS_PROFILE_TEST pass");
}
