/*
 * namco_wsg.cpp - Namco WSG (Waveform Sound Generator) Emulation
 *
 * Ported from Galagino by Till Harbaum
 */

#include "namco_wsg.h"
#include <cstring>

// Internal state for 3 channels
static uint32_t snd_cnt[WSG_CHANNELS] = {0, 0, 0};
static uint32_t snd_freq[WSG_CHANNELS] = {0, 0, 0};
static const int8_t *snd_wave[WSG_CHANNELS] = {nullptr, nullptr, nullptr};
static uint8_t snd_volume[WSG_CHANNELS] = {0, 0, 0};

// Wavetable pointer (set from ROM data)
static const int8_t *wavetable_data = nullptr;

// Default silent waveform
static const int8_t silent_wave[WSG_WAVE_SIZE] = {0};

void wsg_init(const int8_t *wavetable)
{
    wavetable_data = wavetable;

    for (int ch = 0; ch < WSG_CHANNELS; ch++) {
        snd_cnt[ch] = 0;
        snd_freq[ch] = 0;
        snd_wave[ch] = silent_wave;
        snd_volume[ch] = 0;
    }
}

void wsg_parse_registers(const uint8_t *regs)
{
    if (!regs) return;

    /*
     * Pac-Man sound register layout:
     *
     * Voice 0 (at 0x5040-0x5054 in memory, offset 0x00-0x14 here):
     *   0x10: frequency[3:0] (only voice 0 has this extra nibble)
     *   0x11: frequency[7:4]
     *   0x12: frequency[11:8]
     *   0x13: frequency[15:12]
     *   0x14: frequency[19:16]
     *   0x15: volume
     *
     * Voice 1 (at 0x5046-0x5055):
     *   0x00: frequency[3:0] + waveform (combined)
     *   0x01-0x04: frequency[7:4] to [19:16]
     *   0x06: waveform select (upper nibble sometimes)
     *   0x16: volume
     *
     * Actually, the layout from Galagino:
     *   Channel 0: regs 0x10..0x14 freq, 0x15 vol, 0x05 wave
     *   Channel 1: regs 0x11..0x14 freq (partial), 0x16 vol
     *   Channel 2: ...
     *
     * Let's use Galagino's proven parsing:
     */

    // Parse all three WSG channels
    for (int ch = 0; ch < WSG_CHANNELS; ch++) {
        // Channel volume
        snd_volume[ch] = regs[ch * 5 + 0x15] & 0x0F;

        if (snd_volume[ch]) {
            // Frequency (20-bit accumulator)
            // Channel 0 has extra low nibble at 0x10
            snd_freq[ch] = (ch == 0) ? (regs[0x10] & 0x0F) : 0;
            snd_freq[ch] |= (regs[ch * 5 + 0x11] & 0x0F) << 4;
            snd_freq[ch] |= (regs[ch * 5 + 0x12] & 0x0F) << 8;
            snd_freq[ch] |= (regs[ch * 5 + 0x13] & 0x0F) << 12;
            snd_freq[ch] |= (regs[ch * 5 + 0x14] & 0x0F) << 16;

            // Waveform select
            uint8_t wave_idx = regs[ch * 5 + 0x05] & 0x0F;
            if (wavetable_data) {
                snd_wave[ch] = wavetable_data + (wave_idx * WSG_WAVE_SIZE);
            } else {
                snd_wave[ch] = silent_wave;
            }
        } else {
            snd_freq[ch] = 0;
            snd_wave[ch] = silent_wave;
        }
    }
}

void wsg_render(uint16_t *buffer, uint32_t samples)
{
    for (uint32_t i = 0; i < samples; i++) {
        int32_t v = 0;

        // Add up all three wave channels
        if (snd_volume[0]) {
            // Use upper 5 bits of counter as wave index (32 samples)
            v += snd_volume[0] * snd_wave[0][(snd_cnt[0] >> 13) & 0x1F];
        }
        if (snd_volume[1]) {
            v += snd_volume[1] * snd_wave[1][(snd_cnt[1] >> 13) & 0x1F];
        }
        if (snd_volume[2]) {
            v += snd_volume[2] * snd_wave[2][(snd_cnt[2] >> 13) & 0x1F];
        }

        // v is now in range of roughly +/- 512 (3 channels * 15 vol * ~8 wave amplitude)
        // Scale to 16-bit - use 48 instead of 64 to reduce distortion
        v = v * 48;

        // Clamp to signed 16-bit range then convert to unsigned
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;

        // Convert to unsigned 16-bit (0x8000 = center/silence)
        buffer[i] = (uint16_t)(0x8000 + v);

        // Advance phase counters
        snd_cnt[0] += snd_freq[0];
        snd_cnt[1] += snd_freq[1];
        snd_cnt[2] += snd_freq[2];
    }
}
