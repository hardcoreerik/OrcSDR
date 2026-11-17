#include <SDL2/SDL.h>
#include <alsa/asoundlib.h>
#include <libevdev/libevdev.h>
#include <xf86drm.h>
#include <rtl-sdr.h>
#include <fftw3.h>
#include <stdio.h>
int main(void) {
  printf("SDL=%d.%d.%d; ALSA=%s; rtlsdr devices=%u (no hardware is expected in CI)\n",
         SDL_MAJOR_VERSION,SDL_MINOR_VERSION,SDL_PATCHLEVEL,
         SND_LIB_VERSION_STR,rtlsdr_get_device_count());
  return 0;
}
