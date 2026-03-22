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

# ВСЕ НЕОБХОДИМЫЕ ФУНКЦИИ

def init_user (chat_id: str) -> str:
    str_id = str(chat_id)
    db.get_or_create_user(str_id)
