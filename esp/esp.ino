#include <FS.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <WiFiManager.h>          
#include <ArduinoJson.h>

const char* ap_name = "ESP_SETUP_SECRET";
const char* ap_pass = "admin12345";

const char* mqtt_broker = "35.172.255.228";
const int mqtt_port = 1883;

char telegram_id[64] = "00000000"; 
char system_id[16] = "1";

char mqtt_topic_control[128];
char mqtt_topic_sensors[128];

bool shouldSaveConfig = false;

WiFiClient espClient;
PubSubClient client(espClient);
SoftwareSerial unoSerial(D5, D6); // D5=RX, D6=TX

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void buildTopics() {
  sprintf(mqtt_topic_control, "Acpp-garden-Complexx/%s/%s/control", telegram_id, system_id);
  sprintf(mqtt_topic_sensors, "Acpp-garden-Complexx/%s/%s/sensors", telegram_id, system_id);
  Serial.print("CONTROL Topic: "); Serial.println(mqtt_topic_control);
  Serial.print("SENSORS Topic: "); Serial.println(mqtt_topic_sensors);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("MQTT In: "); Serial.println(message);

  if (message == "CMD_RESET_CONF") {
    Serial.println("!!! RECEIVED RESET COMMAND !!!");

    if (SPIFFS.begin()) {
      SPIFFS.remove("/config.json");
      Serial.println("Config file deleted.");
    }

    WiFiManager wm;
    wm.resetSettings();
    Serial.println("WiFi settings cleared.");

    Serial.println("Rebooting...");
    delay(1000);
    ESP.restart();
    return;
  }

  unoSerial.println(message);
}

void setup() {

  Serial.begin(115200);
  unoSerial.begin(9600);
  Serial.println("\n Starting...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
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

  WiFiManager wifiManager;

  pinMode(D8, INPUT_PULLUP);

  if (digitalRead(D7) == LOW) {
    wifiManager.resetSettings();
    Serial.println("СБРОС настроек WIFI!!!");
  }

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_tg_id("tgid", "Telegram User ID", telegram_id, 64);
  WiFiManagerParameter custom_sys_id("sysid", "System Number", system_id, 16);

  wifiManager.addParameter(&custom_tg_id);
  wifiManager.addParameter(&custom_sys_id);

  if (!wifiManager.autoConnect(ap_name, ap_pass)) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

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

  buildTopics();
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512);
  client.setKeepAlive(60);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(mqtt_topic_control);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

void checkUno() {
  static String inputBuffer = "";
  const size_t MAX_BUFFER_SIZE = 256;

  while (unoSerial.available() > 0) {
    char c = unoSerial.read();
    if (c == '\n'){
      inputBuffer.trim();
      if (inputBuffer.length() > 0) {
        Serial.print("Arduino says: "); 
        Serial.println(inputBuffer);

        if (client.connected()){
          client.publish(mqtt_topic_sensors, inputBuffer.c_str());
        }
      }
      inputBuffer = "";
    }
    else {
      inputBuffer += c;

      if (inputBuffer.length() > MAX_BUFFER_SIZE) {
        Serial.println("Error: Buffer overflow, clearing...");
        inputBuffer = ""; 
      }
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