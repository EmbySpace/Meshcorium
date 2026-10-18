#include <Arduino.h>
#include "target.h"

JC3248W535Board board;

// Radio gets its own SPI controller (SPI3 / HSPI). The LCD uses SPI2 via the
// ESP-IDF QSPI driver, so the two never share a bus.
static SPIClass spi(HSPI);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
#endif

// Each try is a full SX1262 init (RadioLib begin with the 1.8 V TCXO, then
// again without TCXO, see CustomSX1262::std_init). Between tries the module
// gets time to come up: on a cold power-on the E22's own 5 V boost converter
// can start later than the ESP32.
#ifndef JC3248_RADIO_INIT_TRIES
  #define JC3248_RADIO_INIT_TRIES 3
#endif
#ifndef JC3248_RADIO_RETRY_MS
  #define JC3248_RADIO_RETRY_MS 1000
#endif

bool radio_init() {
#ifdef JC3248_DEBUG_BOOT_WAIT_MS
  // debug build: give the USB serial monitor time to attach
  uint32_t t0 = millis();
  while (millis() - t0 < JC3248_DEBUG_BOOT_WAIT_MS) delay(10);
  Serial.println();
  Serial.println("=== JC3248W535 debug build ===");
  Serial.printf("E22 pins: NSS=%d SCK=%d MOSI=%d MISO=%d RST=%d BUSY=%d DIO1=%d RXEN=%d\n",
                P_LORA_NSS, P_LORA_SCLK, P_LORA_MOSI, P_LORA_MISO, P_LORA_RESET, P_LORA_BUSY,
                P_LORA_DIO_1, SX126X_RXEN);
  Serial.printf("LoRa: freq=%.3f bw=%.1f sf=%d tx_power=%d\n", (float)LORA_FREQ, (float)LORA_BW, LORA_SF, LORA_TX_POWER);
#endif

  fallback_clock.begin();
  rtc_clock.begin(Wire);

  bool ok = false;
  for (int attempt = 1; attempt <= JC3248_RADIO_INIT_TRIES; attempt++) {
    ok = radio.std_init(&spi);
    if (ok) break;
    Serial.printf("radio init attempt %d/%d failed\n", attempt, JC3248_RADIO_INIT_TRIES);
    if (attempt < JC3248_RADIO_INIT_TRIES) delay(JC3248_RADIO_RETRY_MS);
  }
  if (!ok) {
    Serial.println("LoRa module error");
  #ifdef DISPLAY_CLASS
    display.showRadioError();     // full-screen error; main.cpp halts after this
  #endif
  }

#ifdef JC3248_DEBUG_BOOT_WAIT_MS
  Serial.printf("radio init: %s\n", ok ? "OK" : "FAILED");
  if (ok) {
    // device_errors bits (SX1262 datasheet, GetDeviceErrors):
    //  0x0001 RC64K_CALIB 0x0002 RC13M_CALIB 0x0004 PLL_CALIB 0x0008 ADC_CALIB
    //  0x0010 IMG_CALIB   0x0020 XOSC_START  0x0040 PLL_LOCK  0x0100 PA_RAMP
    Serial.printf("SX1262 status=0x%02X device_errors=0x%04X BUSY=%d\n",
                  radio.getStatus(), radio.getDeviceErrors(), digitalRead(P_LORA_BUSY));
  }
#endif
  return ok;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
