/*
  AudioOutputI2S
  Base class for I2S interface port
  
  Copyright (C) 2017  Earle F. Philhower, III

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <Arduino.h>
#ifdef ESP32
  #include "driver/i2s.h"
#elif defined K210
  #include "dmac.h"
  #include "fpioa.h"
  #include "i2s.h"
  #include "plic.h"
  extern "C" {
  int i2s_send_data(i2s_device_number_t device_num, i2s_channel_num_t channel_num, const uint8_t *pcm, size_t buf_len,
                  size_t single_length);
  }
#else
  #include <i2s.h>
#endif
#include "AudioOutputI2S.h"

AudioOutputI2S::AudioOutputI2S(int port, int output_mode, int dma_buf_count, int use_apll)
{
  this->portNo = port;
  this->i2sOn = false;
  if (output_mode != EXTERNAL_I2S && output_mode != INTERNAL_DAC && output_mode != INTERNAL_PDM) {
    output_mode = EXTERNAL_I2S;
  }
  this->output_mode = output_mode;
#ifdef ESP32
  if (!i2sOn) {
    if (use_apll == APLL_AUTO) {
      // don't use audio pll on buggy rev0 chips
      use_apll = APLL_DISABLE;
      esp_chip_info_t out_info;
      esp_chip_info(&out_info);
      if(out_info.revision > 0) {
        use_apll = APLL_ENABLE;
      }
    }

    i2s_mode_t mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    if (output_mode == INTERNAL_DAC) {
      mode = (i2s_mode_t)(mode | I2S_MODE_DAC_BUILT_IN);
    } else if (output_mode == INTERNAL_PDM) {
      mode = (i2s_mode_t)(mode | I2S_MODE_PDM);
    }

    i2s_comm_format_t comm_fmt = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    if (output_mode == INTERNAL_DAC) {
      comm_fmt = (i2s_comm_format_t)I2S_COMM_FORMAT_I2S_MSB;
    }

    i2s_config_t i2s_config_dac = {
      .mode = mode,
      .sample_rate = 44100,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = comm_fmt,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // lowest interrupt priority
      .dma_buf_count = dma_buf_count,
      .dma_buf_len = 64,
      .use_apll = use_apll // Use audio PLL
    };
    Serial.printf("+%d %p\n", portNo, &i2s_config_dac);
    if (i2s_driver_install((i2s_port_t)portNo, &i2s_config_dac, 0, NULL) != ESP_OK) {
      Serial.println("ERROR: Unable to install I2S drives\n");
    }
    if (output_mode == INTERNAL_DAC || output_mode == INTERNAL_PDM) {
      i2s_set_pin((i2s_port_t)portNo, NULL);
      i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    } else {
      SetPinout(26, 25, 22);
    }
    i2s_zero_dma_buffer((i2s_port_t)portNo);
  } 
#elif defined K210
  i2s_init((i2s_device_number_t)portNo, I2S_TRANSMITTER, 0xC);
  i2s_set_sample_rate((i2s_device_number_t)portNo, 44100);
//  i2s_tx_channel_config((i2s_device_number_t)portNo, I2S_CHANNEL_0, RESOLUTION_16_BIT, SCLK_CYCLES_32, TRIGGER_LEVEL_1, RIGHT_JUSTIFYING_MODE);
  i2s_tx_channel_config((i2s_device_number_t)portNo, I2S_CHANNEL_1, RESOLUTION_16_BIT, SCLK_CYCLES_32, TRIGGER_LEVEL_4, RIGHT_JUSTIFYING_MODE);

  //SetPinout(35, 33, 34);
//  SetPinout(I2S_BCK, I2S_WS, I2S_DA);
#else
  (void) dma_buf_count;
  (void) use_apll;
  if (!i2sOn) {
    i2s_begin();
  }
#endif
  i2sOn = true;
  mono = false;
  bps = 16;
  channels = 2;
  SetGain(1.0);
  SetRate(44100); // Default
}

AudioOutputI2S::~AudioOutputI2S()
{
#ifdef ESP32
  if (i2sOn) {
    Serial.printf("UNINSTALL I2S\n");
    i2s_driver_uninstall((i2s_port_t)portNo); //stop & destroy i2s driver
  }
#elif defined K210
  //
#else
  if (i2sOn) i2s_end();
#endif
  i2sOn = false;
}

bool AudioOutputI2S::SetPinout(int bclk, int wclk, int dout)
{
#ifdef ESP32
  if (output_mode == INTERNAL_DAC || output_mode == INTERNAL_PDM) return false; // Not allowed

  i2s_pin_config_t pins = {
    .bck_io_num = bclk,
    .ws_io_num = wclk,
    .data_out_num = dout,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_set_pin((i2s_port_t)portNo, &pins);
  return true;
#elif defined K210
//  fpioa_set_function(dout, (fpioa_function_t) (FUNC_I2S0_OUT_D0 + (portNo * 11))); //34
//  fpioa_set_function(bclk, (fpioa_function_t) (FUNC_I2S0_SCLK + (portNo * 11))); //35
//  fpioa_set_function(wclk, (fpioa_function_t) (FUNC_I2S0_WS + (portNo * 11))); //33
  fpioa_set_function(dout, (fpioa_function_t) (FUNC_I2S0_OUT_D1)); //34
  fpioa_set_function(bclk, (fpioa_function_t) (FUNC_I2S0_SCLK)); //35
  fpioa_set_function(wclk, (fpioa_function_t) (FUNC_I2S0_WS)); //33
#else
  (void) bclk;
  (void) wclk;
  (void) dout;
  return false;
#endif
}

bool AudioOutputI2S::SetRate(int hz)
{
  // TODO - have a list of allowable rates from constructor, check them
  this->hertz = hz;
#ifdef ESP32
  i2s_set_sample_rates((i2s_port_t)portNo, AdjustI2SRate(hz)); 
#elif defined K210
//TODO
//  i2s_set_sample_rate((i2s_device_number_t)portNo, AdjustI2SRate(hz));
#else
  i2s_set_rate(AdjustI2SRate(hz));
#endif
  return true;
}

bool AudioOutputI2S::SetBitsPerSample(int bits)
{
  if ( (bits != 16) && (bits != 8) ) return false;
  this->bps = bits;
  return true;
}

bool AudioOutputI2S::SetChannels(int channels)
{
  if ( (channels < 1) || (channels > 2) ) return false;
  this->channels = channels;
  return true;
}

bool AudioOutputI2S::SetOutputModeMono(bool mono)
{
  this->mono = mono;
  return true;
}

bool AudioOutputI2S::begin()
{
  return true;
}

bool AudioOutputI2S::ConsumeSample(int16_t sample[2])
{
  int16_t ms[2];

  ms[0] = sample[0];
  ms[1] = sample[1];
  MakeSampleStereo16( ms );

  if (this->mono) {
    // Average the two samples and overwrite
    int32_t ttl = ms[LEFTCHANNEL] + ms[RIGHTCHANNEL];
    ms[LEFTCHANNEL] = ms[RIGHTCHANNEL] = (ttl>>1) & 0xffff;
  }
#ifdef ESP32
  uint32_t s32;
  if (output_mode == INTERNAL_DAC) {
    int16_t l = Amplify(ms[LEFTCHANNEL]) + 0x8000;
    int16_t r = Amplify(ms[RIGHTCHANNEL]) + 0x8000;
    s32 = (r<<16) | (l&0xffff);
  } else {
    s32 = ((Amplify(ms[RIGHTCHANNEL]))<<16) | (Amplify(ms[LEFTCHANNEL]) & 0xffff);
  }
  return i2s_write_bytes((i2s_port_t)portNo, (const char*)&s32, sizeof(uint32_t), 0);
#elif defined K210
//  return i2s_send_data((i2s_device_number_t)portNo, I2S_CHANNEL_0, (const uint8_t *)ms, sizeof(int16_t) * 2, 16);
  ms[0] = Amplify(ms[LEFTCHANNEL]);
  ms[1] = Amplify(ms[RIGHTCHANNEL]);
  dmac_wait_done(DMAC_CHANNEL0);
  i2s_send_data_dma((i2s_device_number_t)portNo, (const uint8_t *)ms, sizeof(int16_t) * 2, DMAC_CHANNEL0);
//  i2s_play((i2s_device_number_t)portNo, DMAC_CHANNEL0, (const uint8_t *)ms, sizeof(int16_t) * 2, 2, this->bps, this->channels);
  return true;
#else
  uint32_t s32 = ((Amplify(ms[RIGHTCHANNEL]))<<16) | (Amplify(ms[LEFTCHANNEL]) & 0xffff);
  return i2s_write_sample_nb(s32); // If we can't store it, return false.  OTW true
#endif
}

uint16_t AudioOutputI2S::ConsumeSamples(int16_t *samples, uint16_t count)
{
  int16_t ms[2];
  int16_t out[64];
  int16_t validSamples = count;
  int16_t curSample = 0;

  while (validSamples) {
    ms[0] = samples[curSample * 2];
    ms[1] = samples[curSample * 2 + 1];
    MakeSampleStereo16( ms );
    if (this->mono) {
      // Average the two samples and overwrite
      int32_t ttl = ms[LEFTCHANNEL] + ms[RIGHTCHANNEL];
      ms[LEFTCHANNEL] = ms[RIGHTCHANNEL] = (ttl>>1) & 0xffff;
    }
    out[curSample*2] = ms[0];
    out[curSample*2 + 1] = ms[1];
    validSamples--;
    curSample++;
  }

#ifdef K210
  dmac_wait_done(DMAC_CHANNEL0);
  i2s_play((i2s_device_number_t)portNo,
          DMAC_CHANNEL0, (uint8_t *)out, sizeof(out), 64, this->bps, this->channels);
#endif
  return true;
}

bool AudioOutputI2S::stop()
{
#ifdef ESP32
  i2s_zero_dma_buffer((i2s_port_t)portNo);
#endif
  return true;
}


