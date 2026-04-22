#ifndef DATASTORAGE_H
#define DATASTORAGE_H

#include <Preferences.h>

/*
 * TieredHRStorage — three-tier circular ring buffer stored in NVS.
 *
 * T1 (5-min resolution, 24 h):  288 samples for HR and HRV
 * T2 (30-min resolution, 7 d):  336 samples for HR and HRV
 * T3 (2-h resolution,  30 d):  360 samples for HR only
 *
 * Promotion: every 6 T1 entries the mean is pushed to T2;
 *            every 4 T2 entries the mean is pushed to T3.
 *
 * Total NVS footprint: 1 608 bytes of blob data + 13 small keys
 * well within the default 24 KB NVS partition.
 */
class TieredHRStorage
{
public:
   static constexpr uint16_t T1_SIZE = 288; // 5-min, 24 h
   static constexpr uint16_t T2_SIZE = 336; // 30-min, 7 d
   static constexpr uint16_t T3_SIZE = 360; // 2-h, 30 d (HR only)

private:
   Preferences prefs;
   bool initialized;

   // T1 (5-min, 24 h)
   uint8_t t1HR[T1_SIZE];
   uint8_t t1HRV[T1_SIZE];
   uint8_t t1Sleep[T1_SIZE]; // 0=awake, 1=asleep, parallel to t1HR
   uint16_t t1Idx;           // next write position
   uint16_t t1Count;         // valid entries (max T1_SIZE)
   uint8_t t1PromoCount;     // T1 entries since last T2 promotion (0-5)

   // T2 (30-min, 7 d)
   uint8_t t2HR[T2_SIZE];
   uint8_t t2HRV[T2_SIZE];
   uint16_t t2Idx;
   uint16_t t2Count;
   uint8_t t2PromoCount; // T2 entries since last T3 promotion (0-3)

   // T3 (2-h, 30 d, HR only)
   uint8_t t3HR[T3_SIZE];
   uint16_t t3Idx;
   uint16_t t3Count;

public:
   TieredHRStorage()
       : initialized(false),
         t1Idx(0), t1Count(0), t1PromoCount(0),
         t2Idx(0), t2Count(0), t2PromoCount(0),
         t3Idx(0), t3Count(0)
   {
      memset(t1HR, 0, T1_SIZE);
      memset(t1HRV, 0, T1_SIZE);
      memset(t2HR, 0, T2_SIZE);
      memset(t2HRV, 0, T2_SIZE);
      memset(t3HR, 0, T3_SIZE);
   }

   bool begin()
   {
      if (!prefs.begin("trakk", false))
      {
         Serial.println("ERROR: Failed to open NVS namespace 'trakk'");
         return false;
      }

      // Load T1
      bool t1OK = (prefs.getBytes("hr5m", t1HR, T1_SIZE) == T1_SIZE &&
                   prefs.getBytes("hrv5m", t1HRV, T1_SIZE) == T1_SIZE);
      prefs.getBytes("slp5m", t1Sleep, T1_SIZE); // tolerate absence (first boot)
      t1Idx = prefs.getUShort("t1idx", 0);
      t1Count = prefs.getUShort("t1cnt", 0);
      t1PromoCount = prefs.getUChar("t1prom", 0);

      // Load T2
      bool t2OK = (prefs.getBytes("hr30m", t2HR, T2_SIZE) == T2_SIZE &&
                   prefs.getBytes("hrv30m", t2HRV, T2_SIZE) == T2_SIZE);
      t2Idx = prefs.getUShort("t2idx", 0);
      t2Count = prefs.getUShort("t2cnt", 0);
      t2PromoCount = prefs.getUChar("t2prom", 0);

      // Load T3
      bool t3OK = (prefs.getBytes("hr2h", t3HR, T3_SIZE) == T3_SIZE);
      t3Idx = prefs.getUShort("t3idx", 0);
      t3Count = prefs.getUShort("t3cnt", 0);

      // Validate; reset corrupted tiers
      if (!t1OK || t1Idx >= T1_SIZE || t1Count > T1_SIZE)
      {
         Serial.println("Initializing T1 (5-min) buffer");
         memset(t1HR, 0, T1_SIZE);
         memset(t1HRV, 0, T1_SIZE);
         memset(t1Sleep, 0, T1_SIZE);
         t1Idx = 0;
         t1Count = 0;
         t1PromoCount = 0;
      }
      if (!t2OK || t2Idx >= T2_SIZE || t2Count > T2_SIZE)
      {
         Serial.println("Initializing T2 (30-min) buffer");
         memset(t2HR, 0, T2_SIZE);
         memset(t2HRV, 0, T2_SIZE);
         t2Idx = 0;
         t2Count = 0;
         t2PromoCount = 0;
      }
      if (!t3OK || t3Idx >= T3_SIZE || t3Count > T3_SIZE)
      {
         Serial.println("Initializing T3 (2-h) buffer");
         memset(t3HR, 0, T3_SIZE);
         t3Idx = 0;
         t3Count = 0;
      }

      Serial.printf("TieredHRStorage: T1=%d/288, T2=%d/336, T3=%d/360\n",
                    t1Count, t2Count, t3Count);
      initialized = true;
      return true;
   }

   // Store a 5-min measurement. hr: BPM (0=no reading), hrv: SDNN ms clamped to uint8_t.
   // Automatically promotes averaged values to T2 (every 6 calls) and T3 (every 4 T2 entries).
   // Call addSleepState() immediately after addMeasurement() to keep indices aligned.
   void addMeasurement(uint8_t hr, uint8_t hrv)
   {
      if (!initialized)
         return;

      // --- T1 write ---
      t1HR[t1Idx] = hr;
      t1HRV[t1Idx] = hrv;
      // t1Sleep will be updated by addSleepState() BEFORE t1Idx advances,
      // so we leave it unchanged here; addSleepState() writes to current t1Idx.
      uint16_t writtenIdx = t1Idx; // capture before advance
      t1Idx = (t1Idx + 1) % T1_SIZE;
      if (t1Count < T1_SIZE)
         t1Count++;
      t1PromoCount++;
      (void)writtenIdx; // used logically; sleep state written separately

      // --- Promote to T2 every 6 T1 entries (= 30-min interval) ---
      if (t1PromoCount >= 6)
      {
         t1PromoCount = 0;
         uint16_t sumHR = 0, sumHRV = 0;
         uint8_t n = 0;
         for (uint8_t i = 0; i < 6; i++)
         {
            uint16_t pos = (t1Idx - 6u + i + T1_SIZE) % T1_SIZE;
            if (t1HR[pos] > 0)
            {
               sumHR += t1HR[pos];
               sumHRV += t1HRV[pos];
               n++;
            }
         }
         t2HR[t2Idx] = (n > 0) ? (uint8_t)(sumHR / n) : 0;
         t2HRV[t2Idx] = (n > 0) ? (uint8_t)(sumHRV / n) : 0;
         t2Idx = (t2Idx + 1) % T2_SIZE;
         if (t2Count < T2_SIZE)
            t2Count++;
         t2PromoCount++;

         // --- Promote to T3 every 4 T2 entries (= 2-h interval) ---
         if (t2PromoCount >= 4)
         {
            t2PromoCount = 0;
            uint16_t sumHR3 = 0;
            uint8_t n3 = 0;
            for (uint8_t i = 0; i < 4; i++)
            {
               uint16_t pos = (t2Idx - 4u + i + T2_SIZE) % T2_SIZE;
               if (t2HR[pos] > 0)
               {
                  sumHR3 += t2HR[pos];
                  n3++;
               }
            }
            t3HR[t3Idx] = (n3 > 0) ? (uint8_t)(sumHR3 / n3) : 0;
            t3Idx = (t3Idx + 1) % T3_SIZE;
            if (t3Count < T3_SIZE)
               t3Count++;
         }
      }

      save();
   }

   // Returns the last 'n' samples (chronological: oldest → newest).
   // tier: 1=T1(5 min), 2=T2(30 min), 3=T3(2 h)
   // isHRV: false=HR bpm, true=HRV SDNN ms
   // Returns actual number of samples written to buf.
   uint16_t getLastN(uint8_t tier, bool isHRV, uint8_t *buf, uint16_t n)
   {
      if (!initialized || buf == nullptr)
         return 0;
      uint8_t *src = nullptr;
      uint16_t size = 0, idx = 0, count = 0;

      if (tier == 1)
      {
         src = isHRV ? t1HRV : t1HR;
         size = T1_SIZE;
         idx = t1Idx;
         count = t1Count;
      }
      else if (tier == 2)
      {
         src = isHRV ? t2HRV : t2HR;
         size = T2_SIZE;
         idx = t2Idx;
         count = t2Count;
      }
      else if (tier == 3)
      {
         src = t3HR;
         size = T3_SIZE;
         idx = t3Idx;
         count = t3Count;
      }
      else
      {
         return 0;
      }

      uint16_t actual = (n < count) ? n : count;
      for (uint16_t i = 0; i < actual; i++)
      {
         buf[i] = src[(idx - actual + i + size) % size];
      }
      return actual;
   }

   // Convenience: return all available samples for a tier.
   uint16_t getAll(uint8_t tier, bool isHRV, uint8_t *buf)
   {
      uint16_t maxN = (tier == 1) ? T1_SIZE : (tier == 2) ? T2_SIZE
                                                          : T3_SIZE;
      return getLastN(tier, isHRV, buf, maxN);
   }

   // Write sleep state (SLEEP_STATE_AWAKE/ASLEEP) for the measurement just stored.
   // Must be called AFTER addMeasurement() to fill the same slot.
   void addSleepState(uint8_t state)
   {
      if (!initialized)
         return;
      // t1Idx already advanced by addMeasurement(); the slot we just wrote is (t1Idx-1)
      uint16_t lastIdx = (t1Idx == 0) ? (T1_SIZE - 1) : (t1Idx - 1);
      t1Sleep[lastIdx] = state;
      prefs.putBytes("slp5m", t1Sleep, T1_SIZE);
   }

   // Return the last 'n' T1 sleep-state entries in chronological order.
   uint16_t getLastNSleep(uint8_t *buf, uint16_t n)
   {
      if (!initialized || buf == nullptr)
         return 0;
      uint16_t actual = (n < t1Count) ? n : t1Count;
      for (uint16_t i = 0; i < actual; i++)
      {
         buf[i] = t1Sleep[(t1Idx - actual + i + T1_SIZE) % T1_SIZE];
      }
      return actual;
   }

   // T1 fill count — used for status/debug output.
   uint16_t getCount() const { return t1Count; }

   void clear()
   {
      memset(t1HR, 0, T1_SIZE);
      memset(t1HRV, 0, T1_SIZE);
      memset(t1Sleep, 0, T1_SIZE);
      memset(t2HR, 0, T2_SIZE);
      memset(t2HRV, 0, T2_SIZE);
      memset(t3HR, 0, T3_SIZE);
      t1Idx = 0;
      t1Count = 0;
      t1PromoCount = 0;
      t2Idx = 0;
      t2Count = 0;
      t2PromoCount = 0;
      t3Idx = 0;
      t3Count = 0;
      save();
      Serial.println("TieredHRStorage cleared");
   }

private:
   void save()
   {
      if (!initialized)
         return;
      prefs.putBytes("hr5m", t1HR, T1_SIZE);
      prefs.putBytes("hrv5m", t1HRV, T1_SIZE);
      prefs.putBytes("slp5m", t1Sleep, T1_SIZE);
      prefs.putUShort("t1idx", t1Idx);
      prefs.putUShort("t1cnt", t1Count);
      prefs.putUChar("t1prom", t1PromoCount);
      prefs.putBytes("hr30m", t2HR, T2_SIZE);
      prefs.putBytes("hrv30m", t2HRV, T2_SIZE);
      prefs.putUShort("t2idx", t2Idx);
      prefs.putUShort("t2cnt", t2Count);
      prefs.putUChar("t2prom", t2PromoCount);
      prefs.putBytes("hr2h", t3HR, T3_SIZE);
      prefs.putUShort("t3idx", t3Idx);
      prefs.putUShort("t3cnt", t3Count);
   }
};

#endif // DATASTORAGE_H
