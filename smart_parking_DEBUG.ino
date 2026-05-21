/*
 * ============================================================
 *  Smart Parking IoT — ESP32  ★ VERSIÓN DEBUG ★
 *  Muestra exactamente qué funciona y qué no en el Serial Monitor
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
const char* HIVEMQ_USER   = "MauLopez";
const char* HIVEMQ_PASS   = "Mauricio123*";
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

#define DIST_OCUPADO_CM  15
#define SISMO_G          1.5f
#define INTERVALO_MS     5000

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

bool          plazaOcupada      = false;
unsigned long ultimaPublicacion = 0;
unsigned long arranque          = 0;

// ── Flags de estado para el resumen ──────────────────────────
bool ok_wifi    = false;
bool ok_ntp     = false;
bool ok_hivemq  = false;
bool ok_ubidots = false;
bool ok_dht     = false;
bool ok_mpu     = false;
bool ok_sr04    = false;

// ════════════════════════════════════════════════════════════
//  HELPERS DE LOG
// ════════════════════════════════════════════════════════════
void logOK(const char* modulo, const char* msg) {
  Serial.printf("  [✓ OK ] %-10s %s\n", modulo, msg);
}
void logERR(const char* modulo, const char* msg) {
  Serial.printf("  [✗ ERR] %-10s %s\n", modulo, msg);
}
void logINFO(const char* modulo, const char* msg) {
  Serial.printf("  [·    ] %-10s %s\n", modulo, msg);
}
void separador() {
  Serial.println("  ─────────────────────────────────────────────");
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║     Smart Parking IoT — MODO DEBUG         ║");
  Serial.println("║     Revisa cada línea para diagnosticar    ║");
  Serial.println("╚════════════════════════════════════════════╝\n");

  // ── PINES LED ─────────────────────────────────────────────
  Serial.println("[ PASO 1 ] Inicializando pines LED...");
  pinMode(LED_LIBRE,   OUTPUT);
  pinMode(LED_OCUPADO, OUTPUT);
  digitalWrite(LED_LIBRE,   LOW);
  digitalWrite(LED_OCUPADO, LOW);
  // Parpadeo para confirmar que los LEDs físicamente funcionan
  Serial.println("           → Ambos LEDs deben parpadear 3 veces ahora");
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_LIBRE,   HIGH);
    digitalWrite(LED_OCUPADO, HIGH);
    delay(300);
    digitalWrite(LED_LIBRE,   LOW);
    digitalWrite(LED_OCUPADO, LOW);
    delay(300);
  }
  logOK("LEDs", "GPIO2 (verde) y GPIO15 (rojo) configurados");
  separador();

  // ── I2C + MPU6050 ─────────────────────────────────────────
  Serial.println("[ PASO 2 ] Buscando MPU6050 en I²C...");
  Serial.printf("           SDA=GPIO%d  SCL=GPIO%d\n", SDA_PIN, SCL_PIN);
  Wire.begin(SDA_PIN, SCL_PIN);

  // Escaneo I2C para ver qué hay conectado
  Serial.println("           Escaneando bus I²C:");
  int dispositivosI2C = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("           → Dispositivo encontrado en 0x%02X", addr);
      if (addr == 0x68) Serial.print("  ← MPU6050 (AD0=LOW)");
      if (addr == 0x69) Serial.print("  ← MPU6050 (AD0=HIGH)");
      Serial.println();
      dispositivosI2C++;
    }
  }
  if (dispositivosI2C == 0) {
    logERR("I2C", "NINGÚN dispositivo encontrado — revisa SDA(GPIO21) y SCL(GPIO22)");
  } else {
    Serial.printf("           %d dispositivo(s) en el bus\n", dispositivosI2C);
  }

  byte statusMPU = mpu.begin();
  if (statusMPU != 0) {
    logERR("MPU6050", "No responde. Causas probables:");
    Serial.println("           • SDA no conectado a GPIO21");
    Serial.println("           • SCL no conectado a GPIO22");
    Serial.println("           • VCC del MPU6050 no conectado a 3.3V");
    Serial.println("           • GND no conectado");
    Serial.println("           • Módulo dañado o AD0 flotante");
    ok_mpu = false;
  } else {
    logOK("MPU6050", "Encontrado en 0x68 — calibrando (no mover)...");
    mpu.calcOffsets(true, true);
    logOK("MPU6050", "Calibración lista");
    ok_mpu = true;
  }
  separador();

  // ── DHT22 ─────────────────────────────────────────────────
  Serial.println("[ PASO 3 ] Probando DHT22...");
  Serial.printf("           Data=GPIO%d\n", DHT_PIN);
  dht.begin();
  delay(2500); // DHT22 necesita ~2s para primera lectura

  float tempTest = dht.readTemperature();
  float humTest  = dht.readHumidity();

  if (isnan(tempTest) || isnan(humTest)) {
    logERR("DHT22", "Sin respuesta. Causas probables:");
    Serial.println("           • Pin DATA no conectado a GPIO4");
    Serial.println("           • Falta resistencia pull-up 10kΩ entre DATA y 3.3V");
    Serial.println("           • VCC no conectado a 3.3V");
    Serial.println("           • GND no conectado");
    Serial.println("           • Sensor dañado");
    ok_dht = false;
  } else {
    char buf[60];
    snprintf(buf, sizeof(buf), "Temp=%.1f°C  Hum=%.1f%%  ← valores reales", tempTest, humTest);
    logOK("DHT22", buf);
    ok_dht = true;
  }
  separador();

  // ── HC-SR04 ───────────────────────────────────────────────
  Serial.println("[ PASO 4 ] Probando HC-SR04...");
  Serial.printf("           TRIG=GPIO%d  ECHO=GPIO%d\n", TRIG_PIN, ECHO_PIN);

  // 3 mediciones de prueba
  int lecturas[3];
  for (int i = 0; i < 3; i++) {
    lecturas[i] = sonar.ping_cm();
    delay(100);
  }
  Serial.printf("           3 lecturas: %d cm | %d cm | %d cm\n",
                lecturas[0], lecturas[1], lecturas[2]);

  bool todasCero = (lecturas[0] == 0 && lecturas[1] == 0 && lecturas[2] == 0);
  if (todasCero) {
    logERR("HC-SR04", "Siempre 0 cm — sin eco. Causas probables:");
    Serial.println("           • TRIG no conectado a GPIO5");
    Serial.println("           • ECHO no conectado a GPIO18");
    Serial.println("           • VCC no conectado a 3.3V (o 5V si es modelo estándar)");
    Serial.println("           • GND no conectado");
    Serial.println("           • Objeto muy cerca (<2cm) o muy lejos (>4m)");
    ok_sr04 = false;
  } else {
    char buf[60];
    snprintf(buf, sizeof(buf), "Distancia actual: %d cm", lecturas[0]);
    logOK("HC-SR04", buf);
    ok_sr04 = true;
  }
  separador();

  // ── WIFI ──────────────────────────────────────────────────
  Serial.println("[ PASO 5 ] Conectando a WiFi...");
  Serial.printf("           SSID: \"%s\"\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int intentosWifi = 0;
  while (WiFi.status() != WL_CONNECTED && intentosWifi < 20) {
    delay(500);
    Serial.print(".");
    intentosWifi++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    logERR("WiFi", "No se pudo conectar. Causas:");
    Serial.println("           • SSID o contraseña incorrectos");
    Serial.println("           • El hotspot del iPhone no está activado");
    Serial.printf("            • Estado WiFi: %d\n", WiFi.status());
    ok_wifi = false;
  } else {
    char buf[80];
    snprintf(buf, sizeof(buf), "Conectado — IP: %s  RSSI: %d dBm",
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
    logOK("WiFi", buf);
    ok_wifi = true;
  }
  separador();

  if (!ok_wifi) {
    logERR("SISTEMA", "Sin WiFi — no se puede continuar con NTP ni MQTT");
    imprimirResumen();
    Serial.println("\n  *** El sistema entrará en loop de espera ***");
    while (true) { delay(5000); Serial.println("  [WiFi] Sin conexión..."); }
  }

  // ── NTP ───────────────────────────────────────────────────
  Serial.println("[ PASO 6 ] Sincronizando hora NTP...");
  Serial.println("           Servidor: pool.ntp.org  Zona: UTC-5 (Bogotá)");
  timeClient.begin();
  bool ntpOK = timeClient.update();

  if (!ntpOK || timeClient.getEpochTime() < 1000000) {
    logERR("NTP", "No se pudo sincronizar la hora");
    ok_ntp = false;
  } else {
    char buf[60];
    snprintf(buf, sizeof(buf), "Hora: %s  Epoch: %lu",
             timeClient.getFormattedTime().c_str(), timeClient.getEpochTime());
    logOK("NTP", buf);
    ok_ntp = true;
  }
  separador();

  // ── HIVEMQ TLS ────────────────────────────────────────────
  Serial.println("[ PASO 7 ] Conectando a HiveMQ Cloud (TLS)...");
  Serial.printf("           Host: %s\n", HIVEMQ_HOST);
  Serial.printf("           Puerto: %d  Usuario: %s\n", HIVEMQ_PORT, HIVEMQ_USER);

  secureClient.setInsecure();
  mqttHiveMQ.setServer(HIVEMQ_HOST, HIVEMQ_PORT);
  mqttHiveMQ.setBufferSize(512);

  String clientId = "ESP32_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.printf("           Client ID: %s\n", clientId.c_str());

  int intentosMQ = 0;
  while (!mqttHiveMQ.connect(clientId.c_str(), HIVEMQ_USER, HIVEMQ_PASS) && intentosMQ < 3) {
    int estado = mqttHiveMQ.state();
    Serial.printf("           Intento %d fallido — estado MQTT: %d", intentosMQ+1, estado);
    // Explicar el código de error
    switch(estado) {
      case -4: Serial.println(" (TIMEOUT — no hay respuesta del broker)"); break;
      case -3: Serial.println(" (CONEXION PERDIDA)"); break;
      case -2: Serial.println(" (FALLO DE CONEXION — posible TLS)"); break;
      case -1: Serial.println(" (DESCONECTADO)"); break;
      case  1: Serial.println(" (BAD PROTOCOL)"); break;
      case  2: Serial.println(" (CLIENT ID rechazado)"); break;
      case  3: Serial.println(" (BROKER NO DISPONIBLE)"); break;
      case  4: Serial.println(" (USUARIO O CONTRASEÑA INCORRECTOS ← revisa credenciales HiveMQ)"); break;
      case  5: Serial.println(" (NO AUTORIZADO)"); break;
      default: Serial.println();
    }
    intentosMQ++;
    delay(3000);
  }

  if (!mqttHiveMQ.connected()) {
    logERR("HiveMQ", "No se pudo conectar al broker MQTT");
    Serial.println("           • Verifica usuario/contraseña en HiveMQ Access Management");
    Serial.println("           • Verifica que el cluster esté activo en hivemq.com");
    ok_hivemq = false;
  } else {
    logOK("HiveMQ", "Conectado con TLS en puerto 8883");
    mqttHiveMQ.subscribe("parking/config/1/cmd");
    ok_hivemq = true;
  }
  separador();

  // ── UBIDOTS ───────────────────────────────────────────────
  Serial.println("[ PASO 8 ] Conectando a Ubidots...");
  Serial.printf("           Host: %s  Puerto: %d\n", UBIDOTS_HOST, UBIDOTS_PORT);

  mqttUbidots.setServer(UBIDOTS_HOST, UBIDOTS_PORT);
  mqttUbidots.setBufferSize(512);

  String clientIdUbi = "ESP32_UBI_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  int intentosUbi = 0;
  while (!mqttUbidots.connect(clientIdUbi.c_str(), UBIDOTS_TOKEN, "") && intentosUbi < 3) {
    Serial.printf("           Intento %d fallido — estado: %d\n",
                  intentosUbi+1, mqttUbidots.state());
    intentosUbi++;
    delay(3000);
  }

  if (!mqttUbidots.connected()) {
    logERR("Ubidots", "No se pudo conectar — verifica el token UBIDOTS");
    ok_ubidots = false;
  } else {
    logOK("Ubidots", "Conectado correctamente");
    ok_ubidots = true;
  }
  separador();

  // ── RESUMEN FINAL ─────────────────────────────────────────
  imprimirResumen();

  arranque = millis();
  Serial.println("\n  Iniciando loop de publicación...\n");
}

// ════════════════════════════════════════════════════════════
//  RESUMEN DE DIAGNÓSTICO
// ════════════════════════════════════════════════════════════
void imprimirResumen() {
  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║          RESUMEN DE DIAGNÓSTICO            ║");
  Serial.println("╠════════════════════════════════════════════╣");
  Serial.printf( "║  WiFi     : %s    ║\n", ok_wifi    ? "✓ OK                         " : "✗ FALLO — revisa SSID/clave  ");
  Serial.printf( "║  NTP      : %s    ║\n", ok_ntp     ? "✓ OK                         " : "✗ FALLO — sin hora           ");
  Serial.printf( "║  HiveMQ   : %s    ║\n", ok_hivemq  ? "✓ OK (TLS 8883)              " : "✗ FALLO — revisa credenciales");
  Serial.printf( "║  Ubidots  : %s    ║\n", ok_ubidots ? "✓ OK                         " : "✗ FALLO — revisa token       ");
  Serial.printf( "║  DHT22    : %s    ║\n", ok_dht     ? "✓ OK                         " : "✗ FALLO — revisa GPIO4       ");
  Serial.printf( "║  MPU6050  : %s    ║\n", ok_mpu     ? "✓ OK                         " : "✗ FALLO — revisa SDA/SCL     ");
  Serial.printf( "║  HC-SR04  : %s    ║\n", ok_sr04    ? "✓ OK                         " : "✗ FALLO — revisa TRIG/ECHO   ");
  Serial.println("╚════════════════════════════════════════════╝");
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  if (!mqttHiveMQ.connected()) {
    logERR("HiveMQ", "Conexión perdida — reconectando...");
    conectarHiveMQ();
  }
  if (!mqttUbidots.connected()) {
    logERR("Ubidots", "Conexión perdida — reconectando...");
    conectarUbidots();
  }
  mqttHiveMQ.loop();
  mqttUbidots.loop();
  timeClient.update();

  if (millis() - ultimaPublicacion < INTERVALO_MS) return;
  ultimaPublicacion = millis();
  leerYPublicar();
}

// ════════════════════════════════════════════════════════════
//  LECTURA Y PUBLICACIÓN (con logs detallados)
// ════════════════════════════════════════════════════════════
void leerYPublicar() {
  Serial.println("\n──── Ciclo de lectura ────────────────────────");

  // HC-SR04
  int distCm = sonar.ping_cm();
  if (distCm == 0) distCm = 400;
  bool nuevaOcupacion = (distCm < DIST_OCUPADO_CM);
  if (nuevaOcupacion != plazaOcupada) {
    plazaOcupada = nuevaOcupacion;
    digitalWrite(LED_LIBRE,   plazaOcupada ? LOW  : HIGH);
    digitalWrite(LED_OCUPADO, plazaOcupada ? HIGH : LOW);
    Serial.printf("  [ESTADO] Plaza cambió a: %s\n", plazaOcupada ? "OCUPADA 🔴" : "LIBRE 🟢");
  }
  Serial.printf("  [SR04]   %d cm → %s\n", distCm, plazaOcupada ? "OCUPADA" : "LIBRE");

  // DHT22
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  if (isnan(temp)) {
    logERR("DHT22", "Lectura fallida en este ciclo");
  } else {
    Serial.printf("  [DHT22]  Temp=%.1f°C  Hum=%.1f%%\n", temp, hum);
  }

  // MPU6050
  mpu.update();
  float ax  = mpu.getAccX();
  float ay  = mpu.getAccY();
  float az  = mpu.getAccZ() - 1.0f;
  float mag = sqrt(ax*ax + ay*ay + az*az);
  Serial.printf("  [MPU]    X=%.2fg Y=%.2fg Z=%.2fg Mag=%.2fg\n", ax, ay, az, mag);
  if (mag > SISMO_G) {
    Serial.printf("  [⚠ SISMO] Magnitud %.2fg supera umbral %.1fg!\n", mag, SISMO_G);
  }

  // Publicar
  bool pubHive = publicarHiveMQ(distCm, temp, hum, mag);
  bool pubUbi  = publicarUbidots(distCm, temp, hum, mag);

  Serial.printf("  [MQTT]   HiveMQ: %s  Ubidots: %s\n",
                pubHive ? "✓ enviado" : "✗ falló",
                pubUbi  ? "✓ enviado" : "✗ falló");

  if (mag > SISMO_G) alertaSismica(ax, ay, az, mag);
}

// ════════════════════════════════════════════════════════════
//  PUBLICACIONES (retornan bool para saber si funcionó)
// ════════════════════════════════════════════════════════════
bool publicarHiveMQ(int dist, float temp, float hum, float mag) {
  if (!mqttHiveMQ.connected()) return false;

  char buf[256];
  StaticJsonDocument<256> doc;

  doc["occupied"] = plazaOcupada;
  doc["dist_cm"]  = dist;
  doc["ts"]       = timeClient.getEpochTime();
  serializeJson(doc, buf);
  mqttHiveMQ.publish("parking/spot/1/status", buf, true);

  if (!isnan(temp)) {
    doc.clear();
    doc["temp_c"]  = serialized(String(temp, 1));
    doc["hum_pct"] = serialized(String(hum,  1));
    doc["ts"]      = timeClient.getEpochTime();
    serializeJson(doc, buf);
    mqttHiveMQ.publish("parking/env/1/data", buf);
  }

  doc.clear();
  doc["status"]   = "ok";
  doc["uptime_s"] = (millis() - arranque) / 1000;
  doc["rssi"]     = WiFi.RSSI();
  doc["ip"]       = WiFi.localIP().toString();
  serializeJson(doc, buf);
  mqttHiveMQ.publish("parking/system/status", buf);

  return true;
}

bool publicarUbidots(int dist, float temp, float hum, float mag) {
  if (!mqttUbidots.connected()) return false;

  StaticJsonDocument<300> doc;
  char topic[64], payload[300];

  doc.createNestedArray("ocupada").createNestedObject()["value"]      = plazaOcupada ? 1 : 0;
  doc.createNestedArray("distancia_cm").createNestedObject()["value"] = dist;
  doc.createNestedArray("vibracion_g").createNestedObject()["value"]  = serialized(String(mag, 2));
  if (!isnan(temp)) {
    doc.createNestedArray("temperatura").createNestedObject()["value"] = serialized(String(temp, 1));
    doc.createNestedArray("humedad").createNestedObject()["value"]     = serialized(String(hum,  1));
  }

  snprintf(topic, sizeof(topic), "/v1.6/devices/%s", DEVICE_LABEL);
  serializeJson(doc, payload);
  return mqttUbidots.publish(topic, payload);
}

void alertaSismica(float ax, float ay, float az, float mag) {
  char buf[200];
  StaticJsonDocument<200> doc;
  doc["acc_x"] = serialized(String(ax, 2));
  doc["acc_y"] = serialized(String(ay, 2));
  doc["acc_z"] = serialized(String(az, 2));
  doc["magnitude"] = serialized(String(mag, 2));
  doc["ts"] = timeClient.getEpochTime();
  serializeJson(doc, buf);
  mqttHiveMQ.publish("parking/seismic/1/alert", buf);

  char topic[64], payload[80];
  snprintf(topic,   sizeof(topic),   "/v1.6/devices/%s", DEVICE_LABEL);
  snprintf(payload, sizeof(payload), "{\"alerta_sismica\":[{\"value\":%.2f}]}", mag);
  mqttUbidots.publish(topic, payload);

  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_OCUPADO, HIGH); delay(150);
    digitalWrite(LED_OCUPADO, LOW);  delay(150);
  }
}

// ════════════════════════════════════════════════════════════
//  RECONEXIONES
// ════════════════════════════════════════════════════════════
void conectarHiveMQ() {
  String clientId = "ESP32_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqttHiveMQ.connect(clientId.c_str(), HIVEMQ_USER, HIVEMQ_PASS)) {
    logOK("HiveMQ", "Reconectado");
    mqttHiveMQ.subscribe("parking/config/1/cmd");
  } else {
    Serial.printf("  [HiveMQ] Reconexión falló — estado: %d\n", mqttHiveMQ.state());
  }
}

void conectarUbidots() {
  String clientId = "ESP32_UBI_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqttUbidots.connect(clientId.c_str(), UBIDOTS_TOKEN, "")) {
    logOK("Ubidots", "Reconectado");
  } else {
    Serial.printf("  [Ubidots] Reconexión falló — estado: %d\n", mqttUbidots.state());
  }
}
