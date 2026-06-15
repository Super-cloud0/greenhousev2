"""
Smart Greenhouse Telegram Bot
Команды:
/start - приветствие и меню
/status - текущие данные датчиков
/photo - запрос фото с камеры
/pump_on, /pump_off - управление помпой
/cmd <команда> - отправить консольную команду на сервер
/stream - ссылка на стрим камеры
/clear - очистить лог-экран
"""

import asyncio
import logging
import requests
import os
from datetime import datetime
from aiogram import Bot, Dispatcher, types, F
from aiogram.filters import Command, CommandStart
from aiogram.types import Message
from aiogram.utils.keyboard import InlineKeyboardBuilder
import paho.mqtt.client as mqtt
import threading

# ======================= НАСТРОЙКИ (ЧЕРЕЗ ENV ИЛИ ПЛЕЙСХОЛДЕРЫ) =======================
TELEGRAM_TOKEN = os.environ.get("TELEGRAM_TOKEN", "YOUR_TELEGRAM_BOT_TOKEN")
ALLOWED_USERS   = []  # Оставь пустым чтобы пускать всех, или добавь telegram user_id

MQTT_HOST     = "io.adafruit.com"
MQTT_PORT     = 1883
MQTT_USERNAME = os.environ.get("MQTT_USERNAME", "YOUR_MQTT_USERNAME")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "YOUR_MQTT_SECRET_KEY")

DATA_TOPIC    = "brobropups/feeds/smartgarden_data"
CONTROL_TOPIC = "brobropups/feeds/smartgarden_control"
LOG_TOPIC     = "brobropups/feeds/smartgarden_log"

CAM_IP = "192.168.137.207"  # IP ESP32-CAM

# ======================= СОСТОЯНИЕ =======================
sensor_data = {"soil": 0, "temp": 0.0, "hum": 0.0, "pump": 0, "updated": "never"}
last_log_message = "Нет системных логов."
mqtt_client_global = None

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger(__name__)

# ======================= КАМЕРА =======================
def get_cam_snapshot() -> bytes | None:
    try:
        resp = requests.get(f"http://{CAM_IP}/capture", timeout=5)
        if resp.status_code == 200:
            return resp.content
    except Exception as e:
        log.error(f"Camera error: {e}")
    return None

def get_cam_stream_url() -> str:
    return f"http://{CAM_IP}:81/stream"

# ======================= MQTT =======================
def mqtt_on_connect(client, userdata, flags, rc):
    if rc == 0:
        log.info("✅ TG Bot MQTT connected")
        client.subscribe(DATA_TOPIC)
        client.subscribe(LOG_TOPIC)
    else:
        log.error(f"❌ TG Bot MQTT connection failed: rc={rc}")

def mqtt_on_message(client, userdata, msg):
    global last_log_message
    topic = msg.topic
    payload = msg.payload.decode("utf-8").strip()

    if topic == DATA_TOPIC:
        try:
            parts = payload.split(",")
            sensor_data["soil"]    = int(parts[0])
            sensor_data["temp"]    = float(parts[1])
            sensor_data["hum"]     = float(parts[2])
            sensor_data["pump"]    = int(parts[3])
            sensor_data["updated"] = datetime.now().strftime("%H:%M:%S")
        except Exception as e:
            log.error(f"Error parsing data: {e}")

    elif topic == LOG_TOPIC:
        last_log_message = payload
        log.info(f"📋 New log from server: {payload}")

def start_mqtt():
    global mqtt_client_global
    client = mqtt.Client(client_id="tg_bot_greenhouse")
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.on_connect = mqtt_on_connect
    client.on_message = mqtt_on_message
    mqtt_client_global = client
    while True:
        try:
            client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
            client.loop_forever()
        except Exception as e:
            log.error(f"MQTT error: {e}, retry in 5s")
            import time; time.sleep(5)

# ======================= TELEGRAM BOT =======================
bot = Bot(token=TELEGRAM_TOKEN)
dp  = Dispatcher()
bot_loop = None

def check_user(user_id: int) -> bool:
    if not ALLOWED_USERS: return True
    return user_id in ALLOWED_USERS

def main_keyboard():
    kb = InlineKeyboardBuilder()
    kb.button(text="📊 Статус датчиков", callback_data="status")
    kb.button(text="📸 Снимок камеры", callback_data="photo")
    kb.button(text="🎥 Видеострим", callback_data="stream")
    kb.button(text="💧 Включить помпу", callback_data="pump_on")
    kb.button(text="⛔ Выключить помпу", callback_data="pump_off")
    kb.button(text=" Clarify 📝 Логи / Статус", callback_data="get_server_log")
    kb.adjust(2)
    return kb.as_markup()

@dp.message(CommandStart())
async def cmd_start(msg: Message):
    if not check_user(msg.from_user.id): return
    await msg.answer(
        "🌱 *Smart Greenhouse Control Panel*\n\n"
        "Система автоматизации и мониторинга теплицы.\n\n"
        "Используйте кнопки меню или команды:\n"
        "/status — данные телеметрии\n"
        "/photo — снимок с ESP32-CAM\n"
        "/stream — ссылка на трансляцию\n"
        "/pump\\_on, /pump\\_off — управление реле\n"
        "/cmd <сообщение> — отправить команду на сервер\n"
        "/clear — очистить экран терминала",
        parse_mode="Markdown",
        reply_markup=main_keyboard()
    )

@dp.message(Command("status"))
@dp.callback_query(F.data == "status")
async def cmd_status(event):
    msg = event.message if isinstance(event, types.CallbackQuery) else event
    if isinstance(event, types.CallbackQuery): await event.answer()
    if not check_user(msg.chat.id): return

    pump = "💧 РАБОТАЕТ" if sensor_data["pump"] else "⛔ ОТКЛЮЧЕНА"
    soil_emoji = "⚠️ СУХО" if sensor_data["soil"] < 30 else "🌿 ОК"
    
    text = (
        f"📊 *Телеметрия теплицы* ({sensor_data['updated']})\n\n"
        f"🪴 Влажность почвы: *{sensor_data['soil']}%* ({soil_emoji})\n"
        f"🌡 Температура воздуха: *{sensor_data['temp']}°C*\n"
        f"💨 Влажность воздуха: *{sensor_data['hum']}%*\n"
        f"⚙️ Состояние помпы: *{pump}*\n"
    )
    await msg.answer(text, parse_mode="Markdown", reply_markup=main_keyboard())

@dp.message(Command("photo"))
@dp.callback_query(F.data == "photo")
async def cmd_photo(event):
    msg = event.message if isinstance(event, types.CallbackQuery) else event
    if isinstance(event, types.CallbackQuery): await event.answer()
    if not check_user(msg.chat.id): return

    wait_msg = await msg.answer("📸 Запрашиваю кадр с ESP32-CAM...")
    photo_bytes = get_cam_snapshot()
    await bot.delete_message(msg.chat.id, wait_msg.message_id)

    if photo_bytes:
        await bot.send_photo(
            msg.chat.id,
            photo=types.BufferedInputFile(photo_bytes, filename="greenhouse.jpg"),
            caption=f"📸 Кадр от {datetime.now().strftime('%H:%M:%S')}\n"
                    f"🌱 Почва: {sensor_data['soil']}% | 🌡 {sensor_data['temp']}°C",
            reply_markup=main_keyboard()
        )
    else:
        await msg.answer(
            "❌ Камера недоступна.\n"
            f"Проверьте статус ESP32-CAM по адресу: `{CAM_IP}`",
            parse_mode="Markdown",
            reply_markup=main_keyboard()
        )

@dp.message(Command("stream"))
@dp.callback_query(F.data == "stream")
async def cmd_stream(event):
    msg = event.message if isinstance(event, types.CallbackQuery) else event
    if isinstance(event, types.CallbackQuery): await event.answer()
    if not check_user(msg.chat.id): return

    kb = InlineKeyboardBuilder()
    kb.button(text="🎥 Открыть MJPEG Стрим", url=get_cam_stream_url())
    await msg.answer(
        f"🎥 *Ссылка на видеопоток*\n\n"
        f"Адрес локального стрима:\n`{get_cam_stream_url()}`\n\n"
        f"_(Для просмотра вы должны находиться в одной WiFi сети с камерой)_",
        parse_mode="Markdown",
        reply_markup=kb.as_markup()
    )

@dp.message(Command("pump_on"))
@dp.callback_query(F.data == "pump_on")
async def cmd_pump_on(event):
    msg = event.message if isinstance(event, types.CallbackQuery) else event
    if isinstance(event, types.CallbackQuery): await event.answer()
    if not check_user(msg.chat.id): return
    
    if mqtt_client_global:
        mqtt_client_global.publish(CONTROL_TOPIC, "PUMP_ON")
        await msg.answer("💧 Сигнал на *ВКЛЮЧЕНИЕ* помпы отправлен.", parse_mode="Markdown", reply_markup=main_keyboard())
    else:
        await msg.answer("❌ Ошибка: нет подключения к шине MQTT", reply_markup=main_keyboard())

@dp.message(Command("pump_off"))
@dp.callback_query(F.data == "pump_off")
async def cmd_pump_off(event):
    msg = event.message if isinstance(event, types.CallbackQuery) else event
    if isinstance(event, types.CallbackQuery): await event.answer()
    if not check_user(msg.chat.id): return
    
    if mqtt_client_global:
        mqtt_client_global.publish(CONTROL_TOPIC, "PUMP_OFF")
        await msg.answer("⛔ Сигнал на *ВЫКЛЮЧЕНИЕ* помпы отправлен.", parse_mode="Markdown", reply_markup=main_keyboard())
    else:
        await msg.answer("❌ Ошибка: нет подключения к шине MQTT", reply_markup=main_keyboard())

@dp.callback_query(F.data == "get_server_log")
async def cb_get_server_log(cb: types.CallbackQuery):
    if not check_user(cb.from_user.id): return
    await cb.answer()
    
    if mqtt_client_global:
        # Запрашиваем свежий статус-транслит у бэкенда (например, GET_STATUS:RU)
        mqtt_client_global.publish(CONTROL_TOPIC, "GET_STATUS:RU")
        await asyncio.sleep(0.8) # Небольшая пауза, чтобы бэкенд успел ответить в лог-топик
        
        await cb.message.answer(f"📋 *Последнее системное уведомление:*\n`{last_log_message}`", parse_mode="Markdown", reply_markup=main_keyboard())
    else:
        await cb.message.answer("❌ Ошибка связи с сервером автоматизации.", reply_markup=main_keyboard())

@dp.message(Command("cmd"))
async def cmd_send_custom(msg: Message):
    if not check_user(msg.from_user.id): return
    command_text = msg.text.replace("/cmd", "").strip()
    if not command_text:
        await msg.answer("Укажите команду. Пример:\n`/cmd REBOOT_ESP`", parse_mode="Markdown")
        return
        
    if mqtt_client_global:
        mqtt_client_global.publish(CONTROL_TOPIC, f"CMD:{command_text}")
        await msg.answer(f"💻 Команда `CMD:{command_text}` отправлена в систему.", parse_mode="Markdown", reply_markup=main_keyboard())
    else:
        await msg.answer("❌ Ошибка: MQTT офлайн.")

@dp.message(Command("clear"))
async def cmd_clear(msg: Message):
    if not check_user(msg.from_user.id): return
    if mqtt_client_global:
        mqtt_client_global.publish(CONTROL_TOPIC, "CLEAR_CHAT")
        await msg.answer("🗑 Отправлена команда очистки дисплея.", reply_markup=main_keyboard())

# Любое свободное текстовое сообщение пересылается как CMD команда
@dp.message(F.text)
async def handle_free_text(msg: Message):
    if not check_user(msg.from_user.id): return
    if msg.text.startswith("/"): return
    
    if mqtt_client_global:
        mqtt_client_global.publish(CONTROL_TOPIC, f"CMD:{msg.text}")
        await msg.answer(f"💻 Текст обработан как консольная команда:\n`CMD:{msg.text}`", parse_mode="Markdown", reply_markup=main_keyboard())

# ======================= MAIN =======================
async def main():
    global bot_loop
    bot_loop = asyncio.get_event_loop()

    # Запуск MQTT-клиента в бэкграунд-потоке
    mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
    mqtt_thread.start()

    log.info("🤖 Telegram Bot Server starting...")
    await dp.start_polling(bot)

if __name__ == "__main__":
    asyncio.run(main())
