/*
 * ============================================================
 *  Smart Parking IoT — ESP32  ★ LISTO PARA CARGAR ★
 *  Broker: HiveMQ Cloud (TLS :8883)
 *  Dashboard: Ubidots
 * ============================================================
 *  LIBRERÍAS (Library Manager del IDE):
 *    PubSubClient          by Nick O'Leary
 *    DHT sensor library    by Adafruit
 *    Adafruit Unified Sensor
 *    MPU6050_light         by rfetick
 *    ArduinoJson           by Benoit Blanchon  (v6.x)
 *    NewPing               by Tim Eckel
 *    NTPClient             by Fabrice Weinberg
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUDP.h>
#include <DHT.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <NewPing.h>
#include <ArduinoJson.h>

// ════════════════════════════════════════════════════════════
//  CREDENCIALES
// ════════════════════════════════════════════════════════════
const char* WIFI_SSID     = "iPhone de Andres";
const char* WIFI_PASSWORD = "12345678";

const char* HIVEMQ_HOST   = "0767c5c7c7914298a3dd01a0d2529721.s1.eu.hivemq.cloud";
const char* HIVEMQ_USER   = "TU_USUARIO_HIVEMQ";   // ← el que creaste en Access Management
const char* HIVEMQ_PASS   = "TU_CONTRASEÑA_HIVEMQ"; // ← la contraseña que pusiste
const int   HIVEMQ_PORT   = 8883;

const char* UBIDOTS_TOKEN = "BBUS-lUutAWnWzb0z19lGzuz29GT4uNigLo";
const char* UBIDOTS_HOST  = "industrial.api.ubidots.com";
const int   UBIDOTS_PORT  = 1883;
const char* DEVICE_LABEL  = "smart-parking-1";

// ════════════════════════════════════════════════════════════
//  PINES
// ════════════════════════════════════════════════════════════
#define TRIG_PIN     5
#define ECHO_PIN     18
#define DHT_PIN      4
#define LED_LIBRE    2
#define LED_OCUPADO  15
#define SDA_PIN      21
#define SCL_PIN      22

// ════════════════════════════════════════════════════════════
//  UMBRALES
// ════════════════════════════════════════════════════════════
#define DIST_OCUPADO_CM  15     // < 15 cm = plaza ocupada
#define SISMO_G          1.5f  // > 1.5g = alerta sísmica
#define INTERVALO_MS     5000  // publica cada 5 segundos

// ════════════════════════════════════════════════════════════
//  OBJETOS
// ════════════════════════════════════════════════════════════
DHT          dht(DHT_PIN, DHT22);
MPU6050      mpu(Wire);
NewPing      sonar(TRIG_PIN, ECHO_PIN, 400);

WiFiClientSecure secureClient;
PubSubClient     mqttHiveMQ(secureClient);

WiFiClient       plainClient;
PubSubClient     mqttUbidots(plainClient);

WiFiUDP    udpNTP;
NTPClient  timeClient(udpNTP, "pool.ntp.org", -18000, 60000);

bool          plazaOcupada       = false;
unsigned long ultimaPublicacion  = 0;
unsigned long arranque           = 0;

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n╔══════════════════════════════╗");
  Serial.println("║   Smart Parking IoT v1.0     ║");
  Serial.println("╚══════════════════════════════╝");

  pinMode(LED_LIBRE,   OUTPUT);
  pinMode(LED_OCUPADO, OUTPUT);
  parpadeoArranque();

  Wire.begin(SDA_PIN, SCL_PIN);
  dht.begin();

  byte statusMPU = mpu.begin();
  if (statusMPU != 0) {
    Serial.println("[MPU6050] NO encontrado — revisa cables SDA/SCL");
  } else {
    Serial.println("[MPU6050] OK — calibrando (no mover el dispositivo)...");
    mpu.calcOffsets(true, true);
    Serial.println("[MPU6050] Calibración lista.");
  }

  conectarWiFi();

  timeClient.begin();
  timeClient.update();
  Serial.print("[NTP] Hora sincronizada: ");
  Serial.println(timeClient.getFormattedTime());

  // HiveMQ Cloud — setInsecure() evita tener que cargar el certificado CA
  // y funciona correctamente con el plan Serverless de HiveMQ
  secureClient.setInsecure();
  mqttHiveMQ.setServer(HIVEMQ_HOST, HIVEMQ_PORT);
  mqttHiveMQ.setBufferSize(512);
  conectarHiveMQ();

  mqttUbidots.setServer(UBIDOTS_HOST, UBIDOTS_PORT);
  mqttUbidots.setBufferSize(512);
  conectarUbidots();

  arranque = millis();
  Serial.println("\n[OK] Todo listo — publicando cada 5 segundos\n");
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  if (!mqttHiveMQ.connected())  conectarHiveMQ();
  if (!mqttUbidots.connected()) conectarUbidots();
  mqttHiveMQ.loop();
  mqttUbidots.loop();
  timeClient.update();

  if (millis() - ultimaPublicacion < INTERVALO_MS) return;
  ultimaPublicacion = millis();
  leerYPublicar();
}

// ════════════════════════════════════════════════════════════
//  LECTURA Y PUBLICACIÓN
// ════════════════════════════════════════════════════════════
void leerYPublicar() {

  // ── HC-SR04 ───────────────────────────────────────────────
  int distCm = sonar.ping_cm();
  if (distCm == 0) distCm = 400;

  bool nuevaOcupacion = (distCm < DIST_OCUPADO_CM);
  if (nuevaOcupacion != plazaOcupada) {
    plazaOcupada = nuevaOcupacion;
    digitalWrite(LED_LIBRE,   plazaOcupada ? LOW  : HIGH);
    digitalWrite(LED_OCUPADO, plazaOcupada ? HIGH : LOW);
  }
  Serial.printf("[SR04]  %d cm → %s\n", distCm, plazaOcupada ? "OCUPADA" : "LIBRE");

  // ── DHT22 ─────────────────────────────────────────────────
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  if (!isnan(temp))
    Serial.printf("[DHT22] %.1f°C  %.1f%%\n", temp, hum);
  else
    Serial.println("[DHT22] Error de lectura");

  // ── MPU6050 ───────────────────────────────────────────────
  mpu.update();
  float ax  = mpu.getAccX();
  float ay  = mpu.getAccY();
  float az  = mpu.getAccZ() - 1.0f;
  float mag = sqrt(ax*ax + ay*ay + az*az);
  Serial.printf("[MPU]   %.2fg\n", mag);

  // ── Publicar ──────────────────────────────────────────────
  publicarHiveMQ(distCm, temp, hum, mag);
  publicarUbidots(distCm, temp, hum, mag);

  if (mag > SISMO_G) {
    Serial.println("[⚠ SISMO] Alerta enviada!");
    alertaSismica(ax, ay, az, mag);
  }

  Serial.println("─────────────────────────────────");
}

// ════════════════════════════════════════════════════════════
//  PUBLICA A HIVEMQ
// ════════════════════════════════════════════════════════════
void publicarHiveMQ(int dist, float temp, float hum, float mag) {
  char buf[256];
  StaticJsonDocument<256> doc;

  // Estado plaza
  doc.clear();
  doc["occupied"] = plazaOcupada;
  doc["dist_cm"]  = dist;
  doc["ts"]       = timeClient.getEpochTime();
  serializeJson(doc, buf);
  mqttHiveMQ.publish("parking/spot/1/status", buf, true);

  // Ambiental
  if (!isnan(temp)) {
    doc.clear();
    doc["temp_c"]  = serialized(String(temp, 1));
    doc["hum_pct"] = serialized(String(hum,  1));
    doc["ts"]      = timeClient.getEpochTime();
    serializeJson(doc, buf);
    mqttHiveMQ.publish("parking/env/1/data", buf);
  }

  // Healthcheck
  doc.clear();
  doc["status"]   = "ok";
  doc["uptime_s"] = (millis() - arranque) / 1000;
  doc["rssi"]     = WiFi.RSSI();
  doc["ip"]       = WiFi.localIP().toString();
  serializeJson(doc, buf);
  mqttHiveMQ.publish("parking/system/status", buf);

  Serial.println("[HiveMQ] ✓");
}

// ════════════════════════════════════════════════════════════
//  PUBLICA A UBIDOTS
// ════════════════════════════════════════════════════════════
void publicarUbidots(int dist, float temp, float hum, float mag) {
  StaticJsonDocument<300> doc;
  char topic[64], payload[300];

  doc.createNestedArray("ocupada").createNestedObject()["value"]       = plazaOcupada ? 1 : 0;
  doc.createNestedArray("distancia_cm").createNestedObject()["value"]  = dist;
  doc.createNestedArray("vibracion_g").createNestedObject()["value"]   = serialized(String(mag, 2));

  if (!isnan(temp)) {
    doc.createNestedArray("temperatura").createNestedObject()["value"] = serialized(String(temp, 1));
    doc.createNestedArray("humedad").createNestedObject()["value"]     = serialized(String(hum,  1));
  }

  snprintf(topic, sizeof(topic), "/v1.6/devices/%s", DEVICE_LABEL);
  serializeJson(doc, payload);
  mqttUbidots.publish(topic, payload);

  Serial.println("[Ubidots] ✓");
}

// ════════════════════════════════════════════════════════════
//  ALERTA SÍSMICA
// ════════════════════════════════════════════════════════════
void alertaSismica(float ax, float ay, float az, float mag) {
  char buf[200];
  StaticJsonDocument<200> doc;
  doc["acc_x"]     = serialized(String(ax,  2));
  doc["acc_y"]     = serialized(String(ay,  2));
  doc["acc_z"]     = serialized(String(az,  2));
  doc["magnitude"] = serialized(String(mag, 2));
  doc["ts"]        = timeClient.getEpochTime();
  serializeJson(doc, buf);
  mqttHiveMQ.publish("parking/seismic/1/alert", buf);

  char topic[64], payload[80];
  snprintf(topic,   sizeof(topic),   "/v1.6/devices/%s", DEVICE_LABEL);
  snprintf(payload, sizeof(payload), "{\"alerta_sismica\":[{\"value\":%.2f}]}", mag);
  mqttUbidots.publish(topic, payload);

  // Parpadeo LED rojo de alarma
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_OCUPADO, HIGH); delay(150);
    digitalWrite(LED_OCUPADO, LOW);  delay(150);
  }
}

// ════════════════════════════════════════════════════════════
//  CONEXIONES
// ════════════════════════════════════════════════════════════
void conectarWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Conectando a 'iPhone de Andres'");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.printf("\n[WiFi] Conectado — IP: %s\n", WiFi.localIP().toString().c_str());
}

void conectarHiveMQ() {
  String clientId = "ESP32_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("[HiveMQ] Conectando");
  while (!mqttHiveMQ.connect(clientId.c_str(), HIVEMQ_USER, HIVEMQ_PASS)) {
    Serial.printf(" (err %d)", mqttHiveMQ.state());
    delay(3000);
  }
  Serial.println(" OK (TLS 8883)");
  mqttHiveMQ.subscribe("parking/config/1/cmd");
}

void conectarUbidots() {
  String clientId = "ESP32_UBI_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("[Ubidots] Conectando");
  while (!mqttUbidots.connect(clientId.c_str(), UBIDOTS_TOKEN, "")) {
    Serial.printf(" (err %d)", mqttUbidots.state());
    delay(3000);
  }
  Serial.println(" OK");
}

void parpadeoArranque() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_LIBRE,   HIGH);
    digitalWrite(LED_OCUPADO, HIGH);
    delay(200);
    digitalWrite(LED_LIBRE,   LOW);
    digitalWrite(LED_OCUPADO, LOW);
    delay(200);
  }
}
