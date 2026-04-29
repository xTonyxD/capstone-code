#include "audio_packet.h"
#include "dac.h"
#include "main.h"
#include <string.h>
#include <math.h>

/* =====================================================================
 *  IMA ADPCM tables
 * ===================================================================== */

static const int16_t step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t index_table[16] = {
    -1, -1, -1, -1,  2,  4,  6,  8,
    -1, -1, -1, -1,  2,  4,  6,  8
};

/* =====================================================================
 *  Ring buffer (ISR pops, main-loop pushes)
 * ===================================================================== */

static struct {
    uint16_t buf[AUDIO_RING_BUF_SIZE];   /* 12-bit DAC values 0-4095 */
    volatile uint16_t head;              /* written by decoder       */
    volatile uint16_t tail;              /* read by TIM6 ISR         */
} ring;

/* ---- Private state ---- */
static ADPCMState_t decoder;
static volatile bool playing = false;
static uint8_t last_face  = 0;

/* Pre-built fake packet (generated once) */
static uint8_t  fake_pkt[AUDIO_PKT_TOTAL_SIZE];
static bool     fake_pkt_ready = false;
static uint8_t  last_pkt[AUDIO_PKT_TOTAL_SIZE];
static bool     last_pkt_ready = false;

/* =====================================================================
 *  Ring-buffer helpers
 * ===================================================================== */

static inline uint16_t ring_count(void)
{
    return (ring.head - ring.tail) & AUDIO_RING_BUF_MASK;
}

static inline uint16_t ring_free(void)
{
    return AUDIO_RING_BUF_SIZE - 1 - ring_count();
}

static inline void ring_push(uint16_t sample)
{
    ring.buf[ring.head] = sample;
    ring.head = (ring.head + 1) & AUDIO_RING_BUF_MASK;
}

static inline uint16_t ring_pop(void)
{
    uint16_t val = ring.buf[ring.tail];
    ring.tail = (ring.tail + 1) & AUDIO_RING_BUF_MASK;
    return val;
}

/* =====================================================================
 *  IMA ADPCM codec
 * ===================================================================== */

static int16_t adpcm_decode_nibble(uint8_t nibble, ADPCMState_t *s)
{
    int16_t step = step_table[s->step_index];
    int32_t diff = step >> 3;

    if (nibble & 4) diff += step;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 1) diff += step >> 2;

    if (nibble & 8)
        s->predicted_sample -= (int16_t)diff;
    else
        s->predicted_sample += (int16_t)diff;

    if (s->predicted_sample >  32767) s->predicted_sample =  32767;
    if (s->predicted_sample < -32768) s->predicted_sample = -32768;

    int idx = s->step_index + index_table[nibble & 0x0F];
    if (idx < 0)  idx = 0;
    if (idx > 88) idx = 88;
    s->step_index = (uint8_t)idx;

    return s->predicted_sample;
}

static uint8_t adpcm_encode_sample(int16_t sample, ADPCMState_t *s)
{
    int16_t step = step_table[s->step_index];
    int32_t diff = (int32_t)sample - s->predicted_sample;
    uint8_t nibble = 0;

    if (diff < 0) { nibble = 8; diff = -diff; }
    if (diff >= step)          { nibble |= 4; diff -= step; }
    if (diff >= (step >> 1))   { nibble |= 2; diff -= step >> 1; }
    if (diff >= (step >> 2))   { nibble |= 1; }

    /* Decode with the same nibble to keep encoder in sync with decoder */
    adpcm_decode_nibble(nibble, s);
    return nibble;
}

/* =====================================================================
 *  Checksum  (XOR of bytes 0 .. N-2)
 * ===================================================================== */

static uint8_t compute_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t ck = 0;
    for (uint16_t i = 0; i < len - 1; i++)
        ck ^= data[i];
    return ck;
}

/* =====================================================================
 *  TIM6 — direct register access (no HAL_TIM module needed)
 * ===================================================================== */

static void TIM6_Setup(void)
{
    /* Enable TIM6 peripheral clock */
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    __DSB();   /* wait for clock to stabilise */

    TIM6->CR1  = 0;
    TIM6->PSC  = 0;                                              /* no prescaler  */
    TIM6->ARR  = (HAL_RCC_GetPCLK1Freq() / AUDIO_SAMPLE_RATE) - 1; /* 8 kHz */
    TIM6->DIER = TIM_DIER_UIE;                                   /* update IRQ    */
    TIM6->CR1  = TIM_CR1_ARPE;                                   /* auto-reload   */
    TIM6->EGR  = TIM_EGR_UG;                                     /* load shadow   */
    TIM6->SR   = 0;                                               /* clear flags   */

    NVIC_SetPriority(TIM6_IRQn, 2);
    NVIC_EnableIRQ(TIM6_IRQn);
}

/* =====================================================================
 *  Public API
 * ===================================================================== */

void Audio_Init(void)
{
    ring.head = 0;
    ring.tail = 0;
    decoder.predicted_sample = 0;
    decoder.step_index = 0;
    playing = false;
    last_pkt_ready = false;

    TIM6_Setup();

    /* Enable DAC channel 1 output (EN1 bit) */
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
    /* Park at mid-scale (silence) */
    DAC1->DHR12R1 = 2048;
}

bool Audio_ProcessPacket(const uint8_t *raw)
{
    if (raw[0] != AUDIO_PKT_START_BYTE)
        return false;

    if (raw[AUDIO_PKT_TOTAL_SIZE - 1] != compute_checksum(raw, AUDIO_PKT_TOTAL_SIZE))
        return false;

    if (raw != last_pkt) {
        memcpy(last_pkt, raw, AUDIO_PKT_TOTAL_SIZE);
    }
    last_pkt_ready = true;

    /* Parse header via packed struct (ARM Cortex-M33 handles unaligned) */
    const AudioPacket_t *pkt = (const AudioPacket_t *)raw;

    last_face = pkt->face_number;

    /* Resync decoder from packet header (handles dropped packets) */
    decoder.predicted_sample = pkt->adpcm_predicted;
    decoder.step_index       = pkt->adpcm_step_index;
    if (decoder.step_index > 88) decoder.step_index = 88;

    /* Decode 100 bytes → 200 samples, push to ring buffer */
    for (int i = 0; i < AUDIO_PKT_AUDIO_SIZE; i++) {
        uint8_t byte   = pkt->audio_data[i];
        uint8_t nib_lo = byte & 0x0F;          /* first sample  */
        uint8_t nib_hi = (byte >> 4) & 0x0F;   /* second sample */

        int16_t s1 = adpcm_decode_nibble(nib_lo, &decoder);
        int16_t s2 = adpcm_decode_nibble(nib_hi, &decoder);

        /* Signed-16 → unsigned-12 for 12-bit DAC */
        uint16_t dac1 = (uint16_t)(((int32_t)s1 + 32768) >> 4);
        uint16_t dac2 = (uint16_t)(((int32_t)s2 + 32768) >> 4);

        if (ring_free() >= 2) {
            ring_push(dac1);
            ring_push(dac2);
        }
    }

    return true;
}

void Audio_StartPlayback(void)
{
    playing = true;
    TIM6->SR  = 0;
    TIM6->CR1 |= TIM_CR1_CEN;   /* start counting */
}

void Audio_StopPlayback(void)
{
    TIM6->CR1 &= ~TIM_CR1_CEN;  /* stop counting */
    playing = false;
    DAC1->DHR12R1 = 2048;        /* silence */
}

bool Audio_IsPlaying(void)
{
    return playing;
}

uint16_t Audio_RingBufCount(void)
{
    return ring_count();
}

uint8_t Audio_GetLastFace(void)
{
    return last_face;
}

/* =====================================================================
 *  TIM6 ISR  — outputs one sample to DAC at 8 kHz
 * ===================================================================== */

void TIM6_IRQHandler(void)
{
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR = ~TIM_SR_UIF;             /* clear update flag */

        if (ring_count() > 0)
            DAC1->DHR12R1 = ring_pop();
        else
            DAC1->DHR12R1 = 2048;           /* underrun → silence */
    }
}

/* =====================================================================
 *  Fake 440 Hz test-tone packet
 * ===================================================================== */

void Audio_GenerateFakePacket(uint8_t *buf)
{
    /* Generate 200 PCM samples of a 440 Hz sine at 8 kHz */
    int16_t pcm[AUDIO_SAMPLES_PER_PKT];
    for (int i = 0; i < AUDIO_SAMPLES_PER_PKT; i++) {
        float t = (float)i / (float)AUDIO_SAMPLE_RATE;
        pcm[i] = (int16_t)(5952.0f * sinf(2.0f * 3.14159265f * 440.0f * t));
    }

    /* Record initial ADPCM state (start of chunk) */
    ADPCMState_t enc = { .predicted_sample = 0, .step_index = 0 };

    /* Build header manually (avoids struct-packing concerns on host) */
    buf[0] = AUDIO_PKT_START_BYTE;
    buf[1] = (uint8_t)(AUDIO_PKT_PAYLOAD_LEN & 0xFF);
    buf[2] = (uint8_t)(AUDIO_PKT_PAYLOAD_LEN >> 8);
    buf[3] = 0;   /* face_number   */
    buf[4] = 0;   /* audio_sequence */
    /* ADPCM predicted sample = 0 (initial) */
    buf[5] = 0;
    buf[6] = 0;
    /* ADPCM step index = 0 (initial) */
    buf[7] = 0;

    /* ADPCM-encode pairs of samples into packed bytes */
    for (int i = 0; i < AUDIO_SAMPLES_PER_PKT; i += 2) {
        uint8_t nib_lo = adpcm_encode_sample(pcm[i],     &enc);
        uint8_t nib_hi = adpcm_encode_sample(pcm[i + 1], &enc);
        buf[AUDIO_PKT_AUDIO_OFFSET + i / 2] = (nib_hi << 4) | nib_lo;
    }

    /* XOR checksum over bytes 0..107 */
    buf[AUDIO_PKT_TOTAL_SIZE - 1] = compute_checksum(buf, AUDIO_PKT_TOTAL_SIZE);
}

void Audio_FeedFakeData(void)
{
    if (!fake_pkt_ready) {
        Audio_GenerateFakePacket(fake_pkt);
        fake_pkt_ready = true;
    }
    /* Refill when less than half full */
    while (ring_count() < AUDIO_RING_BUF_SIZE / 2) {
        Audio_ProcessPacket(fake_pkt);
    }
}

void Audio_FeedLastPacket(void)
{
    if (!last_pkt_ready) {
        return;
    }

    if (ring_count() >= AUDIO_SAMPLES_PER_PKT) {
        return;
    }

    Audio_ProcessPacket(last_pkt);
}
