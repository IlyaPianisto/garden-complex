// --- Библиотеки ---
#include <Wire.h>                 // Для I2C 
#include <SoftwareSerial.h>
#include <Adafruit_Sensor.h>      // Требуется для библиотеки BME680
#include <Adafruit_BME680.h>

// --- Пины ---

// 1. Связь с ESP8266 (RX, TX) 
SoftwareSerial espSerial(12, 13); // Пин 10 = RX (к ESP D6), Пин 11 = TX (к ESP D5)

// 2. 8-ми канальный релюховый модуль.
#define Nasos_1 11
#define Nasos_2 10
#define Nasos_3 9
#define Nasos_4 8
#define Nasos_5 7
#define Nasos_6 6
#define Nasos_7 5 
#define Nasos_8 4 

// 3. Датчик BME680
Adafruit_BME680 bme; 

// 4. Фоторезистор
#define POT_PIN A0

// 5. Датчик холла для скорости ветра 
#define Veter_PIN 2

// --- Глобальные переменные для датчика ветра ---
volatile unsigned long veterPulseCount = 0;
unsigned long lastVeterCheck = 0;

// ВАЖНО: Этот фактор нужно откалибровать!
// Он переводит Гц (импульсы/сек) в м/с (или км/ч, как тебе удобнее).
// Сейчас стоит 1.0 (1 Гц = 1 м/с) - это просто заглушка.
const float VETER_FACTOR = 0.01884; 

// Эта функция вызывается АВТОМАТИЧЕСКИ при каждом импульсе с датчика Холла
void countVeterPulse() {
  veterPulseCount++;
}

void setup() {

  Serial.begin(9600);
  Serial.println("Nano-Helper запущен.");

  // Serial для связи с ESP
  espSerial.begin(9600);
  
  // Инициализация BME680
  if (!bme.begin()) {
    Serial.println("Ошибка BME680! Проверьте подключение.");
  } else {
    Serial.println("BME680 инициализирован.");
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);
  }
  
  pinMode(POT_PIN, INPUT);

  // Настраиваем пины реле и СРАЗУ ВЫКЛЮЧАЕМ ИХ
  // Большинство релейных модулей активны по НИЗКОМУ уровню (LOW = ВКЛ, HIGH = ВЫКЛ)
  pinMode(Nasos_1, OUTPUT); digitalWrite(Nasos_1, HIGH);
  pinMode(Nasos_2, OUTPUT); digitalWrite(Nasos_2, HIGH);
  pinMode(Nasos_3, OUTPUT); digitalWrite(Nasos_3, HIGH);
  pinMode(Nasos_4, OUTPUT); digitalWrite(Nasos_4, HIGH);
  pinMode(Nasos_5, OUTPUT); digitalWrite(Nasos_5, HIGH);
  pinMode(Nasos_6, OUTPUT); digitalWrite(Nasos_6, HIGH);
  pinMode(Nasos_7, OUTPUT); digitalWrite(Nasos_7, HIGH); 
  pinMode(Nasos_8, OUTPUT); digitalWrite(Nasos_8, HIGH);

  // Настройка датчика ветра ---
  pinMode(Veter_PIN, INPUT_PULLUP); 
  // Настраиваем прерывание 0 (это пин D2)
  // Вызывать 'countVeterPulse' при каждом RISING (передний фронт сигнала)
  attachInterrupt(digitalPinToInterrupt(Veter_PIN), countVeterPulse, RISING);
  lastVeterCheck = millis(); // Запоминаем время старта
}

void loop() {
  checkESPCommands();
}

void checkESPCommands() {
  if (espSerial.available() > 0) {
    String command = espSerial.readStringUntil('\n');
    command.trim();
    Serial.print("Получена команда от ESP: ");
    Serial.println(command);

    // --- Обработка команд ---

    if (command == "GET_ALL") {
      readAllBMEData();
    }
    else if (command == "GET_POT") {
      readResistor();
    }
    // Команда для датчика ветра ---
    else if (command == "GET_VETER") {
      readVeter();
    }
    // Команда для насосов ---
    // Ожидаем команду в формате "NASOS:Номер:Состояние"
    // Например: "NASOS:1:ON" или "NASOS:3:OFF"
    else if (command.startsWith("NASOS:")) {
      // "NASOS:1:ON"
      int firstColon = command.indexOf(':');      // Индекс 1-го ':' (в позиции 5)
      int secondColon = command.indexOf(':', firstColon + 1); // Индекс 2-го ':' (в позиции 7)

      if (firstColon != -1 && secondColon != -1) {
        // Вырезаем номер насоса (между 1-м и 2-м ':')
        String numStr = command.substring(firstColon + 1, secondColon);
        int pumpNum = numStr.toInt();
        
        // Вырезаем состояние (после 2-го ':')
        String stateStr = command.substring(secondColon + 1);
        
        // Вызываем функцию управления
        controlNasos(pumpNum, stateStr);
      }
    }
  }
}

// --- НОВАЯ ФУНКЦИЯ: Управление насосами ---
void controlNasos(int num, String state) {
  int pinToControl = -1;
  
  // Выбираем пин в зависимости от номера
  switch (num) {
    case 1: pinToControl = Nasos_1; break;
    case 2: pinToControl = Nasos_2; break;
    case 3: pinToControl = Nasos_3; break;
    case 4: pinToControl = Nasos_4; break;
    case 5: pinToControl = Nasos_5; break;
    case 6: pinToControl = Nasos_6; break;
    case 7: pinToControl = Nasos_7; break;
    case 8: pinToControl = Nasos_8; break;
    default:
      Serial.println("Неверный номер насоса!");
      espSerial.println("NASOS:Error:InvalidNumber");
      return;
  }

  // Устанавливаем состояние (помним, что HIGH = ВЫКЛ, LOW = ВКЛ)
  if (state == "ON") {
    digitalWrite(pinToControl, LOW);
    Serial.print("Насос "); Serial.print(num); Serial.println(" ВКЛЮЧЕН");
    // Отправляем подтверждение обратно в ESP
    espSerial.println("NASOS:" + String(num) + ":ON");
  } else if (state == "OFF") {
    digitalWrite(pinToControl, HIGH);
    Serial.print("Насос "); Serial.print(num); Serial.println(" ВЫКЛЮЧЕН");
    // Отправляем подтверждение обратно в ESP
    espSerial.println("NASOS:" + String(num) + ":OFF");
  } else {
    Serial.println("Неверная команда состояния (нужно ON или OFF)");
    espSerial.println("NASOS:Error:InvalidState");
  }
}


// --- НОВАЯ ФУНКЦИЯ: Чтение датчика ветра ---
void readVeter() {
  unsigned long duration = millis() - lastVeterCheck; // Сколько мс прошло с прошлой проверки
  
  // Безопасно считываем счетчик импульсов
  // Отключаем прерывания на мгновение, чтобы 'veterPulseCount' не изменился во время чтения
  noInterrupts();
  unsigned long pulseCount = veterPulseCount;
  veterPulseCount = 0; // Сбрасываем счетчик для следующего измерения
  interrupts(); // Включаем прерывания обратно

  lastVeterCheck = millis(); // Сбрасываем таймер

  // 1. Считаем частоту (Гц = импульсы в секунду)
  // (float)pulseCount / (duration / 1000.0)
  float frequency = (float)pulseCount / (duration / 500.0);

  // 2. Переводим частоту в скорость ветра (м/с или км/ч)
  float windSpeed = frequency * VETER_FACTOR;

  Serial.print("Скорость ветра (м/с): ");
  Serial.println(windSpeed, 6);
  // Отправляем ответ в ESP
  espSerial.println("VETER:" + String(windSpeed));
}


// Функция для чтения всех данных с BME680
void readAllBMEData() {
  if (!bme.performReading()) {
    Serial.println("Ошибка чтения BME680!");
    espSerial.println("BME:Error");
    return;
  }
  
  float tempC = bme.temperature;
  float humidity = bme.humidity;
  float pressure = bme.pressure / 100.0F; // Давление в гПа
  
  Serial.print("Температура: "); Serial.print(tempC); Serial.println(" *C");
  Serial.print("Влажность: "); Serial.print(humidity); Serial.println(" %");
  Serial.print("Давление: "); Serial.print(pressure); Serial.println(" hPa");

  // Отправляем все данные в ESP в виде одной строки
  String response = "BME:T:" + String(tempC) + "|H:" + String(humidity) + "|P:" + String(pressure);
  espSerial.println(response);
}

// Функция чтения фоторезистора и отправки ответа в ESP
void readResistor() {
  int potValue = analogRead(POT_PIN);
  Serial.print("Значение фоторезистора: ");
  Serial.println(potValue);
  // Отправляем ответ в ESP
  espSerial.println("POT:" + String(potValue));
}