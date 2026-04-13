// ============================================================
//  Smart Pill Box — Adaptive Reminder System (Demo Mode)
//  4-Compartment Cumulative Weight Detection
// ============================================================

#include <WiFi.h>
#include <time.h>
#include "HX711.h"
#include <Firebase_ESP_Client.h>

// ─── WiFi ────────────────────────────────────────────────────
const char* ssid     = "Khushi dell";
const char* password = "paastaaa";

// ─── NTP ─────────────────────────────────────────────────────
const char* ntpServer     = "pool.ntp.org";
const long  gmtOffset_sec = 19800;

// ─── Firebase ────────────────────────────────────────────────
#define API_KEY      "AIzaSyC4MUXtW0NtUdZ0kHsxSZrxxU584uJ2GAo"
#define DATABASE_URL "https://smartpillbox-3ec4b-default-rtdb.firebaseio.com/"

FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;

// ─── Pins ────────────────────────────────────────────────────
#define IR_PIN        33
#define MORNING_LED   14
#define AFTERNOON_LED 27
#define EVENING_LED   26
#define NIGHT_LED     25
#define DOUT           4
#define SCK            5

// ─── HX711 ───────────────────────────────────────────────────
HX711 scale;
float calibration_factor = -7050;

// ─── Multi-compartment weight config ─────────────────────────
// CHANGED: COMPARTMENT_WEIGHT is now measured dynamically at startup
float COMPARTMENT_WEIGHT   = 0.0f;   // calculated from (fullWeight - emptyWeight) / 4
const float WEIGHT_MARGIN  = 0.6f;
const int   NUM_SLOTS      = 4;

float initialFullWeight = 0.0f;
float slotTargetWeight[NUM_SLOTS];
bool  slotWeightConfirmed[NUM_SLOTS] = { false, false, false, false };
float currentWeight = 0.0f;

// ─── Demo timing ─────────────────────────────────────────────
const unsigned long SLOT_DURATION = 40000UL;
const unsigned long GAP_DURATION  = 10000UL;

// ─── Adaptive system ─────────────────────────────────────────
const int HISTORY_ROUNDS = 5;

struct SlotConfig {
  String        name;
  int           led;
  unsigned long adjustedMs;
  unsigned long sumDelays;
  int           countRounds;
};

SlotConfig slots[4] = {
  { "MORNING",   MORNING_LED,   0, 0, 0 },
  { "AFTERNOON", AFTERNOON_LED, 0, 0, 0 },
  { "EVENING",   EVENING_LED,   0, 0, 0 },
  { "NIGHT",     NIGHT_LED,     0, 0, 0 }
};

// ─── State machine ───────────────────────────────────────────
int           state         = 0;
bool          pillTaken     = false;
bool          missedWritten = false;
unsigned long stateStartMs  = 0;
int           roundNumber   = 1;

// ─── LED blink ───────────────────────────────────────────────
const unsigned long BLINK_INTERVAL = 500;
unsigned long prevBlinkMs = 0;
bool ledBlink = LOW;

// ─── Timers ──────────────────────────────────────────────────
const unsigned long FIREBASE_INTERVAL  = 15000;
const unsigned long KEEPALIVE_INTERVAL = 20000;
unsigned long prevFirebaseMs  = 0;
unsigned long prevKeepaliveMs = 0;

// ─── Serial print ────────────────────────────────────────────
const unsigned long PRINT_INTERVAL = 5000;
unsigned long prevPrintMs = 0;

// ─── IR detection ────────────────────────────────────────────
int irDetectCount = 0;
const int REQ_IR_DETECTS = 3;

// ─── Helpers ─────────────────────────────────────────────────

float readStableWeight() {
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += scale.get_units(1);
    delay(10);
  }
  return sum / 10.0f;
}

// CHANGED: calibrateWeight() now measures empty box first, then full box,
// and computes COMPARTMENT_WEIGHT dynamically. No hardcoded pill weight needed.
void calibrateWeight() {
  Serial.println("\n--- WEIGHT CALIBRATION ---");

  // Step 1: Measure empty box
  Serial.println("Step 1: Place EMPTY pill box (no pills), waiting 6s...");
  delay(6000);
  float emptyWeight = readStableWeight();
  Serial.printf("Empty box weight: %.2f g\n", emptyWeight);

  // Step 2: Measure full box
  Serial.println("Step 2: Place FULL pill box (all 4 compartments loaded), waiting 6s...");
  delay(6000);
  initialFullWeight = readStableWeight();
  Serial.printf("Full box weight: %.2f g\n", initialFullWeight);

  // Step 3: Compute per-compartment weight
  float totalPillWeight = initialFullWeight - emptyWeight;
  COMPARTMENT_WEIGHT = totalPillWeight / (float)NUM_SLOTS;
  Serial.printf("Total pill weight: %.2f g | Per compartment: %.2f g\n",
    totalPillWeight, COMPARTMENT_WEIGHT);

  // Step 4: Compute targets (same logic as before, now using measured COMPARTMENT_WEIGHT)
  for (int i = 0; i < NUM_SLOTS; i++) {
    slotTargetWeight[i]    = initialFullWeight - (COMPARTMENT_WEIGHT * (i + 1));
    slotWeightConfirmed[i] = false;
    Serial.printf("  After slot %d (%s) target: %.2f g\n",
      i, slots[i].name.c_str(), slotTargetWeight[i]);
  }
  Serial.println("--- CALIBRATION DONE ---\n");
}

void checkWeightForAllSlots(float w) {
  for (int i = 0; i < NUM_SLOTS; i++) {
    if (!slotWeightConfirmed[i]) {
      if (w <= slotTargetWeight[i] + WEIGHT_MARGIN) {
        slotWeightConfirmed[i] = true;
        Serial.printf("[WEIGHT] Slot %d (%s) threshold crossed: %.2f g (target %.2f g)\n",
          i, slots[i].name.c_str(), w, slotTargetWeight[i]);
      }
    }
  }
}

void allLedsOff() {
  digitalWrite(MORNING_LED,   LOW);
  digitalWrite(AFTERNOON_LED, LOW);
  digitalWrite(EVENING_LED,   LOW);
  digitalWrite(NIGHT_LED,     LOW);
}

int slotIndex(int s) {
  if (s == 0) return 0;
  if (s == 2) return 1;
  if (s == 4) return 2;
  if (s == 6) return 3;
  return -1;
}

bool isSlotState(int s) {
  return slotIndex(s) >= 0;
}

// ─── Firebase helpers ────────────────────────────────────────

void firebaseReconnect() {
  Serial.println("Reconnecting Firebase SSL...");
  Firebase.begin(&config, &auth);
  delay(1000);
}

bool firebaseSetWithRetry(const String &path, FirebaseJson &json) {
  bool ok = Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json);
  if (!ok) {
    Serial.println("Firebase write failed, reconnecting and retrying...");
    firebaseReconnect();
    ok = Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json);
  }
  return ok;
}

void firebaseKeepalive() {
  if (!Firebase.ready()) {
    firebaseReconnect();
    return;
  }
  Firebase.RTDB.getInt(&fbdo, "/keepalive");
  Firebase.RTDB.setInt(&fbdo, "/keepalive", (int)(millis() / 1000));
  Serial.println("[KEEPALIVE] SSL ping sent");
}

// ─── Adaptive core ───────────────────────────────────────────

void loadAdaptiveData(int i) {
  String base = "/adaptive/" + slots[i].name;

  if (Firebase.RTDB.getInt(&fbdo, (base + "/sumDelays").c_str()))
    slots[i].sumDelays = (unsigned long)fbdo.intData();

  if (Firebase.RTDB.getInt(&fbdo, (base + "/countRounds").c_str()))
    slots[i].countRounds = fbdo.intData();

  if (slots[i].countRounds > 0) {
    slots[i].adjustedMs = slots[i].sumDelays / slots[i].countRounds;
    Serial.printf("[ADAPTIVE] %s loaded: avg intake at %lus into slot (n=%d)\n",
      slots[i].name.c_str(),
      slots[i].adjustedMs / 1000,
      slots[i].countRounds);
  }
}

void recordAndAdapt(int i, unsigned long actualMs) {
  actualMs = min(actualMs, SLOT_DURATION - 500UL);

  if (slots[i].countRounds >= HISTORY_ROUNDS) {
    unsigned long avgMs = slots[i].sumDelays / slots[i].countRounds;
    slots[i].sumDelays -= avgMs;
    slots[i].countRounds--;
  }
  slots[i].sumDelays   += actualMs;
  slots[i].countRounds += 1;

  unsigned long newAdj = slots[i].sumDelays / slots[i].countRounds;
  slots[i].adjustedMs  = newAdj;

  if (!Firebase.ready()) {
    firebaseReconnect();
  }

  FirebaseJson json;
  json.set("sumDelays",   (int)slots[i].sumDelays);
  json.set("countRounds", slots[i].countRounds);
  json.set("adjustedMs",  (int)newAdj);
  json.set("adjustedSec", (int)(newAdj / 1000));
  json.set("lastRound",   roundNumber);

  if (!Firebase.RTDB.updateNode(&fbdo, ("/adaptive/" + slots[i].name).c_str(), &json)) {
    firebaseReconnect();
    Firebase.RTDB.updateNode(&fbdo, ("/adaptive/" + slots[i].name).c_str(), &json);
  }

  String histPath = "/adaptive/" + slots[i].name + "/history/" + String(roundNumber);
  Firebase.RTDB.setInt(&fbdo, histPath.c_str(), (int)(actualMs / 1000));

  Serial.printf("[ADAPTIVE] %s: pill at %lus | new reminder at %lus | n=%d\n",
    slots[i].name.c_str(),
    actualMs / 1000,
    newAdj / 1000,
    slots[i].countRounds);
}

// ─── Firebase slot writers ───────────────────────────────────

void writePillTaken(int i, unsigned long actualMs) {
  if (!Firebase.ready()) {
    firebaseReconnect();
  }

  unsigned long long ts = (unsigned long long)time(nullptr) * 1000ULL;
  FirebaseJson json;
  json.set("status",        "TAKEN");
  json.set("timestamp",     ts);
  json.set("actualMs",      (int)actualMs);
  json.set("actualSec",     (int)(actualMs / 1000));
  json.set("adjustedMs",    (int)slots[i].adjustedMs);
  json.set("adjustedSec",   (int)(slots[i].adjustedMs / 1000));
  json.set("round",         roundNumber);
  json.set("weightAtTaken", currentWeight);
  json.set("weightTarget",  slotTargetWeight[i]);

  bool ok = firebaseSetWithRetry("/slots/" + slots[i].name, json);
  Serial.println(ok ? "TAKEN written to Firebase"
                    : "Firebase FAILED: " + fbdo.errorReason());
}

void writeNotTaken(int i) {
  if (!Firebase.ready()) {
    firebaseReconnect();
  }

  FirebaseJson json;
  json.set("status",    "NOT TAKEN");
  json.set("timestamp", 0);
  json.set("actualMs",  0);
  json.set("round",     roundNumber);

  bool ok = firebaseSetWithRetry("/slots/" + slots[i].name, json);
  Serial.println(ok ? "NOT TAKEN written"
                    : "Firebase FAILED: " + fbdo.errorReason());
}

// ─── Setup ───────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  pinMode(IR_PIN,        INPUT);
  pinMode(MORNING_LED,   OUTPUT);
  pinMode(AFTERNOON_LED, OUTPUT);
  pinMode(EVENING_LED,   OUTPUT);
  pinMode(NIGHT_LED,     OUTPUT);
  allLedsOff();

  scale.begin(DOUT, SCK);
  scale.set_scale(calibration_factor);
  scale.tare();
  delay(2000);

  calibrateWeight();

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  unsigned long wStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wStart < 15000) {
    Serial.print(".");
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    configTime(gmtOffset_sec, 0, ntpServer);
  } else {
    Serial.println("\nWiFi FAILED");
  }

  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.begin(&config, &auth);
  fbdo.setBSSLBufferSize(1024, 512);
  Firebase.reconnectWiFi(true);

  if (Firebase.signUp(&config, &auth, "", ""))
    Serial.println("Firebase auth OK");
  else
    Serial.println("Firebase auth FAILED: " +
      String(config.signer.signupError.message.c_str()));
  delay(3000);

  for (int i = 0; i < 4; i++) {
    FirebaseJson slotJson;
    slotJson.set("status",        "NOT TAKEN");
    slotJson.set("timestamp",     0);
    slotJson.set("actualMs",      0);
    slotJson.set("actualSec",     0);
    slotJson.set("adjustedMs",    0);
    slotJson.set("adjustedSec",   0);
    slotJson.set("round",         0);
    slotJson.set("weightAtTaken", 0.0);
    slotJson.set("weightTarget",  slotTargetWeight[i]);
    Firebase.RTDB.setJSON(&fbdo, ("/slots/" + slots[i].name).c_str(), &slotJson);

    FirebaseJson adaptJson;
    adaptJson.set("sumDelays",   0);
    adaptJson.set("countRounds", 0);
    adaptJson.set("adjustedMs",  0);
    adaptJson.set("adjustedSec", 0);
    adaptJson.set("lastRound",   0);
    Firebase.RTDB.setJSON(&fbdo, ("/adaptive/" + slots[i].name).c_str(), &adaptJson);
  }

  for (int i = 0; i < 4; i++) loadAdaptiveData(i);

  state           = 0;
  pillTaken       = false;
  missedWritten   = false;
  stateStartMs    = millis();
  prevKeepaliveMs = millis();

  Serial.println("\n=== Demo Ready ===");
  Serial.printf("SLOT=%lus  GAP=%lus\n", SLOT_DURATION / 1000, GAP_DURATION / 1000);
  Serial.printf("Compartment weight: %.2fg | Full box: %.2fg\n\n",
    COMPARTMENT_WEIGHT, initialFullWeight);
}

// ─── Loop ────────────────────────────────────────────────────

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000)
      delay(500);
  }

  unsigned long now_ms  = millis();
  unsigned long elapsed = now_ms - stateStartMs;
  unsigned long dur     = isSlotState(state) ? SLOT_DURATION : GAP_DURATION;
  int           si      = slotIndex(state);

  if (now_ms - prevKeepaliveMs >= KEEPALIVE_INTERVAL) {
    prevKeepaliveMs = now_ms;
    firebaseKeepalive();
  }

  int irState     = digitalRead(IR_PIN);
  currentWeight   = readStableWeight();

  checkWeightForAllSlots(currentWeight);

  // ── State transition ─────────────────────────────────────
  if (elapsed >= dur) {

    if (isSlotState(state) && !pillTaken) {
      if (slotWeightConfirmed[si]) {
        pillTaken = true;
        writePillTaken(si, elapsed);
        recordAndAdapt(si, elapsed);
        Serial.printf("[WEIGHT-LATE] %s confirmed taken by weight at slot end\n",
          slots[si].name.c_str());
      } else if (!missedWritten) {
        writeNotTaken(si);
        missedWritten = true;
      }
    }

    state++;
    if (state > 7) {
      state = 0;
      roundNumber++;
      Serial.printf("\n========= ROUND %d START =========\n", roundNumber);
      Serial.println("Re-measuring box weight for new round...");
      delay(2000);
      calibrateWeight();
    }

    pillTaken       = false;
    missedWritten   = false;
    irDetectCount   = 0;
    stateStartMs    = millis();
    elapsed         = 0;
    si              = slotIndex(state);
    allLedsOff();

    if (isSlotState(state)) {
      Serial.printf(">>> State %d | %s | reminder at %lus into slot\n",
        state, slots[si].name.c_str(), slots[si].adjustedMs / 1000);
    } else {
      Serial.printf(">>> State %d | GAP\n", state);
    }
  }

  // ── Slot logic ───────────────────────────────────────────
  if (isSlotState(state)) {

    bool reminderActive = (elapsed >= slots[si].adjustedMs);

    if (!pillTaken && reminderActive) {

      if (!pillTaken && slotWeightConfirmed[si]) {
        pillTaken = true;
        writePillTaken(si, elapsed);
        recordAndAdapt(si, elapsed);
        Serial.printf("[WEIGHT] %s: TAKEN (weight %.2fg, target %.2fg)\n",
          slots[si].name.c_str(), currentWeight, slotTargetWeight[si]);
      }

      if (!pillTaken && irState == LOW) {
        irDetectCount++;
        if (irDetectCount >= REQ_IR_DETECTS) {
          pillTaken = true;
          writePillTaken(si, elapsed);
          recordAndAdapt(si, elapsed);
          irDetectCount = 0;
          Serial.printf("[IR] %s: TAKEN\n", slots[si].name.c_str());
        }
      } else if (!pillTaken) {
        irDetectCount = 0;
      }

      if (!pillTaken) {
        if (now_ms - prevBlinkMs >= BLINK_INTERVAL) {
          prevBlinkMs = now_ms;
          ledBlink    = !ledBlink;
          digitalWrite(slots[si].led, ledBlink);
        }
      }

    } else if (!pillTaken && !reminderActive) {
      digitalWrite(slots[si].led, LOW);
    }

    if (pillTaken) {
      digitalWrite(slots[si].led, LOW);
    }

  } else {
    allLedsOff();
  }

  // ── Firebase heartbeat every 15s ─────────────────────────
  if (Firebase.ready() && now_ms - prevFirebaseMs >= FIREBASE_INTERVAL) {
    prevFirebaseMs = now_ms;
    if (isSlotState(state) && !pillTaken) {
      FirebaseJson json;
      json.set("status",      "NOT TAKEN");
      json.set("timestamp",   0);
      json.set("actualMs",    0);
      json.set("adjustedMs",  (int)slots[si].adjustedMs);
      json.set("adjustedSec", (int)(slots[si].adjustedMs / 1000));
      json.set("round",       roundNumber);
      firebaseSetWithRetry("/slots/" + slots[si].name, json);
    }
  }

  // ── Serial debug every 5s ────────────────────────────────
  if (now_ms - prevPrintMs >= PRINT_INTERVAL) {
    prevPrintMs = now_ms;
    int secElapsed = elapsed / 1000;
    int secLeft    = (dur > elapsed) ? (dur - elapsed) / 1000 : 0;

    Serial.printf("[R%d] State:%d(%s) elapsed:%ds left:%ds | IR:%s Wt:%.2fg",
      roundNumber, state,
      isSlotState(state) ? slots[si].name.c_str() : "GAP",
      secElapsed, secLeft,
      irState == LOW ? "OBJ" : "---",
      currentWeight);

    Serial.print(" | Slots:[");
    for (int i = 0; i < NUM_SLOTS; i++) {
      Serial.print(slotWeightConfirmed[i] ? "TAKEN" : "pending");
      if (i < NUM_SLOTS - 1) Serial.print(", ");
    }
    Serial.print("]");

    if (isSlotState(state)) {
      bool ra = elapsed >= slots[si].adjustedMs;
      Serial.printf(" | rem@%lus:%s | %s",
        slots[si].adjustedMs / 1000,
        ra ? "ACTIVE" : "wait",
        pillTaken ? "TAKEN" : "pending");
    }
    Serial.println();
  }

  delay(100);
}
