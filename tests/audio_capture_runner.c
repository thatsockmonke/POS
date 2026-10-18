/* Run the real device capture code against a timed I2S producer and bounded
 * pthread-backed FreeRTOS primitives. A millisecond models a 256-frame block;
 * the simulated UI/SD stall exceeds all six hardware DMA blocks. */
#include "audio_test_port.h"
#include "pos_peripherals.h"
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct test_stream {
  pthread_mutex_t lock;
  unsigned char *bytes;
  size_t capacity, count, read, write;
};
struct test_sem {
  pthread_mutex_t lock;
  pthread_cond_t ready;
  bool signalled;
};
static pthread_t foreground;
static bool fail_alloc, fail_task, enabled, receiving;
static bool fail_config, fail_readback;
static uint8_t codec[256];
enum { RIGHT_SIGNAL, LEFT_SIGNAL, SILENT, EXTREMES };
static unsigned signal_mode, reference_gain = 8;
static int16_t raw_sample(unsigned index) {
  static const int16_t extremes[] = {0,     1,     -1,    4095,  4096,
                                     -4096, -4097, 32767, -32768};
  return signal_mode == EXTREMES ? extremes[index % 9]
                                 : (int)(index % 257) - 128;
}
static int16_t expected_sample(unsigned index) {
  int raw = raw_sample(index);
  int amplified = raw * (int)reference_gain;
  if (amplified > 32767)
    return 32767;
  if (amplified < -32768)
    return -32768;
  return (int16_t)amplified;
}
static atomic_uint waveform = 2166136261u;
static atomic_uint blocks;
static atomic_bool fail_read;
static atomic_uint allocated, channels, output_writes, output_peak,
    active_tasks;
static uint32_t sample;
static i2s_event_callbacks_t callbacks;
static void delay(void) {
  struct timespec t = {0, 1000000};
  nanosleep(&t, NULL);
}
static void wait_blocks(unsigned n) {
  for (unsigned i = 0; i < 2000 && atomic_load(&blocks) < n; ++i)
    delay();
  assert(atomic_load(&blocks) >= n);
}
int pos_device_i2c_write(uint8_t a, uint8_t r, uint8_t v) {
  assert(a == 0x18);
  codec[r] = v;
  return 0;
}
int pos_device_i2c_read(uint8_t a, uint8_t r, uint8_t *v) {
  assert(a == 0x18);
  *v = codec[r] ^ (fail_readback && r == 0x14 ? 1 : 0);
  return 0;
}
int i2s_new_channel(const i2s_chan_config_t *c, i2s_chan_handle_t *tx,
                    i2s_chan_handle_t *rx) {
  (void)c;
  assert(!channels);
  ++channels;
  receiving = rx != NULL;
  if (tx)
    *tx = (void *)1;
  if (rx)
    *rx = (void *)1;
  return ESP_OK;
}
int i2s_channel_init_std_mode(i2s_chan_handle_t h, const i2s_std_config_t *c) {
  (void)h;
  assert(c->clk_cfg == 16000);
  assert(c->slot_cfg == I2S_SLOT_MODE_STEREO);
  assert(c->gpio_cfg.bclk == 41 && c->gpio_cfg.ws == 43);
  assert(c->gpio_cfg.mclk == I2S_GPIO_UNUSED);
  assert(receiving ? c->gpio_cfg.din == 46 : c->gpio_cfg.dout == 42);
  return fail_config ? -1 : 0;
}
int i2s_channel_register_event_callback(i2s_chan_handle_t h,
                                        const i2s_event_callbacks_t *c,
                                        void *ctx) {
  (void)h;
  (void)ctx;
  callbacks = *c;
  return 0;
}
int i2s_channel_enable(i2s_chan_handle_t h) {
  (void)h;
  assert(!enabled);
  enabled = true;
  return 0;
}
int i2s_channel_disable(i2s_chan_handle_t h) {
  (void)h;
  assert(enabled);
  enabled = false;
  return 0;
}
int i2s_del_channel(i2s_chan_handle_t h) {
  (void)h;
  assert(channels && !enabled);
  --channels;
  return 0;
}
int i2s_channel_read(i2s_chan_handle_t h, void *data, size_t bytes, size_t *n,
                     unsigned timeout) {
  (void)h;
  (void)timeout;
  assert(enabled && receiving);
  bool capture = !pthread_equal(pthread_self(), foreground);
  if (capture) {
    delay();
    if (atomic_load(&fail_read))
      return -1;
  }
  int16_t *stereo = data;
  for (size_t i = 0; i < bytes / 4; ++i) {
    int16_t value = raw_sample(sample++);
    stereo[i * 2] = signal_mode == LEFT_SIGNAL ? value : 0;
    stereo[i * 2 + 1] =
        signal_mode == RIGHT_SIGNAL || signal_mode == EXTREMES ? value : 0;
  }
  *n = bytes;
  if (capture)
    atomic_fetch_add(&blocks, 1);
  return 0;
}
int i2s_channel_write(i2s_chan_handle_t h, const void *p, size_t n,
                      size_t *written, unsigned t) {
  (void)h;
  (void)t;
  assert(!receiving);
  const int16_t *pcm = p;
  for (size_t i = 0; i < n / 2; ++i) {
    unsigned v = pcm[i] < 0 ? -(int)pcm[i] : pcm[i];
    unsigned peak = atomic_load(&output_peak);
    while (peak < v && !atomic_compare_exchange_weak(&output_peak, &peak, v))
      ;
  }
  uint32_t wave_hash = atomic_load(&waveform);
  for (size_t i = 0; i < n / 2; ++i)
    wave_hash = (wave_hash ^ (uint16_t)pcm[i]) * 16777619u;
  atomic_store(&waveform, wave_hash);
  atomic_fetch_add(&output_writes, 1);
  *written = n;
  return 0;
}
StreamBufferHandle_t xStreamBufferCreate(size_t n, size_t trigger) {
  (void)trigger;
  if (fail_alloc)
    return NULL;
  struct test_stream *s = calloc(1, sizeof(*s));
  assert(s);
  s->bytes = malloc(n);
  assert(s->bytes);
  s->capacity = n;
  pthread_mutex_init(&s->lock, NULL);
  ++allocated;
  return s;
}
size_t xStreamBufferSend(StreamBufferHandle_t s, const void *p, size_t n,
                         unsigned timeout) {
  (void)timeout;
  pthread_mutex_lock(&s->lock);
  size_t count = n < s->capacity - s->count ? n : s->capacity - s->count;
  for (size_t i = 0; i < count; ++i) {
    s->bytes[s->write] = ((const unsigned char *)p)[i];
    s->write = (s->write + 1) % s->capacity;
  }
  s->count += count;
  pthread_mutex_unlock(&s->lock);
  return count;
}
size_t xStreamBufferReceive(StreamBufferHandle_t s, void *p, size_t n,
                            unsigned timeout) {
  for (unsigned attempt = 0;; ++attempt) {
    pthread_mutex_lock(&s->lock);
    size_t count = n < s->count ? n : s->count;
    for (size_t i = 0; i < count; ++i) {
      ((unsigned char *)p)[i] = s->bytes[s->read];
      s->read = (s->read + 1) % s->capacity;
    }
    s->count -= count;
    pthread_mutex_unlock(&s->lock);
    if (count || attempt >= timeout)
      return count;
    delay();
  }
}
void vStreamBufferDelete(StreamBufferHandle_t s) {
  pthread_mutex_destroy(&s->lock);
  free(s->bytes);
  free(s);
  --allocated;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
  struct test_sem *s = calloc(1, sizeof(*s));
  assert(s);
  pthread_mutex_init(&s->lock, NULL);
  pthread_cond_init(&s->ready, NULL);
  ++allocated;
  return s;
}
int xSemaphoreGive(SemaphoreHandle_t s) {
  pthread_mutex_lock(&s->lock);
  s->signalled = true;
  pthread_cond_signal(&s->ready);
  pthread_mutex_unlock(&s->lock);
  return pdTRUE;
}
int xSemaphoreTake(SemaphoreHandle_t s, unsigned timeout) {
  (void)timeout;
  pthread_mutex_lock(&s->lock);
  while (!s->signalled)
    pthread_cond_wait(&s->ready, &s->lock);
  s->signalled = false;
  pthread_mutex_unlock(&s->lock);
  return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t s) {
  pthread_cond_destroy(&s->ready);
  pthread_mutex_destroy(&s->lock);
  free(s);
  --allocated;
}
struct thread_args {
  void (*fn)(void *);
  void *ctx;
};
static void *thread_entry(void *p) {
  struct thread_args args = *(struct thread_args *)p;
  free(p);
  args.fn(args.ctx);
  return NULL;
}
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *ctx,
                unsigned priority, void *handle) {
  (void)name;
  (void)stack;
  (void)priority;
  (void)handle;
  if (fail_task)
    return 0;
  struct thread_args *args = malloc(sizeof(*args));
  assert(args);
  *args = (struct thread_args){fn, ctx};
  pthread_t thread;
  atomic_fetch_add(&active_tasks, 1);
  assert(!pthread_create(&thread, NULL, thread_entry, args));
  pthread_detach(thread);
  return pdPASS;
}
void vTaskDelete(void *h) {
  (void)h;
  atomic_fetch_sub(&active_tasks, 1);
  pthread_exit(NULL);
}
TickType_t xTaskGetTickCount(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint32_t)((uint64_t)t.tv_sec * 100 + t.tv_nsec / 10000000);
}
static unsigned random_note;
uint32_t esp_random(void) { return random_note; }
void vTaskDelay(unsigned ticks) {
  unsigned ms = ticks * 10;
  struct timespec t = {ms / 1000, (ms % 1000) * 1000000};
  nanosleep(&t, NULL);
}
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t s) {
  pthread_mutex_lock(&s->lock);
  size_t n = s->count;
  pthread_mutex_unlock(&s->lock);
  return n;
}
static void wait_cue(void) {
  unsigned before = atomic_load(&output_writes);
  for (unsigned i = 0; i < 1000 && atomic_load(&output_writes) == before; ++i)
    delay();
  assert(atomic_load(&output_writes) > before);
  for (unsigned i = 0; i < 1000 && atomic_load(&channels); ++i)
    delay();
  assert(!atomic_load(&channels));
}
int main(void) {
  foreground = pthread_self();
  pos_error_t e = {0};
  int16_t mono[256];
  size_t frames;
  assert(!pos_microphone_start(&e));
  assert(pos_audio_write(mono, 1, 16000, &e)); /* Capture/output exclusion. */
  wait_blocks(20); /* UI + SD stall, over three times the DMA capacity. */
  unsigned expected = 63u * 256u;
  for (unsigned i = 0; i < 20; ++i) {
    assert(!pos_microphone_read(mono, 256, &frames, &e) && frames == 256);
    for (size_t j = 0; j < frames; ++j)
      assert(mono[j] == expected_sample(expected++));
  }
  unsigned left, right;
  pos_microphone_levels(&left, &right);
  assert(left == 0 && right == 128);
  pos_microphone_end();
  unsigned captured = atomic_load(&blocks), delivered = 20 * 256;
  for (;;) {
    assert(!pos_microphone_read(mono, 256, &frames, &e));
    if (!frames)
      break;
    for (size_t j = 0; j < frames; ++j)
      assert(mono[j] == expected_sample(expected++));
    delivered += (unsigned)frames;
  }
  assert(delivered == captured * 256); /* Save every buffered tail frame. */
  pos_microphone_stop();
  assert(!allocated && !channels);
  /* Repeated display stalls followed by batch drainage remain sustainable. */
  sample = 0;
  atomic_store(&blocks, 0);
  assert(!pos_microphone_start(&e));
  int16_t batch[1024];
  expected = 63u * 256u;
  delivered = 0;
  for (unsigned cycle = 0; cycle < 24; ++cycle) {
    wait_blocks(atomic_load(&blocks) + 12);
    while (pos_microphone_pending() >= 256) {
      assert(!pos_microphone_read(batch, 1024, &frames, &e));
      for (size_t j = 0; j < frames; ++j)
        assert(batch[j] == expected_sample(expected++));
      delivered += (unsigned)frames;
    }
  }
  pos_microphone_end();
  captured = atomic_load(&blocks);
  do {
    assert(!pos_microphone_read(batch, 1024, &frames, &e));
    for (size_t j = 0; j < frames; ++j)
      assert(batch[j] == expected_sample(expected++));
    delivered += (unsigned)frames;
  } while (frames);
  assert(delivered == captured * 256);
  pos_microphone_stop();
  assert(!allocated && !channels);
  /* An inactive RIGHT remains silent even if LEFT has data; never pick noise
   * from the wrong channel automatically. Raw L/R diagnostics expose it. */
  for (signal_mode = LEFT_SIGNAL; signal_mode <= EXTREMES; ++signal_mode) {
    sample = 0;
    atomic_store(&blocks, 0);
    assert(!pos_microphone_start(&e));
    wait_blocks(1);
    assert(!pos_microphone_read(mono, 256, &frames, &e) && frames == 256);
    for (size_t j = 0; j < frames; ++j)
      assert(mono[j] ==
             (signal_mode == EXTREMES ? expected_sample(63 * 256 + j) : 0));
    pos_microphone_end();
    pos_microphone_levels(&left, &right);
    if (signal_mode == LEFT_SIGNAL)
      assert(left == 128 && right == 0);
    if (signal_mode == SILENT)
      assert(left == 0 && right == 0);
    if (signal_mode == EXTREMES)
      assert(left == 0 && right == 32768);
    pos_microphone_levels(&left, &right);
    assert(left == 0 && right == 0); /* Readouts clear the previous window. */
    pos_microphone_stop();
    assert(!allocated && !channels && !enabled);
  }
  signal_mode = RIGHT_SIGNAL;
  fail_config = true;
  assert(pos_microphone_start(&e));
  fail_config = false;
  assert(!allocated && !channels && !enabled);
  fail_readback = true;
  assert(pos_microphone_start(&e));
  assert(strstr(e.message, "readback"));
  fail_readback = false;
  assert(!allocated && !channels && !enabled);
  /* Gain changes alter captured PCM, including at quiet levels and limits. */
  const unsigned gains[] = {1, 3, 8, 64, 127, 256, 1537, 4096};
  for (unsigned i = 0; i < sizeof(gains) / sizeof(gains[0]); ++i) {
    unsigned gain = gains[i];
    reference_gain = gain;
    assert(!pos_microphone_set_gain(gain));
    signal_mode = EXTREMES;
    sample = 0;
    atomic_store(&blocks, 0);
    assert(!pos_microphone_start(&e));
    wait_blocks(1);
    assert(!pos_microphone_read(mono, 256, &frames, &e));
    for (size_t j = 0; j < frames; ++j)
      assert(mono[j] == expected_sample(63 * 256 + j));
    pos_microphone_stop();
    assert(!allocated && !channels && !enabled);
  }
  /* A control change while I2S is running reaches the PCM producer. */
  signal_mode = RIGHT_SIGNAL;
  sample = 0;
  atomic_store(&blocks, 0);
  assert(!pos_microphone_set_gain(1));
  assert(!pos_microphone_start(&e));
  wait_blocks(1);
  assert(!pos_microphone_read(mono, 256, &frames, &e));
  for (size_t i = 0; i < frames; ++i)
    assert(mono[i] >= -128 && mono[i] <= 128);
  unsigned before = atomic_load(&blocks);
  assert(!pos_microphone_set_gain(64));
  wait_blocks(before + 3);
  pos_microphone_end();
  bool amplified = false;
  do {
    assert(!pos_microphone_read(mono, 256, &frames, &e));
    for (size_t i = 0; i < frames; ++i)
      amplified |= mono[i] > 1024 || mono[i] < -1024;
  } while (frames);
  assert(amplified);
  pos_microphone_stop();
  assert(!allocated && !channels && !enabled);
  assert(pos_microphone_set_gain(0) && pos_microphone_set_gain(4097) &&
         pos_microphone_set_gain(UINT_MAX));
  assert(!pos_microphone_set_gain(8));
  reference_gain = 8;
  signal_mode = RIGHT_SIGNAL;
  /* A prolonged storage stall is reported, never silently dropped. */
  atomic_store(&blocks, 0);
  assert(!pos_microphone_start(&e));
  wait_blocks(126);
  pos_microphone_end();
  size_t retained = 0;
  while (!pos_microphone_read(mono, 256, &frames, &e)) {
    retained += frames;
    assert(frames);
  }
  assert(retained == 32000 && strstr(e.message, "Audio buffer filled") &&
         frames == 0);
  pos_microphone_stop();
  assert(!allocated && !channels);
  /* Failed allocation/task creation and I2S errors release all resources. */
  fail_alloc = true;
  assert(pos_microphone_start(&e));
  fail_alloc = false;
  assert(!allocated && !channels);
  fail_task = true;
  assert(pos_microphone_start(&e));
  fail_task = false;
  assert(!allocated && !channels);
  atomic_store(&fail_read, true);
  assert(!pos_microphone_start(&e));
  for (unsigned i = 0; i < 20; ++i)
    delay();
  while (!pos_microphone_read(mono, 256, &frames, &e))
    assert(frames);
  pos_microphone_stop();
  atomic_store(&fail_read, false);
  assert(!allocated && !channels);
  assert(!pos_microphone_start(&e));
  callbacks.on_recv_q_ovf((void *)1, NULL, NULL);
  for (unsigned i = 0; i < 20; ++i)
    delay();
  while (!pos_microphone_read(mono, 256, &frames, &e))
    assert(frames);
  pos_microphone_stop();
  pos_microphone_stop();
  assert(!allocated && !channels);
  /* Actual asynchronous cue worker, waveform volume and shared ownership. */
  assert(!pos_audio_feedback_configure(true, 20, true, true));
  pos_audio_feedback(POS_SOUND_NAV);
  wait_cue();
  unsigned quiet = atomic_exchange(&output_peak, 0);
  assert(quiet > 0 && quiet <= 3277);
  assert(!pos_audio_feedback_configure(true, 40, true, true));
  pos_audio_feedback(POS_SOUND_NAV);
  wait_cue();
  unsigned louder = atomic_exchange(&output_peak, 0);
  assert(louder >= quiet * 2 - 2 && louder <= 6554);
  unsigned writes = atomic_load(&output_writes);
  assert(!pos_audio_feedback_configure(false, 40, true, true));
  pos_audio_feedback(POS_SOUND_NOTICE);
  vTaskDelay(30);
  assert(atomic_load(&output_writes) == writes);
  assert(!pos_audio_feedback_configure(true, 40, false, false));
  pos_audio_feedback(POS_SOUND_KEY);
  pos_audio_feedback(POS_SOUND_NOTICE);
  vTaskDelay(30);
  assert(atomic_load(&output_writes) == writes);
  pos_audio_feedback(POS_SOUND_PREVIEW);
  wait_cue();
  assert(!pos_audio_feedback_configure(true, 40, true, true));
  int16_t silence[512] = {0};
  assert(!pos_audio_write(silence, 256, 16000, &e));
  writes = atomic_load(&output_writes);
  pos_audio_feedback(POS_SOUND_NOTICE);
  vTaskDelay(30);
  assert(atomic_load(&output_writes) == writes && channels == 1);
  pos_audio_stop();
  assert(!pos_microphone_start(&e));
  writes = atomic_load(&output_writes);
  pos_audio_feedback(POS_SOUND_NOTICE);
  vTaskDelay(20);
  assert(atomic_load(&output_writes) == writes && pos_microphone_active());
  pos_microphone_stop();
  /* A 100 Hz scheduler must wait for an active cue before playing a WAV.
   * The old 5-ms delay rounded to zero and exhausted retries immediately. */
  unsigned before_cue = atomic_load(&output_writes);
  pos_audio_feedback(POS_SOUND_NOTICE);
  for (unsigned i = 0; i < 1000 && atomic_load(&output_writes) == before_cue;
       ++i)
    delay();
  assert(atomic_load(&output_writes) > before_cue);
  assert(!pos_audio_write(silence, 256, 16000, &e));
  pos_audio_stop();
  assert(!pos_microphone_start(&e));
  pos_microphone_stop();
  assert(!pos_audio_write(silence, 256, 16000, &e));
  pos_audio_stop();
  before_cue = atomic_load(&output_writes);
  pos_audio_feedback(POS_SOUND_STARTUP);
  wait_cue();
  assert(atomic_load(&output_writes) >= before_cue + 44);
  atomic_store(&waveform, 2166136261u);
  random_note = 1;
  pos_audio_feedback(POS_SOUND_NAV);
  wait_cue();
  unsigned first_note = atomic_load(&waveform);
  atomic_store(&waveform, 2166136261u);
  random_note = 4;
  pos_audio_feedback(POS_SOUND_NAV);
  wait_cue();
  assert(first_note != atomic_load(&waveform));
  pos_audio_feedback_hold(true);
  pos_audio_feedback_hold(true);
  writes = atomic_load(&output_writes);
  pos_audio_feedback(POS_SOUND_NAV);
  vTaskDelay(30);
  assert(atomic_load(&output_writes) == writes);
  pos_audio_feedback_hold(false);
  pos_audio_feedback(POS_SOUND_NAV);
  vTaskDelay(30);
  assert(atomic_load(&output_writes) == writes);
  pos_audio_feedback_hold(false);
  pos_audio_feedback(POS_SOUND_NAV);
  wait_cue();
  assert(atomic_load(&output_writes) > writes);
  pos_audio_feedback_shutdown();
  for (unsigned i = 0; i < 100 && atomic_load(&active_tasks); ++i)
    delay();
  assert(!active_tasks && !allocated && !channels);
  puts("Delayed consumers preserve ordered PCM; overruns/errors stop; restarts "
       "and cleanup pass.");
  return 0;
}
