# 🚗 Smart Parking IoT — Monitoreo Integral y Alertas Sísmicas

Prototipo funcional de parqueadero inteligente con ESP32 que monitorea disponibilidad de plaza, condiciones ambientales y detecta eventos sísmicos en tiempo real.

---

## 📋 Tabla de Contenidos
- [Descripción](#descripción)
- [Arquitectura](#arquitectura)
- [Diagrama de Secuencia](#diagrama-de-secuencia)
- [Hardware y Conexiones](#hardware-y-conexiones)
- [Temas MQTT](#temas-mqtt)
- [Endpoints API](#endpoints-api)
- [Librerías Utilizadas](#librerías-utilizadas)
- [Uso de Memoria](#uso-de-memoria)
- [Limitaciones](#limitaciones)
- [Posibilidades de Mejora](#posibilidades-de-mejora)
- [Configuración y Despliegue](#configuración-y-despliegue)

---

## Descripción

El sistema detecta si un puesto de parqueadero está **libre u ocupado** mediante un sensor ultrasónico, monitorea **temperatura y humedad** con un DHT22, y actúa como **estación de alerta temprana sísmica** usando un acelerómetro MPU6050. Toda la información se publica por **MQTT con cifrado TLS** a HiveMQ Cloud y se visualiza en un dashboard de **Ubidots**.

**Usuarios objetivo:** administradores de parqueaderos y propietarios de vehículos.

---

## Arquitectura

```
[HC-SR04]  ─────┐
[DHT22]    ─────┤──► [ESP32] ──MQTT TLS:8883──► [HiveMQ Cloud] ──► [Ubidots Dashboard]
[MPU6050]  ─────┘       │
                         └──NTP──► pool.ntp.org
```

### Diagrama de Bloques

| Capa | Componente | Protocolo |
|------|-----------|-----------|
| Física | HC-SR04, DHT22, MPU6050, LEDs | I²C / Digital |
| MCU | ESP32 WROOM-32 | WiFi 2.4 GHz |
| Broker | HiveMQ Cloud Serverless | MQTT TLS puerto 8883 |
| Cloud | Ubidots STEM | MQTT puerto 1883 |
| Sincronización | pool.ntp.org | NTP UDP puerto 123 |

---

## Diagrama de Secuencia

```
Sensores        ESP32            NTP Server       HiveMQ (TLS)      Ubidots
   │               │                  │                 │               │
   │──lectura──►   │                  │                 │               │
   │               │──UDP request────►│                 │               │
   │               │◄─timestamp───────│                 │               │
   │               │──CONNECT TLS────────────────────►  │               │
   │               │◄─CONNACK────────────────────────── │               │
   │               │──PUBLISH parking/spot/1/status────►│               │
   │               │──PUBLISH parking/env/1/data───────►│               │
   │               │──PUBLISH parking/system/status────►│               │
   │               │──PUBLISH /v1.6/devices/smart-parking-1────────────►│
   │               │                  │                 │               │
   │  (si sismo)   │                  │                 │               │
   │               │──PUBLISH parking/seismic/1/alert──►│               │
   │               │──PUBLISH alerta_sismica────────────────────────────►│
```

---

## Hardware y Conexiones

### Lista de Componentes

| Componente | Cantidad | Costo estimado |
|-----------|----------|---------------|
| ESP32 DevKit v1 | 1 | $30.000 COP |
| HC-SR04 (ultrasónico) | 1 | $8.000 COP |
| DHT22 (temp/hum) | 1 | $15.000 COP |
| MPU6050 (acelerómetro) | 1 | $10.000 COP |
| LED verde + LED rojo | 2 | $2.000 COP |
| Resistencias 220Ω (LEDs) | 2 | $500 COP |
| Resistencia 10kΩ (DHT22 pull-up) | 1 | $300 COP |
| Protoboard + cables | 1 | $12.000 COP |
| Fuente USB 5V | 1 | $10.000 COP |
| **Total** | | **~$87.800 COP** |

### Tabla de Pines

| Sensor | Pin Sensor | Pin ESP32 | Color cable sugerido |
|--------|-----------|-----------|---------------------|
| HC-SR04 | VCC | 3.3V | Rojo |
| HC-SR04 | GND | GND | Negro |
| HC-SR04 | TRIG | GPIO 5 | Azul |
| HC-SR04 | ECHO | GPIO 18 | Celeste |
| DHT22 | VCC | 3.3V | Rojo |
| DHT22 | GND | GND | Negro |
| DHT22 | DATA | GPIO 4 | Verde |
| MPU6050 | VCC | 3.3V | Rojo |
| MPU6050 | GND | GND | Negro |
| MPU6050 | SDA | GPIO 21 | Naranja |
| MPU6050 | SCL | GPIO 22 | Amarillo |
| LED Verde | Ánodo (+) | GPIO 2 → 220Ω | Verde |
| LED Rojo | Ánodo (+) | GPIO 15 → 220Ω | Rojo |
| Ambos LEDs | Cátodo (−) | GND | Negro |

> ⚠️ **Nota HC-SR04:** Si tienes el modelo estándar de 5V, coloca un divisor resistivo (1kΩ + 2kΩ) en el pin ECHO para bajar la señal de 5V a 3.3V y no dañar el GPIO del ESP32. El modelo "HC-SR04+" ya opera a 3.3V de forma nativa.

> ⚠️ **Nota DHT22:** Conectar resistencia pull-up de 10kΩ entre el pin DATA y 3.3V. Sin ella el sensor no funciona de forma estable.

---

## Temas MQTT

El broker utilizado es **HiveMQ Cloud** con TLS en el puerto **8883**.

| Topic | Tipo | Payload (JSON) | Descripción |
|-------|------|---------------|-------------|
| `parking/spot/1/status` | PUBLICA | `{"occupied": true, "dist_cm": 12, "ts": 1716000000}` | Estado de ocupación de la plaza |
| `parking/env/1/data` | PUBLICA | `{"temp_c": "24.5", "hum_pct": "60.2", "ts": 1716000000}` | Datos ambientales del DHT22 |
| `parking/seismic/1/alert` | PUBLICA | `{"acc_x": "0.12", "acc_y": "0.08", "acc_z": "1.51", "magnitude": "1.52", "ts": 1716000000}` | Alerta sísmica detectada por MPU6050 |
| `parking/system/status` | PUBLICA | `{"status": "ok", "uptime_s": 3600, "rssi": -65, "ip": "192.168.1.10"}` | Healthcheck del sistema |
| `parking/config/1/cmd` | SUSCRIBE | `{"threshold": 15, "interval": 5, "seismic_g": 1.5}` | Comandos de configuración remotos |

### Seguridad
- Conexión cifrada **TLS 1.2** en puerto **8883**
- Autenticación con usuario y contraseña
- `WiFiClientSecure` con `setInsecure()` para HiveMQ Cloud Serverless (certificado Let's Encrypt confiable)

---

## Endpoints API

El ESP32 expone un servidor HTTP en el **puerto 80** de su IP local.

### `GET /api/health`
Verifica que el dispositivo está activo.

**Respuesta 200 OK:**
```json
{
  "status": "ok",
  "device": "ESP32_PARKING_1",
  "uptime_s": 3600,
  "rssi_dbm": -65,
  "ip": "192.168.1.10",
  "mqtt": "connected",
  "ntp_epoch": 1716000000
}
```

### `GET /api/parking/spots`
Retorna el estado actual de todas las plazas.

**Respuesta 200 OK:**
```json
[
  {
    "id": 1,
    "occupied": false,
    "timestamp": "1716000000"
  }
]
```

### `POST /api/config/threshold`
Actualiza el umbral de distancia para detección de ocupación.

**Payload:**
```json
{ "dist_cm": 20 }
```

**Respuesta 201:**
```json
{ "updated": true }
```

---

## Librerías Utilizadas

| Librería | Autor | Versión | Uso |
|---------|-------|---------|-----|
| PubSubClient | Nick O'Leary | 2.8.0 | Cliente MQTT para ESP32 |
| DHT sensor library | Adafruit | 1.4.6 | Lectura sensor DHT22 |
| Adafruit Unified Sensor | Adafruit | 1.1.9 | Dependencia de DHT |
| MPU6050_light | rfetick | 1.1.0 | Acelerómetro/giroscopio I²C |
| ArduinoJson | Benoit Blanchon | 6.21.5 | Serialización JSON de payloads |
| NewPing | Tim Eckel | 1.9.7 | Sensor ultrasónico HC-SR04 |
| NTPClient | Fabrice Weinberg | 3.2.1 | Sincronización de tiempo NTP |
| WiFiClientSecure | ESP32 Arduino | built-in | TLS sobre WiFi |
| WebServer | ESP32 Arduino | built-in | Endpoints HTTP healthcheck |

---

## Uso de Memoria

Datos generados por el IDE Arduino al verificar/cargar el sketch:

| Memoria | Usado | Total | Porcentaje |
|---------|-------|-------|-----------|
| Flash (programa) | ~812 KB | 4 MB | ~62% |
| RAM (SRAM) | ~234 KB | 520 KB | ~45% |

> Los valores exactos se actualizan con cada compilación. El IDE muestra la línea:
> `Sketch uses XXXXX bytes (XX%) of program storage space.`

---

## Limitaciones

- **Un solo puesto:** el prototipo monitorea únicamente 1 plaza de parqueadero.
- **Dependencia WiFi:** requiere red WiFi 2.4 GHz estable; sin ella no hay conectividad.
- **Sin batería:** diseñado para operar conectado a la red eléctrica (~240 mA con WiFi activo).
- **Latencia MQTT:** entre 100–300 ms dependiendo de la calidad de la red.
- **Calibración manual del MPU6050:** el umbral de detección sísmica (1.5g) fue establecido empíricamente y puede requerir ajuste según el entorno.
- **Plan gratuito HiveMQ:** límite de conexiones simultáneas y retención de mensajes.
- **Sin persistencia local:** si el ESP32 pierde conexión, los datos de ese período se pierden.

---

## Posibilidades de Mejora

- **Escalar a múltiples plazas:** replicar el sistema con varios ESP32 y un dashboard unificado.
- **Modo deep-sleep:** reducir consumo energético para operación con batería/panel solar.
- **Cámara OV2640:** reconocimiento de placas vehiculares con ESP32-CAM.
- **Integración de pagos:** pasarela para cobro automático por tiempo de parqueo.
- **Machine Learning en edge:** modelo TinyML para distinguir sismos de vibraciones de vehículos con mayor precisión.
- **App móvil nativa:** notificaciones push en tiempo real al propietario del vehículo.
- **Redundancia de red:** fallback a datos móviles con módulo SIM800L.
- **Base de datos histórica:** almacenar series de tiempo en InfluxDB o Firebase para análisis de patrones.

---

## Configuración y Despliegue

### 1. Cuentas necesarias (gratuitas)
- **HiveMQ Cloud:** [hivemq.com](https://www.hivemq.com/mqtt-cloud-broker/) → crear cluster Serverless → crear credenciales en *Access Management*
- **Ubidots STEM:** [ubidots.com](https://ubidots.com/) → obtener token en *API Credentials*

### 2. Librerías (Arduino IDE → Library Manager)
Instalar todas las listadas en la sección [Librerías Utilizadas](#librerías-utilizadas).

### 3. Configurar credenciales en el sketch
Editar las líneas al inicio de `smart_parking_LISTO.ino`:
```cpp
const char* WIFI_SSID     = "TU_RED";
const char* WIFI_PASSWORD = "TU_CLAVE";
const char* HIVEMQ_HOST   = "xxxx.s1.eu.hivemq.cloud";
const char* HIVEMQ_USER   = "tu_usuario";
const char* HIVEMQ_PASS   = "tu_contraseña";
const char* UBIDOTS_TOKEN = "BBFF-...";
```

### 4. Cargar al ESP32
- Placa: **ESP32 Dev Module**
- Upload Speed: 115200
- Monitor Serial: 115200 baudios

### 5. Verificar en Ubidots
Ir a **Devices → smart-parking-1** para ver las variables en tiempo real y construir el dashboard.

---

## Plataformas Cloud

| Plataforma | Uso | Plan | URL |
|-----------|-----|------|-----|
| HiveMQ Cloud | Broker MQTT (TLS) | Serverless Free | hivemq.com |
| Ubidots | Dashboard IoT | STEM Free | ubidots.com |

