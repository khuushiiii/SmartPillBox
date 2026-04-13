**Smart Pill Box — Adaptive Reminder System**

An IoT-based Smart Pill Box that improves medication adherence using dual confirmation (IR + Weight sensing) and adaptive reminders.

**Features**
✅ Dual Confirmation System
IR Sensor → Detects hand presence
Load Cell → Confirms pill removal
✅ Adaptive Reminder Algorithm
Learns user behavior
Adjusts reminder timing automatically
✅ Real-Time Monitoring
Firebase Realtime Database integration
Live dashboard visualization
✅ Multi-Slot Management
Morning, Afternoon, Evening, Night compartments
✅ Low-Cost Implementation (~₹1000)

**How It Works**
System activates for a time slot
LED starts blinking as reminder
Detects:
Hand movement (IR sensor)
Weight drop (Load cell)
If both conditions are satisfied → ✔ Pill Taken
Else → ❌ Not Taken
Data stored in Firebase
System learns and updates future reminders

**Pin Configuration**
📍 HX711 → ESP32
VCC → 3.3V
GND → GND
DT → GPIO 4
SCK → GPIO 5
📍 IR Sensor → ESP32
VCC → 3.3V
GND → GND
OUT → GPIO 33
📍 LEDs → ESP32
Morning → GPIO 14
Afternoon → GPIO 27
Evening → GPIO 26
Night → GPIO 25

**Running the Project**
Connect hardware as per diagram
Upload code to ESP32
Open Serial Monitor (115200 baud)
Perform calibration (empty + full box)
System starts monitoring automatically
