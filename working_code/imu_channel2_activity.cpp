// BMA400 - Channel 2 GEN2 No-Motion Interrupt Test
// Board: Seeed XIAO ESP32C3
// Mirrors the initMotionInterrupt() pattern from src/Sensors.h
// Wire BMA400 INT2 pin to D7

#include <Wire.h>
#include "SparkFun_BMA400_Arduino_Library.h"

BMA400 accelerometer;

// INT2 pin — same as IMU_INT2_PIN in Sensors.h
const int interruptPin = D7;

// GEN2 no-motion thresholds — same as Sensors.h
#define NO_MOTION_THRESHOLD_MG 10 // 1 LSB = 8 mg  →  ~80 mg stillness threshold
#define NO_MOTION_DURATION 100    // samples at FILT1 (100 Hz) = 1 second of no-motion

volatile bool noMotionFlag = false;

void IRAM_ATTR noMotionISRHandler()
{
   noMotionFlag = true;
}

void setup()
{
   Serial.begin(115200);
   Serial.println("BMA400 - Channel 2 GEN2 No-Motion Interrupt Test");

   Wire.begin();

   while (accelerometer.beginI2C() != BMA400_OK)
   {
      Serial.println("Error: BMA400 not connected, check wiring and I2C address!");
      delay(1000);
   }

   Serial.println("BMA400 connected!");

   accelerometer.setODR(BMA400_ODR_200HZ);
   accelerometer.setRange(BMA400_RANGE_16G);

   // Configure GEN2 interrupt for no-motion detection on INT_CHANNEL_2
   // (mirrors initMotionInterrupt() in src/Sensors.h exactly)
   bma400_gen_int_conf gen2;
   gen2.gen_int_thres = NO_MOTION_THRESHOLD_MG;
   gen2.gen_int_dur = NO_MOTION_DURATION;
   gen2.axes_sel = BMA400_AXIS_XYZ_EN;
   gen2.data_src = BMA400_DATA_SRC_ACC_FILT1;
   gen2.criterion_sel = BMA400_INACTIVITY_INT; // fire when BELOW threshold
   gen2.evaluate_axes = BMA400_ALL_AXES_INT;   // all axes must be still
   gen2.ref_update = BMA400_UPDATE_EVERY_TIME; // auto-update reference
   gen2.hysteresis = BMA400_HYST_0_MG;
   gen2.int_thres_ref_x = 0;
   gen2.int_thres_ref_y = 0;
   gen2.int_thres_ref_z = 0;
   gen2.int_chan = BMA400_INT_CHANNEL_2;

   if (accelerometer.setGeneric2Interrupt(&gen2) != BMA400_OK)
   {
      Serial.println("ERROR: Failed to configure GEN2 no-motion interrupt");
      while (true)
         delay(1000);
   }

   accelerometer.setInterruptPinMode(BMA400_INT_CHANNEL_2, BMA400_INT_PUSH_PULL_ACTIVE_1);
   accelerometer.enableInterrupt(BMA400_GEN2_INT_EN, true);

   pinMode(interruptPin, INPUT);
   attachInterrupt(digitalPinToInterrupt(interruptPin), noMotionISRHandler, RISING);

   Serial.println("GEN2 no-motion interrupt configured. Move then hold still to trigger.");
}

void loop()
{
   if (noMotionFlag)
   {
      noInterrupts();
      noMotionFlag = false;
      interrupts();

      uint16_t interruptStatus = 0;
      accelerometer.getInterruptStatus(&interruptStatus);

      if (interruptStatus & BMA400_ASSERTED_GEN2_INT)
      {
         accelerometer.getSensorData();
         Serial.print("No-motion detected! Accel (g) -> X: ");
         Serial.print(accelerometer.data.accelX, 3);
         Serial.print("  Y: ");
         Serial.print(accelerometer.data.accelY, 3);
         Serial.print("  Z: ");
         Serial.println(accelerometer.data.accelZ, 3);
      }
      else
      {
         Serial.println("INT2 fired but GEN2 bit not set in status.");
      }
   }
}
