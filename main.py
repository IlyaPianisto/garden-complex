import logging
import asyncio
import json
import os
from dotenv import load_dotenv
from datetime import datetime
from telegram import Update, InlineKeyboardButton, InlineKeyboardMarkup, ReplyKeyboardMarkup
from telegram.ext import Application, CommandHandler, ContextTypes, CallbackQueryHandler, MessageHandler, filters
import paho.mqtt.client as mqtt
import database as db

# Настройки
load_dotenv()
TOKEN = os.getenv("TOKEN")
MQTT_BROKER = os.getenv("MQTT_BROKER")
MQTT_PORT = int(os.getenv("MQTT_PORT"))
MQTT_TOPIC_SUBSCRIBE = os.getenv("MQTT_TOPIC_SUBSCRIBE")
TREATMENTS_FILE = "treatments.json"

# Логирование
logging.basicConfig(format="%(asctime)s - %(name)s - %(levelname)s - %(message)s", level=logging.INFO)
logger = logging.getLogger(__name__)

# Глобальные переменные
mqtt_client = None

def load_treatments() -> dict:
    if not os.path.exists(TREATMENTS_FILE):
        logger.warning(f"ФАЙЛ ОБРАБОТОК ({TREATMENTS_FILE}) НЕ НАЙДЕН!!!")
        return {}
    try:
        with open(TREATMENTS_FILE, "r", encoding='utf8') as f:
            return json.load(f)
    except Exception as e:
        logger.error(f"ERROR!! Ошибка загрузки {TREATMENTS_FILE}: {e}")
        return {}

treatments = load_treatments()

# Маппинг месяцев
MONTHS_RU = {
    1: "Январь", 2: "Февраль", 3: "Март", 4: "Апрель", 5: "Май", 6: "Июнь",
    7: "Июль", 8: "Август", 9: "Сентябрь", 10: "Октябрь", 11: "Ноябрь", 12: "Декабрь"
}

user_states: dict = {}

# ВСЕ НЕОБХОДИМЫЕ ФУНКЦИИ

def init_user (chat_id: str) -> str:
    str_id = str(chat_id)
    db.get_or_create_user(str_id)
    if str_id not in user_states:
        user_states[str_id] = {
            'active_system_id' : None,
            'pump_states' : {i : False for i in range(1, 9)},
            'calibrating' : None,
            'awaiting_input' : None,
            'pending_treatment' : None,
        }

    return str_id

def get_active_system(str_id: str) -> dict | None:
    system_id = user_states[str_id].get(['active_system_id'])
    if system_id is None:
        return None

    return db.get_system(system_id)

def format_sensor_snapshot(cash: dict) -> str:
    return json.dumps({
        "wind": cash.get("wind"),
        "light": cash.get("light"),
        "temp": cash.get("temp"),
        "humidity": cash.get("humidity"),
    }, ensure_ascii=False)

def publish_command(chat_id, sys_id, command):
    topic = f"app/{chat_id}/{sys_id}/control"
    if mqtt_client:
        mqtt_client.publish(topic, command)
        logger.info(f"MQTT OUT {topic}: {command}")

def on_connect(client):
    logger.info("Connected to MQTT")
    client.subscribe(MQTT_TOPIC_SUBSCRIBE)

def on_message(msg):
    try:
        payload = msg.payload.decode()
        parts = msg.topic.split('/')
        owner_id = parts[1]

        if owner_id not in user_states:
            return

        state = user_states[owner_id]

        if payload.startswith("VETER:"):
            val = float(payload.split(":")[1])
            db.update_sensor_cash(owner_id, "wind", val)

        elif payload.startswith("POT:"):
            val = float(payload.split(":")[1])
            db.update_sensor_cash(owner_id, "light", val)

        elif payload.startswith("BME:"):
            bme_parts = payload[4].split("|")
            for part in bme_parts:
                if part.startswith("T:"):
                    db.update_sensor_cash(owner_id, "temp", float(part[2:]))
                elif part.startswith("H:"):
                    db.update_sensor_cash(owner_id, "humidity", float(part[2:]))

        elif payload.startswith("NASOS:"):
            parts = payload.split(":")
        if len(parts) >= 3:
            p_num = int(parts[1])
            state = (parts[2] == "ON")
            user_states[owner_id]["pump_states"][p_num] = state

    except Exception as e:
        logger.error(f"MQTT Parse error: {e}")

# МЕНЮ!

def get_main_menu():
    kb = [
        [InlineKeyboardButton("Обработка деревьев", callback_data="menu:treatment_auto")], # проверять!
        [InlineKeyboardButton("Настройки", callback_data="menu:settings")]
    ]
    return InlineKeyboardMarkup(kb)

async def start(update: Update):
    init_user(update.effective_chat.id)
    await update.message.reply_text("Система управления садом.", reply_markup=get)

async def text_input_handler(update: Update):
        str_id = str(update.effective_chat.id)
        state = user_states[str_id]
        text = update.message.text.strip()
        waiting = state.get('awaiting_input')

        if waiting == "sys_name":
            pending = state.get("pending_sys", {})
            sys_id_num = pending.get("sys_id")
            result = db.add_system(str_id, sys_id_num, name=text)
            state['awaiting_input'] = None
            state['pending_sys'] = None

            if result:
                state['active_system_id'] = result["id"]
                await update.message.reply_text(f"Система '{text}' добавлена и выбрана как активная")
