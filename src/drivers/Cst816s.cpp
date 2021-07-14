#include "Cst816s.h"
#include <FreeRTOS.h>
#include <legacy/nrf_drv_gpiote.h>
#include <nrfx_log.h>
#include <task.h>

using namespace Pinetime::Drivers;

/* References :
 * This implementation is based on this article :
 * https://medium.com/@ly.lee/building-a-rust-driver-for-pinetimes-touch-controller-cbc1a5d5d3e9 Touch panel datasheet (weird chinese
 * translation) : https://wiki.pine64.org/images/5/51/CST816S%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8CV1.1.en.pdf
 *
 * TODO : we need a complete datasheet and protocol reference!
 * */

Cst816S::Cst816S(TwiMaster& twiMaster, uint8_t twiAddress) : twiMaster {twiMaster}, twiAddress {twiAddress} {
}

void Cst816S::Init() {
  nrf_gpio_cfg_output(pinReset);
  nrf_gpio_pin_set(pinReset);
  vTaskDelay(50);
  nrf_gpio_pin_clear(pinReset);
  vTaskDelay(5);
  nrf_gpio_pin_set(pinReset);
  vTaskDelay(50);

  // Wake the touchpanel up
  uint8_t dummy;
  twiMaster.Read(twiAddress, 0x15, &dummy, 1);
  vTaskDelay(5);
  twiMaster.Read(twiAddress, 0xa7, &dummy, 1);
  vTaskDelay(5);

  /*
  [2] EnConLR - Continuous operation can slide around
  [1] EnConUD - Slide up and down to enable continuous operation
  [0] EnDClick - Enable Double-click action
  */
  static constexpr uint8_t motionMask = 0b00000101;
  twiMaster.Write(twiAddress, 0xEC, &motionMask, 1);

  // There's mixed information about which register contains which information
  if (twiMaster.Read(twiAddress, 0xA7, &chipId, 1) == TwiMaster::ErrorCodes::TransactionFailed) {
    chipId = 0xFF;
  }
  if (twiMaster.Read(twiAddress, 0xA8, &vendorId, 1) == TwiMaster::ErrorCodes::TransactionFailed) {
    vendorId = 0xFF;
  }
  if (twiMaster.Read(twiAddress, 0xA9, &fwVersion, 1) == TwiMaster::ErrorCodes::TransactionFailed) {
    fwVersion = 0xFF;
  }
}

Cst816S::TouchInfos Cst816S::GetTouchInfo() {
  Cst816S::TouchInfos info;

  auto ret = twiMaster.Read(twiAddress, 0, touchData, sizeof(touchData));
  if (ret != TwiMaster::ErrorCodes::NoError) {
    info.isValid = false;
    return info;
  }

  // This can only be 0 or 1
  auto nbTouchPoints = touchData[touchPointNumIndex] & 0x0f;

  auto xHigh = touchData[touchXHighIndex] & 0x0f;
  auto xLow = touchData[touchXLowIndex];
  uint16_t x = (xHigh << 8) | xLow;

  auto yHigh = touchData[touchYHighIndex] & 0x0f;
  auto yLow = touchData[touchYLowIndex];
  uint16_t y = (yHigh << 8) | yLow;

  auto action = touchData[touchEventIndex] >> 6; /* 0 = Down, 1 = Up, 2 = contact*/

  info.x = x;
  info.y = y;
  info.action = action;
  info.touching = (nbTouchPoints > 0);
  info.gesture = static_cast<Gestures>(touchData[gestureIndex]);

  return info;
}

void Cst816S::Sleep() {
  nrf_gpio_pin_clear(pinReset);
  vTaskDelay(5);
  nrf_gpio_pin_set(pinReset);
  vTaskDelay(50);
  static constexpr uint8_t sleepValue = 0x03;
  twiMaster.Write(twiAddress, 0xA5, &sleepValue, 1);
  NRF_LOG_INFO("[TOUCHPANEL] Sleep");
}

void Cst816S::Wakeup() {
  Init();
  NRF_LOG_INFO("[TOUCHPANEL] Wakeup");
}
