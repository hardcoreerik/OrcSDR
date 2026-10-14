/* A/B oracle for OrcSDR RF Analysis linear-power cache. Build with:
 * cc -O2 -std=c11 -fno-fast-math -ffp-contract=off -Wall -Wextra -Werror
 *    rf_power_cache_regression.c -lm -o rf_power_test && ./rf_power_test
 * Tested math intentionally mirrors the observable old and proposed new paths.
 * No ESP-IDF or RF hardware; it is a CPU arithmetic contract, not performance evidence.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t state = 0x90210u;
static float rng(void) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return (float)(state >> 8) * (1.0f / 16777216.0f);
}
struct Result { float channel_dbfs; uint32_t bandwidth_hz; };
static struct Result original(const float *db, int size, int first, int last, float hz) {
  float p = 0;
  for (int i = first; i < last; ++i) p += powf(10.0f, db[i] / 10.0f) / 1.5f;
  struct Result r = {0};
  r.channel_dbfs = 10.0f * log10f(p + 1.0e-14f);
  if (last <= first || last > size) return r;
  float total = 0;
  for (int i = first; i < last; ++i) total += powf(10.0f, db[i] / 10.0f);
  if (total <= 0) return r;
  const float lo = total * 0.005f, hi = total * 0.995f;
  float sum = 0;
  int low = first, high = last - 1, found = 0;
  for (int i = first; i < last; ++i) {
    sum += powf(10.0f, db[i] / 10.0f);
    if (!found && sum >= lo) { low = i; found = 1; }
    if (sum >= hi) { high = i; break; }
  }
  r.bandwidth_hz = (uint32_t)lroundf((high - low + 1) * hz);
  return r;
}
static struct Result cached(const float *db, int size, int first, int last, float hz) {
  float linear[8192];
  for (int i = 0; i < size; ++i) linear[i] = powf(10.0f, db[i] / 10.0f);
  float p = 0;
  for (int i = first; i < last; ++i) p += linear[i] / 1.5f;
  struct Result r = {0};
  r.channel_dbfs = 10.0f * log10f(p + 1.0e-14f);
  if (last <= first || last > size) return r;
  float total = 0;
  for (int i = first; i < last; ++i) total += linear[i];
  if (total <= 0) return r;
  const float lo = total * 0.005f, hi = total * 0.995f;
  float sum = 0;
  int low = first, high = last - 1, found = 0;
  for (int i = first; i < last; ++i) {
    sum += linear[i];
    if (!found && sum >= lo) { low = i; found = 1; }
    if (sum >= hi) { high = i; break; }
  }
  r.bandwidth_hz = (uint32_t)lroundf((high - low + 1) * hz);
  return r;
}
int main(void) {
  float db[8192];
  const int sizes[] = {256,512,1024,2048,4096,8192};
  int tests = 0, failed = 0;
  for (unsigned n = 0; n < sizeof(sizes)/sizeof(sizes[0]); ++n) {
    const int size = sizes[n];
    for (int trial = 0; trial < 60; ++trial) {
      for (int i = 0; i < size; ++i) {
        db[i] = trial%4==0 ? -100.0f : -150.0f + 140.0f*rng();
      }
      if (trial%4==2) db[(trial * 97)%size] = 0.0f;
      const int first = trial%5==0 ? 0 : trial%5==1 ? size/4 : size/2;
      const int last = trial%5==0 ? size : trial%5==1 ? size*3/4 : size;
      const float hz = trial%3 == 0 ? 1.0f : trial%3 == 1 ? 937.5f : 122.0703125f;
      const struct Result a = original(db,size,first,last,hz);
      const struct Result b = cached(db,size,first,last,hz);
      if (memcmp(&a.channel_dbfs,&b.channel_dbfs,sizeof(float)) ||
          a.bandwidth_hz != b.bandwidth_hz) {
        fprintf(stderr,"FAIL size=%d trial=%d old_db=%g new_db=%g old_bw=%u new_bw=%u\n",
                size,trial,a.channel_dbfs,b.channel_dbfs,a.bandwidth_hz,b.bandwidth_hz);
        ++failed;
      }
      ++tests;
    }
  }
  printf("ORC_RF_CACHE_CHECK cases=%d failed=%d (exact float/channel power and 99%% OBW)\n",tests,failed);
  return failed ? 1 : 0;
}
