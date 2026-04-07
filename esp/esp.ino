#include <FS.h>                   // Для работы с файловой системой
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <WiFiManager.h>          
#include <ArduinoJson.h>          // !!! УСТАНОВИТЕ ЭТУ БИБЛИОТЕКУ !!!

// --- НАСТРОЙКИ ТОЧКИ ДОСТУПА ---
const char* ap_name = "ESP_SETUP_SECRET";
const char* ap_pass = "admin12345";

// --- НАСТРОЙКИ MQTT ---
const char* mqtt_broker = "broker.hivemq.com";
const int mqtt_port = 1883;

// ПЕРЕМЕННЫЕ (Дефолтные значения)
char telegram_id[20] = "00000000"; 
char system_id[5] = "1";

// Топики
char mqtt_topic_control[100];
char mqtt_topic_sensors[100];

// Флаг для сохранения
bool shouldSaveConfig = false;

WiFiClient espClient;
PubSubClient client(espClient);
SoftwareSerial unoSerial(D5, D6); // D5=RX, D6=TX

// Callback уведомляющий, что нужно сохранить настройки
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void buildTopics() {
  sprintf(mqtt_topic_control, "app/%s/%s/control", telegram_id, system_id);
  sprintf(mqtt_topic_sensors, "app/%s/%s/sensors", telegram_id, system_id);
  Serial.print("CONTROL Topic: "); Serial.println(mqtt_topic_control);
  Serial.print("SENSORS Topic: "); Serial.println(mqtt_topic_sensors);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("MQTT In: "); Serial.println(message);

  // --- НОВАЯ ЛОГИКА СБРОСА ---
  if (message == "CMD_RESET_CONF") {
    Serial.println("!!! RECEIVED RESET COMMAND !!!");
    
    // 1. Удаляем файл с ID и номером системы
    if (SPIFFS.begin()) {
      SPIFFS.remove("/config.json");
      Serial.println("Config file deleted.");
    }
    
    // 2. Сбрасываем настройки WiFiManager (SSID/Password)
    WiFiManager wm;
    wm.resetSettings();
    Serial.println("WiFi settings cleared.");
    
    // 3. Перезагрузка
    Serial.println("Rebooting...");
    delay(1000);
    ESP.restart();
    return; // Выходим, чтобы не отправлять это в Arduino
  }
  // -----------------------------
  
  // Если это не команда сброса, шлем дальше на Arduino
  unoSerial.println(message);
}

void setup() {
  Serial.begin(115200);
  unoSerial.begin(9600);
  Serial.println("\n Starting...");

  

  // 1. Монтируем файловую систему и читаем конфиг
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      // Файл есть, читаем и парсим
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        auto error = deserializeJson(json, buf.get());
        if (!error) {
          Serial.println("\nparsed json");
          // Копируем данные из файла в переменные
          strcpy(telegram_id, json["telegram_id"]);
          strcpy(system_id, json["system_id"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  // 2. Настройка WiFiManager
  WiFiManager wifiManager;
  
  //wifiManager.resetSettings();
  
  // Коллбек сохранения
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Параметры меню (передаем текущие значения переменных как дефолт)
  WiFiManagerParameter custom_tg_id("tgid", "Telegram User ID", telegram_id, 20);
  WiFiManagerParameter custom_sys_id("sysid", "System Number", system_id, 4);

  wifiManager.addParameter(&custom_tg_id);
  wifiManager.addParameter(&custom_sys_id);

  // Чтобы сбросить настройки WiFi для теста, раскомментируйте строку ниже:
  //wifiManager.resetSettings();

  if (!wifiManager.autoConnect(ap_name, ap_pass)) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

  // 3. Сохранение (если пользователь менял настройки)
  strcpy(telegram_id, custom_tg_id.getValue());
  strcpy(system_id, custom_sys_id.getValue());

  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["telegram_id"] = telegram_id;
    json["system_id"] = system_id;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }
    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
  }

  Serial.println("\nlocal ip");
  Serial.println(WiFi.localIP());

  // 4. Запуск MQTT
  buildTopics();
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Подписка на точный топик
      client.subscribe(mqtt_topic_control);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

void checkUno() {
  if (unoSerial.available() > 0) {
    String reply = unoSerial.readStringUntil('\n');
    reply.trim();
    if (reply.length() > 0) {
      Serial.print("Arduino says: "); Serial.println(reply);
      // Публикуем ответ в Telegram
      client.publish(mqtt_topic_sensors, reply.c_str());
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  checkUno();
}