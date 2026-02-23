#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>

// Display pin definitions (from demo_epaper.cpp)
#define DISPLAY_CS_PIN D9
#define DISPLAY_DC_PIN D1
#define DISPLAY_RES_PIN D2
#define DISPLAY_BUSY_PIN D6
#define DISPLAY_POWER_PIN 8

// 1.54" EPD Module (200x200 pixels)
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(DISPLAY_CS_PIN, DISPLAY_DC_PIN, DISPLAY_RES_PIN, DISPLAY_BUSY_PIN)
);

// Initialize display
bool initDisplay() {
    Serial.println("Initializing e-paper display...");
    
    // Power control for display
    pinMode(DISPLAY_POWER_PIN, OUTPUT);
    digitalWrite(DISPLAY_POWER_PIN, HIGH);
    delay(100);
    
    display.init(115200, true, 50, false);
    display.setRotation(1);  // Landscape orientation
    
    Serial.println("Display initialized (200x200)");
    return true;
}

// Draw battery icon with charge level indicator
void drawBatteryIcon(int16_t x, int16_t y, float voltage) {
    // Battery outline (20x10 pixels)
    display.drawRect(x, y, 20, 10, GxEPD_BLACK);
    display.fillRect(x + 20, y + 3, 2, 4, GxEPD_BLACK);  // Terminal
    
    // Calculate fill based on voltage (3.6V to 4.2V range)
    float percentage = (voltage - 3.6) / (4.2 - 3.6);
    if (percentage > 1.0) percentage = 1.0;
    if (percentage < 0.0) percentage = 0.0;
    
    int16_t fillWidth = (int16_t)(16 * percentage);
    if (fillWidth > 0) {
        display.fillRect(x + 2, y + 2, fillWidth, 6, GxEPD_BLACK);
    }
}

// Render dashboard screen with HR and battery
void renderDashboard(uint8_t hr, float voltage, bool buildingHistory) {
    Serial.println("Rendering DASHBOARD...");
    
    // Use partial refresh for dashboard (faster)
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Title
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(10, 20);
        display.print("TrakkWatch");
        
        // Battery indicator (top right)
        drawBatteryIcon(160, 5, voltage);
        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(130, 20);
        display.printf("%.2fV", voltage);
        
        // Heart rate - large centered display
        display.setFont(&FreeMonoBold18pt7b);
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        
        if (hr > 0) {
            char hrText[8];
            sprintf(hrText, "%d", hr);
            display.getTextBounds(hrText, 0, 0, &tbx, &tby, &tbw, &tbh);
            uint16_t x = ((display.width() - tbw) / 2) - tbx;
            display.setCursor(x, 100);
            display.print(hrText);
            
            // BPM label
            display.setFont(&FreeMonoBold12pt7b);
            display.getTextBounds("BPM", 0, 0, &tbx, &tby, &tbw, &tbh);
            x = ((display.width() - tbw) / 2) - tbx;
            display.setCursor(x, 130);
            display.print("BPM");
            
            // Heart icon (simple)
            display.fillCircle(70, 85, 5, GxEPD_BLACK);
            display.fillCircle(80, 85, 5, GxEPD_BLACK);
            display.fillTriangle(65, 87, 85, 87, 75, 100, GxEPD_BLACK);
        } else {
            // No reading available
            display.setFont(&FreeMonoBold12pt7b);
            display.getTextBounds("--", 0, 0, &tbx, &tby, &tbw, &tbh);
            uint16_t x = ((display.width() - tbw) / 2) - tbx;
            display.setCursor(x, 100);
            display.print("--");
            
            display.setFont(&FreeMonoBold9pt7b);
            display.getTextBounds("No reading", 0, 0, &tbx, &tby, &tbw, &tbh);
            x = ((display.width() - tbw) / 2) - tbx;
            display.setCursor(x, 125);
            display.print("No reading");
        }
        
        // Building history message
        if (buildingHistory) {
            display.setFont(&FreeMonoBold9pt7b);
            display.getTextBounds("Building history...", 0, 0, &tbx, &tby, &tbw, &tbh);
            uint16_t x = ((display.width() - tbw) / 2) - tbx;
            display.setCursor(x, 160);
            display.print("Building history...");
        }
        
        // Instructions at bottom
        display.setFont(&FreeMonoBold9pt7b);
        display.getTextBounds("Tap: graph->sleep->dash", 0, 0, &tbx, &tby, &tbw, &tbh);
        uint16_t x = ((display.width() - tbw) / 2) - tbx;
        display.setCursor(x, 190);
        display.print("Tap: graph->sleep->dash");
        
    } while (display.nextPage());
    
    Serial.println("Dashboard rendered");
}

// Render 4-hour heart rate graph
void renderGraph(uint8_t* hrData, uint8_t count) {
    Serial.println("Rendering GRAPH...");
    
    // Use full refresh for complex graph rendering
    display.setFullWindow();
    display.firstPage();
    
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Title
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(10, 15);
        display.print("4-Hour History");
        
        // Graph area: left margin 30, right margin 10, top margin 25, bottom margin 30
        const int16_t graphLeft = 30;
        const int16_t graphRight = 190;
        const int16_t graphTop = 25;
        const int16_t graphBottom = 170;
        const int16_t graphWidth = graphRight - graphLeft;
        const int16_t graphHeight = graphBottom - graphTop;
        
        // Y-axis range: 40-180 BPM
        const uint8_t minBPM = 40;
        const uint8_t maxBPM = 180;
        const uint8_t bpmRange = maxBPM - minBPM;
        
        // Draw axes
        display.drawLine(graphLeft, graphTop, graphLeft, graphBottom, GxEPD_BLACK);
        display.drawLine(graphLeft, graphBottom, graphRight, graphBottom, GxEPD_BLACK);
        
        // Draw horizontal gridlines every 20 BPM
        display.setFont(0);  // Small font for labels
        for (uint8_t bpm = 60; bpm <= 160; bpm += 20) {
            int16_t y = graphBottom - ((bpm - minBPM) * graphHeight / bpmRange);
            display.drawLine(graphLeft - 2, y, graphRight, y, GxEPD_BLACK);
            display.setCursor(2, y - 3);
            display.print(bpm);
        }
        
        // X-axis labels
        display.setCursor(graphLeft - 5, graphBottom + 10);
        display.print("4h");
        display.setCursor(graphRight - 15, graphBottom + 10);
        display.print("Now");
        
        // Plot data points
        if (count > 0) {
            int16_t lastX = -1, lastY = -1;
            
            for (uint8_t i = 0; i < count; i++) {
                uint8_t hr = hrData[i];
                
                // Skip zero values (no data)
                if (hr == 0) continue;
                
                // Clamp to valid range
                if (hr < minBPM) hr = minBPM;
                if (hr > maxBPM) hr = maxBPM;
                
                // Calculate position
                int16_t x = graphLeft + (i * graphWidth / (count - 1));
                int16_t y = graphBottom - ((hr - minBPM) * graphHeight / bpmRange);
                
                // Draw point
                display.fillCircle(x, y, 2, GxEPD_BLACK);
                
                // Connect with line to previous point
                if (lastX >= 0 && lastY >= 0) {
                    display.drawLine(lastX, lastY, x, y, GxEPD_BLACK);
                }
                
                lastX = x;
                lastY = y;
            }
        } else {
            // No data message
            display.setFont(&FreeMonoBold9pt7b);
            int16_t tbx, tby;
            uint16_t tbw, tbh;
            display.getTextBounds("No data", 0, 0, &tbx, &tby, &tbw, &tbh);
            uint16_t x = ((display.width() - tbw) / 2) - tbx;
            display.setCursor(x, 100);
            display.print("No data");
        }
        
        // Instructions at bottom
        display.setFont(&FreeMonoBold9pt7b);
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds("Double-tap: next", 0, 0, &tbx, &tby, &tbw, &tbh);
        uint16_t x = ((display.width() - tbw) / 2) - tbx;
        display.setCursor(x, 195);
        display.print("Double-tap: next");
        
    } while (display.nextPage());
    
    Serial.println("Graph rendered");
}

// Render sleep summary screen with placeholder metrics
void renderSleepSummary() {
    Serial.println("Rendering SLEEP SUMMARY...");

    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // ---- Title ----
        display.setFont(&FreeMonoBold12pt7b);
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds("Sleep Summary", 0, 0, &tbx, &tby, &tbw, &tbh);
        uint16_t cx = ((display.width() - tbw) / 2) - tbx;
        display.setCursor(cx, 22);
        display.print("Sleep Summary");

        // Divider line below title
        display.drawLine(5, 30, display.width() - 5, 30, GxEPD_BLACK);

        // ---- Metrics (two-column: label left, value right) ----
        display.setFont(&FreeMonoBold9pt7b);
        const int16_t leftX  = 8;
        const int16_t rightX = 110;
        const int16_t rowH   = 30;    // row spacing
        int16_t       rowY   = 56;    // first row baseline

        // Row 1: Total sleep
        display.setCursor(leftX, rowY);
        display.print("Total Sleep");
        display.setCursor(rightX, rowY);
        display.print("--h --m");
        rowY += rowH;

        // Row 2: Deep sleep
        display.setCursor(leftX, rowY);
        display.print("Deep Sleep");
        display.setCursor(rightX, rowY);
        display.print("--h --m");
        rowY += rowH;

        // Row 3: Light sleep
        display.setCursor(leftX, rowY);
        display.print("Light Sleep");
        display.setCursor(rightX, rowY);
        display.print("--h --m");
        rowY += rowH;

        // Row 4: Sleep efficiency
        display.setCursor(leftX, rowY);
        display.print("Efficiency");
        display.setCursor(rightX, rowY);
        display.print("--%");

        // Divider above footer
        display.drawLine(5, 172, display.width() - 5, 172, GxEPD_BLACK);

        // ---- Footer hint ----
        display.setFont(&FreeMonoBold9pt7b);
        display.getTextBounds("Double-tap: next", 0, 0, &tbx, &tby, &tbw, &tbh);
        cx = ((display.width() - tbw) / 2) - tbx;
        display.setCursor(cx, 192);
        display.print("Double-tap: next");

    } while (display.nextPage());

    Serial.println("Sleep summary rendered");
}

// Put display in low power hibernate mode
void hibernateDisplay() {
    display.hibernate();
    Serial.println("Display hibernated");
}

#endif // DISPLAYMANAGER_H
