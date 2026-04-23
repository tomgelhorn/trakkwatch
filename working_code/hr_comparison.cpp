/*
 * HR Algorithm Comparison: Custom DSP (Sensors.h) vs SparkFun PBA
 *
 * Runde 1 – SparkFun PBA (heartRate.h): 60 s Echtzeit-Erkennung via checkForBeat()
 * Runde 2 – Custom DSP  (Sensors.h):    measureHeartRate() – Puffer + Bandpass + Peaks
 *
 * Beide Messungen dauern je 60 Sekunden.
 */

#include "Sensors.h"   // measureHeartRate(), initHeartRateSensor(), particleSensor, ...
#include "heartRate.h" // SparkFun PBA: checkForBeat()

#define DURATION_MS 60000UL

// -------------------------------------------------------------------------
// PBA-Zustand
// -------------------------------------------------------------------------
static const byte RATE_SIZE = 8;
static byte pbaRates[RATE_SIZE];
static byte pbaRateSpot = 0;
static long pbaLastBeat = 0;
static float pbaInstant = 0.0f;
static int pbaAvg = 0;

// -------------------------------------------------------------------------
// Runde 1: SparkFun PBA für DURATION_MS
// -------------------------------------------------------------------------
static int runPBA()
{
   memset(pbaRates, 0, sizeof(pbaRates));
   pbaRateSpot = 0;
   pbaLastBeat = 0;
   pbaInstant = 0.0f;
   pbaAvg = 0;

   Serial.println("\n--- Runde 1: SparkFun PBA ---");
   Serial.println("Handgelenk anlegen...");

   uint32_t startTime = millis();
   uint32_t lastPrint = startTime;

   while ((millis() - startTime) < DURATION_MS)
   {
      long irValue = particleSensor.getIR();

      if (checkForBeat(irValue))
      {
         long delta = millis() - pbaLastBeat;
         pbaLastBeat = millis();
         pbaInstant = 60.0f / (delta / 1000.0f);

         if (pbaInstant >= MIN_BPM && pbaInstant <= MAX_BPM)
         {
            pbaRates[pbaRateSpot++] = (byte)pbaInstant;
            pbaRateSpot %= RATE_SIZE;

            pbaAvg = 0;
            for (byte x = 0; x < RATE_SIZE; x++)
               pbaAvg += pbaRates[x];
            pbaAvg /= RATE_SIZE;
         }
      }

      if (millis() - lastPrint >= 5000)
      {
         int elapsed = (millis() - startTime) / 1000;
         Serial.printf("[%2ds] IR=%6ld  sofort=%.1f  avg=%d%s\n",
                       elapsed, irValue, pbaInstant, pbaAvg,
                       irValue < IR_WRIST_THRESHOLD ? "  (kein Handgelenk)" : "");
         lastPrint = millis();
      }

      delay(1);
   }

   Serial.printf("PBA fertig: avg=%d BPM\n", pbaAvg);
   return pbaAvg;
}

// -------------------------------------------------------------------------
// setup(): beide Runden + Ergebnisausgabe
// -------------------------------------------------------------------------
void setup()
{
   Serial.begin(115200);
   delay(500);
   Serial.println("\n=== HR Algorithmus-Vergleich ===");
   Serial.printf("Messdauer je Runde: %lu Sekunden\n", DURATION_MS / 1000);

   if (!initHeartRateSensor())
   {
      Serial.println("FEHLER: Sensor-Init fehlgeschlagen!");
      while (1)
         delay(1000);
   }

   Serial.println("Sensor bereit. Messung startet in 3 Sekunden...");
   delay(3000);

   // Runde 1: PBA
   int pbaBPM = runPBA();

   Serial.println("\nKurze Pause (2 s) vor Runde 2...");
   delay(2000);

   // Runde 2: Custom DSP via measureHeartRate() aus Sensors.h
   Serial.println("\n--- Runde 2: Custom DSP (Sensors.h) ---");
   HRVResult dspResult = measureHeartRate(DURATION_MS);

   // -----------------------------------------------------------------------
   // Ergebnis
   // -----------------------------------------------------------------------
   Serial.println("\n╔══════════════════════════════════════╗");
   Serial.println("║     ERGEBNIS (je 60 Sekunden)        ║");
   Serial.println("╠══════════════════════════════════════╣");
   Serial.printf("║  SparkFun PBA (Rolling Avg):  %3d BPM ║\n", pbaBPM);
   Serial.printf("║  Custom DSP   (Bandpass+FFT): %3d BPM ║\n",
                 dspResult.valid ? dspResult.bpm : 0);
   if (dspResult.valid)
      Serial.printf("║  Custom SDRR:                %4d ms  ║\n", dspResult.sdrr_ms);
   Serial.println("╚══════════════════════════════════════╝");

   if (pbaBPM > 0 && dspResult.valid)
      Serial.printf("Differenz: %d BPM\n", abs((int)pbaBPM - (int)dspResult.bpm));
}

void loop()
{
   delay(10000);
}
