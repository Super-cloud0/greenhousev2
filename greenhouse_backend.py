"""
Smart Greenhouse Automation Server
- Подключается к MQTT (Adafruit IO)
- Получает и парсит телеметрию с ESP32
- Обрабатывает системные команды и отправляет статус/логи обратно на ESP32
"""

import paho.mqtt.client as mqtt
import time
import os
from datetime import datetime

# ======================= НАСТРОЙКИ (ЧЕРЕЗ ENV ИЛИ ПЛЕЙСХОЛДЕРЫ) =======================
MQTT_HOST     = "io.adafruit.com"
MQTT_PORT     = 1883
MQTT_USERNAME = os.environ.get("MQTT_USERNAME", "YOUR_MQTT_USERNAME")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "YOUR_MQTT_SECRET_KEY")

DATA_TOPIC    = "brobropups/feeds/smartgarden_data"
CONTROL_TOPIC = "brobropups/feeds/smartgarden_control"
LOG_TOPIC     = "brobropups/feeds/smartgarden_log"

# ======================= ТЕКУЩЕЕ СОСТОЯНИЕ СИСТЕМЫ =======================
sensor_data = {
    "soil": 0,
    "temp": 0.0,
    "hum": 0.0,
    "pump": 0,
    "updated": "never"
}

def generate_status_report(lang: str = "EN") -> str:
    """Генерирует строгое шаблонное сообщение о состоянии системы."""
    pump_status = "ON" if sensor_data["pump"] else "OFF"
    
    if lang == "RU":
        # Транслит для вывода на TFT_eSPI без поддержки кириллицы
        soil_alert = "VSE OK" if sensor_data["soil"] >= 30 else "SUKHO! NUZHEN POLIV"
        report = f"STATION OK. Temp:{sensor_data['temp']}C, Vlazhnost:{sensor_data['hum']}%, Pochva:{sensor_data['soil']}%. Nasos:{pump_status}. Status: {soil_alert}"
    else:
        soil_alert = "MOIST" if sensor_data["soil"] >= 30 else "DRY! NEED WATER"
        report = f"STATION OK. Temp:{sensor_data['temp']}C, Hum:{sensor_data['hum']}%, Soil:{sensor_data['soil']}%. Pump:{pump_status}. Status: {soil_alert}"
        
    return report[:200]  # Ограничение под буфер экрана ESP32

# ======================= ОБРАБОТЧИКИ MQTT =======================
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ Connected to MQTT broker")
        client.subscribe(DATA_TOPIC)
        client.subscribe(CONTROL_TOPIC)
        print(f"📡 Subscribed to: {DATA_TOPIC}")
        print(f"📡 Subscribed to: {CONTROL_TOPIC}")
    else:
        print(f"❌ MQTT connection failed: rc={rc}")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode("utf-8").strip()
    print(f"📨 [{topic}] {payload}")

    # 1. Сбор телеметрии с датчиков
    if topic == DATA_TOPIC:
        try:
            parts = payload.split(",")
            sensor_data["soil"]    = int(parts[0])
            sensor_data["temp"]    = float(parts[1])
            sensor_data["hum"]     = float(parts[2])
            sensor_data["pump"]    = int(parts[3])
            sensor_data["updated"] = datetime.now().strftime("%H:%M")
            print(f"🌱 Soil:{sensor_data['soil']}% Temp:{sensor_data['temp']}°C Hum:{sensor_data['hum']}% Pump:{'ON' if sensor_data['pump'] else 'OFF'}")
        except Exception as e:
            print(f"⚠️ Telemetry parse error: {e}")

    # 2. Обработка текстовых команд с терминала ESP32
    elif topic == CONTROL_TOPIC and payload.startswith("CMD:"):
        user_cmd = payload[4:].strip()
        print(f"💻 Received console command: {user_cmd}")
        
        # Эхо-ответ или простая логика процессинга команд
        echo_response = f"ACK: Command '{user_cmd}' received at {datetime.now().strftime('%H:%M:%S')}"
        client.publish(LOG_TOPIC, echo_response[:200])

    # 3. Запрос полного отчета о статусе (бывшая кнопка ANALYZE)
    elif topic == CONTROL_TOPIC and payload.startswith("GET_STATUS"):
        # Проверяем, запрашивает ли ESP32 русский транслит (например, если отправлено GET_STATUS:RU)
        lang = "RU" if "RU" in payload else "EN"
        print(f"📊 Generating system status report [{lang}]...")
        
        report = generate_status_report(lang)
        print(f"📝 Report output: {report}")
        
        client.publish(LOG_TOPIC, report)
        print(f"📤 Sent report to ESP32 via {LOG_TOPIC}")

def on_disconnect(client, userdata, rc):
    print(f"⚠️ Disconnected from MQTT (rc={rc}), reconnecting...")

# ======================= ТОЧКА ВХОДА =======================
def main():
    print("=" * 50)
    print(" Smart Greenhouse Automation Server")
    print("=" * 50)

    client = mqtt.Client(client_id="greenhouse_backend_py")
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.on_connect    = on_connect
    client.on_message    = on_message
    client.on_disconnect = on_disconnect

    while True:
        try:
            print(f"🔌 Connecting to {MQTT_HOST}...")
            client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
            client.loop_forever()
        except KeyboardInterrupt:
            print("\n👋 Server stopped manually.")
            break
        except Exception as e:
            print(f"❌ Connection error: {e}, retrying in 5 seconds...")
            time.sleep(5)

if __name__ == "__main__":
    main()
