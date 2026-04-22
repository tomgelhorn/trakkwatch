import serial
import time
import csv
from datetime import datetime
import threading
import sys

class HRVDataLogger:
    def __init__(self, port='COM12', baudrate=115200):
        """
        Initialisiert den HRV Data Logger
        
        Args:
            port: Serieller Port (z.B. 'COM3' für Windows, '/dev/ttyUSB0' für Linux)
            baudrate: Baudrate (muss mit Arduino übereinstimmen)
        """
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.data_buffer = []
        self.is_recording = False
        self.start_time = None
        
    def connect(self):
        """Verbindet mit dem seriellen Port"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(2)  # Arduino braucht Zeit zum Initialisieren
            print(f"Verbunden mit {self.port} @ {self.baudrate} baud")
            return True
        except serial.SerialException as e:
            print(f"Fehler beim Verbinden: {e}")
            return False
    
    def start_recording(self):
        """Startet die Aufzeichnung"""
        self.is_recording = True
        self.data_buffer = []
        self.start_time = time.perf_counter()
        print("\n=== AUFZEICHNUNG GESTARTET ===")
        print("Drücke 's' um zu speichern, 'q' zum Beenden\n")
    
    def stop_recording(self):
        """Stoppt die Aufzeichnung"""
        self.is_recording = False
        print("\n=== AUFZEICHNUNG GESTOPPT ===")
    
    def read_data(self):
        """Liest kontinuierlich Daten vom seriellen Port"""
        while True:
            if self.ser and self.ser.in_waiting > 0:
                try:
                    line = self.ser.readline().decode('utf-8').strip()
                    
                    # Zeitstempel mit Nanosekunden-Präzision
                    timestamp_abs = time.time()  # Absolute Zeit (Unix timestamp)
                    timestamp_rel = time.perf_counter() - self.start_time if self.start_time else 0
                    
                    # Versuche den Wert zu parsen
                    try:
                        value = float(line)
                        
                        if self.is_recording:
                            self.data_buffer.append({
                                'timestamp_ms': timestamp_rel * 1000,  # Millisekunden relativ zum Start
                                'timestamp_abs': timestamp_abs,
                                'ir_value': value
                            })
                            
                        # Live-Anzeige (alle 10 Samples)
                        if len(self.data_buffer) % 10 == 0:
                            print(f"Samples: {len(self.data_buffer)} | "
                                  f"Zeit: {timestamp_rel:.3f}s | "
                                  f"IR: {value:.0f}", end='\r')
                    
                    except ValueError:
                        # Nicht-numerische Ausgaben (z.B. Debug-Messages)
                        if not line.startswith("Initializing"):
                            print(f"Info: {line}")
                
                except UnicodeDecodeError:
                    pass
            
            time.sleep(0.001)  # Kleine Pause um CPU zu schonen
    
    def save_to_csv(self, filename=None):
        """Speichert die Daten in eine CSV-Datei"""
        if not self.data_buffer:
            print("Keine Daten zum Speichern vorhanden!")
            return
        
        if filename is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"hrv_data_{timestamp}.csv"
        
        try:
            with open(filename, 'w', newline='') as csvfile:
                fieldnames = ['timestamp_ms', 'timestamp_abs', 'ir_value']
                writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
                
                writer.writeheader()
                writer.writerows(self.data_buffer)
            
            duration = self.data_buffer[-1]['timestamp_ms'] / 1000
            print(f"\n✓ {len(self.data_buffer)} Samples gespeichert in: {filename}")
            print(f"  Aufnahmedauer: {duration:.2f} Sekunden")
            print(f"  Sample-Rate: {len(self.data_buffer)/duration:.1f} Hz")
            
        except Exception as e:
            print(f"Fehler beim Speichern: {e}")
    
    def keyboard_listener(self):
        """Hört auf Tastatureingaben"""
        print("\n=== HRV Data Logger ===")
        print("Befehle:")
        print("  'r' - Start Recording")
        print("  's' - Stop & Save")
        print("  'q' - Quit")
        print("  'c' - Clear buffer\n")
        
        while True:
            try:
                cmd = input().strip().lower()
                
                if cmd == 'r':
                    self.start_recording()
                
                elif cmd == 's':
                    self.stop_recording()
                    self.save_to_csv()
                
                elif cmd == 'c':
                    self.data_buffer = []
                    print("Buffer geleert")
                
                elif cmd == 'q':
                    print("\nBeende...")
                    if self.is_recording and self.data_buffer:
                        save = input("Daten vor Beenden speichern? (j/n): ")
                        if save.lower() == 'j':
                            self.save_to_csv()
                    sys.exit(0)
                    
            except KeyboardInterrupt:
                print("\nBeende...")
                sys.exit(0)
    
    def run(self):
        """Hauptprogramm"""
        if not self.connect():
            return
        
        # Starte Daten-Leser in separatem Thread
        reader_thread = threading.Thread(target=self.read_data, daemon=True)
        reader_thread.start()
        
        # Hauptthread für Tastatureingaben
        self.keyboard_listener()
        
        # Cleanup
        if self.ser:
            self.ser.close()


if __name__ == "__main__":
    # ANPASSEN: Deinen seriellen Port hier eintragen
    # Windows: 'COM3', 'COM4', etc.
    # Linux/Mac: '/dev/ttyUSB0', '/dev/ttyACM0', etc.
    
    logger = HRVDataLogger(port='COM15', baudrate=115200)
    logger.run()