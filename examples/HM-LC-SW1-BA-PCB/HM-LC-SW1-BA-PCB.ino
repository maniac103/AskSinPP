//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

#define USE_WOR
#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>

#include <Switch.h>

#define BOOT_CONFIG           0x02
#define BOOT_STATE_RESET      0x02
#define BOOT_STATE_PRE_RESET  0x01
#define BOOT_STATE_NORMAL     0x00

// we use a Pro Mini
// Arduino pin for the LED
// D4 == PIN 4 on Pro Mini
#define LED_PIN 4

// Arduino pin for the config button
// B0 == PIN 8 on Pro Mini
#define CONFIG_BUTTON_PIN 8

#define RELAY1_PIN 17

// number of available peers per channel
#define PEERS_PER_CHANNEL 8

// all library classes are placed in the namespace 'as'
using namespace as;

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
    {0x42,0xA2,0xB7},       // Device ID
    "papa42a2b7",           // Device Serial
    {0x00,0x6c},            // Device Model
    0x10,                   // Firmware Version
    as::DeviceType::Switch, // Device Type
    {0x01,0x00}             // Info Bytes
};

/**
 * Configure the used hardware
 */
typedef AvrSPI<10,11,12,13> RadioSPI;
typedef AskSin<StatusLed<LED_PIN>,BattSensor<AsyncMeter<InternalVCC>>,Radio<RadioSPI,2> > Hal;

DEFREGISTER(Reg0,DREG_INTKEY,DREG_LEDMODE,MASTERID_REGS,DREG_LOWBATLIMIT)
class SwList0 : public RegList0<Reg0> {
public:
  SwList0(uint16_t addr) : RegList0<Reg0>(addr) {}
  void defaults () {
    clear();
    lowBatLimit(22);
  }
};

template <class DEVTYPE>
class ResetOnBoot : public Alarm {
  DEVTYPE& dev;
private:
  uint8_t cnt;
  uint8_t ms;
public:
  ResetOnBoot (DEVTYPE& d) : Alarm(0), dev(d), cnt(0), ms(200) { async(true); }
  virtual ~ResetOnBoot() {}

  void setBootState(uint8_t state) {
    StorageConfig sc = dev.getConfigArea();
    sc.setByte(BOOT_CONFIG, state);
    DPRINT(F("SETTING NEXT BOOT STATE    : "));DDECLN(state);
    sc.validate();
  }

  uint8_t getBootState() {
    StorageConfig sc = dev.getConfigArea();
    DPRINT(F("GETTING CURRENT BOOT STATE : "));DDECLN(sc.getByte(BOOT_CONFIG));
    return sc.getByte(BOOT_CONFIG);
  }

  virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
    if (cnt < (4000 / ms)) {
      cnt++;
      cnt % 2 == 0 ? dev.led().ledOn() : dev.led().ledOff();
      tick = millis2ticks(ms);
      clock.add(*this);
    } else {
      dev.led().ledOff();
      setBootState(BOOT_STATE_NORMAL);
    }
  }

  void init() {
    set(millis2ticks(ms));
    sysclock.add(*this);
    if (getBootState() == BOOT_STATE_RESET) {
      setBootState(BOOT_STATE_NORMAL);
      dev.reset();
    } else if (getBootState() == BOOT_STATE_PRE_RESET) {
      setBootState(BOOT_STATE_RESET);
      ms = 100;
    } else if (getBootState() == BOOT_STATE_NORMAL) {
      setBootState(BOOT_STATE_PRE_RESET);
    }
  }
};


// setup the device with channel type and number of channels
class SwitchType : public MultiChannelDevice<Hal,SwitchChannel<Hal,PEERS_PER_CHANNEL,SwList0>,1,SwList0> {
public:
  typedef MultiChannelDevice<Hal,SwitchChannel<Hal,PEERS_PER_CHANNEL,SwList0>,1,SwList0> DevType;
  SwitchType (const DeviceInfo& i,uint16_t addr) : DevType(i,addr) {}
  virtual ~SwitchType () {}

  virtual void configChanged () {
    DevType::configChanged();
    uint8_t lowbat = getList0().lowBatLimit();
    DDECLN(lowbat);
    if( lowbat > 0 ) {
      battery().low(lowbat);
    }
  }
};

Hal hal;
SwitchType sdev(devinfo,0x20);
ResetOnBoot<SwitchType> resetOnBoot(sdev);

ConfigToggleButton<SwitchType> cfgBtn(sdev);
#ifndef USE_WOR
BurstDetector<Hal> bd(hal);
#endif

void initPeerings (bool first) {
  // create internal peerings - CCU2 needs this
  if( first == true ) {
    HMID devid;
    sdev.getDeviceID(devid);
    for( uint8_t i=1; i<=sdev.channels(); ++i ) {
      Peer ipeer(devid,i);
      sdev.channel(i).peer(ipeer);
    }
  }
}

void setup () {
  DINIT(57600,ASKSIN_PLUS_PLUS_IDENTIFIER);
  bool first = sdev.init(hal);
  sdev.channel(1).init(RELAY1_PIN);
  buttonISR(cfgBtn,CONFIG_BUTTON_PIN);
  initPeerings(first);
#ifndef USE_WOR
  // start burst detection
  bd.enable(sysclock);
#endif
  // stay on for 15 seconds after start
  hal.activity.stayAwake(seconds2ticks(15));
  // measure battery every hour
  hal.battery.init(seconds2ticks(60UL*60),sysclock);
  resetOnBoot.init();
  sdev.initDone();
  //if (sdev.getMasterID() == HMID::broadcast) { DPRINTLN(F("START PAIRING")); sdev.startPairing(); } // start pairing of no master id is present
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if( worked == false && poll == false ) {
    hal.activity.savePower<Sleep<> >(hal);
  }
}
