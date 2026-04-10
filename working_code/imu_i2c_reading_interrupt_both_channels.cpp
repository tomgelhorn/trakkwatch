#include <Wire.h>
#include "Arduino.h"
#include "SparkFun_BMA400_Arduino_Library.h"

// Create a new sensor object
BMA400 accelerometer;

// I2C address selection
uint8_t i2cAddress = BMA400_I2C_ADDRESS_DEFAULT; // 0x14
//uint8_t i2cAddress = BMA400_I2C_ADDRESS_SECONDARY; // 0x15

#if defined(D7)
const int interruptPinCh1 = D6;
#else
const int interruptPinCh1 = D6;
#endif

#if defined(D6)
const int interruptPinCh2 = D7;
#else
const int interruptPinCh2 = D7;
#endif

volatile bool interruptCh1Occurred = false;
volatile bool interruptCh2Occurred = false;

void bma400InterruptHandlerCh1()
{
    interruptCh1Occurred = true;
}

void bma400InterruptHandlerCh2()
{
    interruptCh2Occurred = true;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("BMA400 - Basic Readings + Dual Interrupt Channels");

    pinMode(interruptPinCh1, INPUT);
    pinMode(interruptPinCh2, INPUT);

    Wire.begin();

    while (accelerometer.beginI2C(i2cAddress) != BMA400_OK)
    {
        Serial.println("Error: BMA400 not connected, check wiring and I2C address!");
        delay(1000);
    }

    Serial.println("BMA400 connected!");

    // Keep tap-friendly ODR and range.
    accelerometer.setODR(BMA400_ODR_200HZ);
    accelerometer.setRange(BMA400_RANGE_16G);

    // Map data-ready interrupt to INT2 for basic acceleration readings.
    accelerometer.setDRDYInterruptChannel(BMA400_INT_CHANNEL_2);

    // Map tap interrupts to INT1.
    bma400_tap_conf tapConfig =
    {
        .axes_sel = BMA400_TAP_Z_AXIS_EN,
        .sensitivity = BMA400_TAP_SENSITIVITY_0,
        .tics_th = BMA400_TICS_TH_18_DATA_SAMPLES,
        .quiet = BMA400_QUIET_60_DATA_SAMPLES,
        .quiet_dt = BMA400_QUIET_DT_4_DATA_SAMPLES,
        .int_chan = BMA400_INT_CHANNEL_1
    };
    accelerometer.setTapInterrupt(&tapConfig);

    accelerometer.setInterruptPinMode(BMA400_INT_CHANNEL_1, BMA400_INT_PUSH_PULL_ACTIVE_1);
    accelerometer.setInterruptPinMode(BMA400_INT_CHANNEL_2, BMA400_INT_PUSH_PULL_ACTIVE_1);

    accelerometer.enableInterrupt(BMA400_DRDY_INT_EN, true);
    accelerometer.enableInterrupt(BMA400_SINGLE_TAP_INT_EN, true);
    accelerometer.enableInterrupt(BMA400_DOUBLE_TAP_INT_EN, true);

    attachInterrupt(digitalPinToInterrupt(interruptPinCh1), bma400InterruptHandlerCh1, RISING);
    attachInterrupt(digitalPinToInterrupt(interruptPinCh2), bma400InterruptHandlerCh2, RISING);

    Serial.print("INT1 pin (tap): ");
    Serial.println(interruptPinCh1);
    Serial.print("INT2 pin (DRDY): ");
    Serial.println(interruptPinCh2);
}

void loop()
{
    if (!interruptCh1Occurred && !interruptCh2Occurred)
    {
        return;
    }

    bool handledCh1 = interruptCh1Occurred;
    bool handledCh2 = interruptCh2Occurred;
    interruptCh1Occurred = false;
    interruptCh2Occurred = false;

    uint16_t interruptStatus = 0;
    accelerometer.getInterruptStatus(&interruptStatus);

    if (handledCh2 && (interruptStatus & BMA400_ASSERTED_DRDY_INT))
    {
        accelerometer.getSensorData();

        Serial.print("DRDY -> X: ");
        Serial.print(accelerometer.data.accelX, 3);
        Serial.print("\tY: ");
        Serial.print(accelerometer.data.accelY, 3);
        Serial.print("\tZ: ");
        Serial.println(accelerometer.data.accelZ, 3);
    }

    if (handledCh1)
    {
        if (interruptStatus & BMA400_ASSERTED_S_TAP_INT)
        {
            Serial.println("INT1 -> Single tap");
        }
        else if (interruptStatus & BMA400_ASSERTED_D_TAP_INT)
        {
            Serial.println("INT1 -> Double tap");
        }
    }
}