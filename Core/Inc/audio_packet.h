#ifndef AUDIO_PACKET_H
#define AUDIO_PACKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ---- Packet format constants ---- */
#define AUDIO_PKT_START_BYTE    0xAA
#define AUDIO_PKT_TOTAL_SIZE    109
#define AUDIO_PKT_PAYLOAD_LEN   106   /* bytes 3..108 */
#define AUDIO_PKT_AUDIO_OFFSET  8
#define AUDIO_PKT_AUDIO_SIZE    100   /* bytes of packed ADPCM nibbles */
#define AUDIO_SAMPLES_PER_PKT   200   /* 2 nibbles per byte            */

/* ---- Audio playback config ---- */
#define AUDIO_SAMPLE_RATE       8000
#define AUDIO_RING_BUF_SIZE     1024  /* must be power of 2 */
#define AUDIO_RING_BUF_MASK     (AUDIO_RING_BUF_SIZE - 1)

/* ---- Packed on-wire packet layout (for reference / casting) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  start_byte;           /* 0:   0xAA                       */
    uint16_t payload_length;       /* 1-2: little-endian, == 106      */
    uint8_t  face_number;          /* 3:   robot expression 0-255     */
    uint8_t  audio_sequence;       /* 4:   wrapping counter 0-255     */
    int16_t  adpcm_predicted;      /* 5-6: decoder sync baseline      */
    uint8_t  adpcm_step_index;     /* 7:   decoder sync step (0-88)   */
    uint8_t  audio_data[100];      /* 8-107: 4-bit IMA-ADPCM nibbles  */
    uint8_t  checksum;             /* 108: XOR of bytes 0-107         */
} AudioPacket_t;

/* ---- ADPCM codec state ---- */
typedef struct {
    int16_t  predicted_sample;
    uint8_t  step_index;
} ADPCMState_t;

/* ---- Public API ---- */

/** Initialise audio subsystem (ring buffer, TIM6, DAC channel). */
void Audio_Init(void);

/** Validate checksum, decode ADPCM, push samples to ring buffer.
 *  @return true if packet was valid */
bool Audio_ProcessPacket(const uint8_t *raw);

/** Start 8 kHz TIM6 interrupt → DAC output. */
void Audio_StartPlayback(void);

/** Stop playback, output silence. */
void Audio_StopPlayback(void);

/** Is the DAC currently streaming? */
bool Audio_IsPlaying(void);

/** Number of samples waiting in the ring buffer. */
uint16_t Audio_RingBufCount(void);

/** Face number from the last valid packet. */
uint8_t Audio_GetLastFace(void);

/** Fill a 109-byte buffer with a valid packet containing 440 Hz sine. */
void Audio_GenerateFakePacket(uint8_t *buf);

/** Keep the ring buffer fed with the built-in 440 Hz test tone.
 *  Call from the main loop — does nothing if buffer is already ≥ half full. */
void Audio_FeedFakeData(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PACKET_H */
