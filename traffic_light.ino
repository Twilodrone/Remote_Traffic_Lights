/*
 * Remote Traffic Lights — ESP-NOW Pedestrian Crossing
 *
 * Два одинаковых столба. Каждый имеет:
 *   - транспортный светофор (R, Y, G)
 *   - пешеходный светофор (R, G)
 *   - кнопка для пешеходов
 *
 * По умолчанию: транспортный зелёный, пешеходный красный.
 * При нажатии любой кнопки: транспортный → мигающий зелёный → жёлтый → красный,
 * пешеходный → зелёный. Через заданное время — обратно.
 *
 * Столбы синхронизируются по ESP-NOW.
 */

#include <WiFi.h>
#include <esp_now.h>

// ========== Пины ==========

#define BTN_PIN         4     // кнопка (GND при нажатии, INPUT_PULLUP)

#define VEH_RED_PIN     16    // транспортный — красный
#define VEH_YEL_PIN     17    // транспортный — жёлтый
#define VEH_GRN_PIN     18    // транспортный — зелёный

#define PED_RED_PIN     19    // пешеходный — красный
#define PED_GRN_PIN     21    // пешеходный — зелёный

// ========== Тайминги (мс) ==========

const uint32_t T_GREEN_FLASH  = 4000;   // мигание зелёного перед жёлтым
const uint32_t T_YELLOW       = 3000;   // жёлтый
const uint32_t T_PED_GREEN    = 10000;  // пешеходный зелёный
const uint32_t T_PED_FLASH    = 5000;   // пешеходный мигает перед возвратом
const uint32_t HEARTBEAT_MS   = 500;    // отправка состояния по ESP-NOW
const uint32_t TIMEOUT_MS     = 3000;   // таймаут — если нет heartbeat, авария

// ========== Состояния FSM ==========

enum State : uint8_t {
  ST_VEHICLE_GREEN,          // транспорт зелёный, пешеход красный
  ST_GREEN_FLASH,            // транспорт мигает зелёным
  ST_VEHICLE_YELLOW,         // транспорт жёлтый
  ST_PEDESTRIAN_GREEN,       // пешеход зелёный
  ST_PEDESTRIAN_FLASH,       // пешеход мигает
  ST_FAULT                   // авария (нет связи с другим столбом)
};

// ========== Пакет ESP-NOW ==========

struct Packet {
  uint8_t state;   // State текущего столба
};

// ========== Глобальные переменные ==========

State currentState = ST_VEHICLE_GREEN;

unsigned long stateStart  = 0;
unsigned long lastTxTime  = 0;
unsigned long lastRxTime  = 0;

bool buttonWasPressed = false;

// Адрес второго столба — задай через `peer_addr` после
// прошивки, прочитав MAC: Serial.println(WiFi.macAddress());
uint8_t peerAddress[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// ========== ESP-NOW отправка ==========

void sendState() {
  Packet pkt;
  pkt.state = (uint8_t)currentState;
  esp_now_send(peerAddress, (uint8_t *)&pkt, sizeof(pkt));
}

// ========== ESP-NOW приём ==========

void onReceive(const esp_now_recv_info_t *info,
               const uint8_t *data, int len) {
  if (len != sizeof(Packet)) return;

  Packet pkt;
  memcpy(&pkt, data, sizeof(pkt));
  State peerState = (State)pkt.state;

  lastRxTime = millis();

  // Если второй столб в аварии или мы в аварии — не дёргаемся
  if (peerState == ST_FAULT || currentState == ST_FAULT) return;

  // Если напарник уже переключился — подхватываем его состояние,
  // но никогда не откатываемся назад, если у нас более «продвинутый» этап
  if (peerState > currentState) {
    currentState = peerState;
    stateStart = millis();
  }
}

// ========== Управление выходами ==========

void setOutputs() {
  // По умолчанию всё выключено
  digitalWrite(VEH_RED_PIN, LOW);
  digitalWrite(VEH_YEL_PIN, LOW);
  digitalWrite(VEH_GRN_PIN, LOW);
  digitalWrite(PED_RED_PIN, LOW);
  digitalWrite(PED_GRN_PIN, LOW);

  switch (currentState) {
    case ST_VEHICLE_GREEN:
      digitalWrite(VEH_GRN_PIN, HIGH);
      digitalWrite(PED_RED_PIN, HIGH);
      break;

    case ST_GREEN_FLASH:
      // Мигаем зелёным
      digitalWrite(VEH_GRN_PIN, (millis() / 400) % 2);
      digitalWrite(PED_RED_PIN, HIGH);
      break;

    case ST_VEHICLE_YELLOW:
      digitalWrite(VEH_YEL_PIN, HIGH);
      digitalWrite(PED_RED_PIN, HIGH);
      break;

    case ST_PEDESTRIAN_GREEN:
      digitalWrite(VEH_RED_PIN, HIGH);
      digitalWrite(PED_GRN_PIN, HIGH);
      break;

    case ST_PEDESTRIAN_FLASH:
      digitalWrite(VEH_RED_PIN, HIGH);
      digitalWrite(PED_GRN_PIN, (millis() / 500) % 2);
      break;

    case ST_FAULT:
      // Жёлтый мигает — аварийный режим
      digitalWrite(VEH_YEL_PIN, (millis() / 500) % 2);
      break;
  }
}

// ========== Конечный автомат ==========

void updateStateMachine() {
  unsigned long now = millis();

  switch (currentState) {
    case ST_VEHICLE_GREEN:
      if (!digitalRead(BTN_PIN)) {           // кнопка нажата (GND)
        buttonWasPressed = true;
        currentState = ST_GREEN_FLASH;
        stateStart = now;
        sendState();
      }
      break;

    case ST_GREEN_FLASH:
      if (now - stateStart >= T_GREEN_FLASH) {
        currentState = ST_VEHICLE_YELLOW;
        stateStart = now;
        sendState();
      }
      break;

    case ST_VEHICLE_YELLOW:
      if (now - stateStart >= T_YELLOW) {
        currentState = ST_PEDESTRIAN_GREEN;
        stateStart = now;
        sendState();
      }
      break;

    case ST_PEDESTRIAN_GREEN:
      if (now - stateStart >= T_PED_GREEN) {
        currentState = ST_PEDESTRIAN_FLASH;
        stateStart = now;
        sendState();
      }
      break;

    case ST_PEDESTRIAN_FLASH:
      if (now - stateStart >= T_PED_FLASH) {
        currentState = ST_VEHICLE_GREEN;
        stateStart = now;
        sendState();
      }
      break;

    default:
      break;
  }
}

// ========== Setup ==========

void setup() {
  Serial.begin(115200);

  pinMode(BTN_PIN, INPUT_PULLUP);

  pinMode(VEH_RED_PIN, OUTPUT);
  pinMode(VEH_YEL_PIN, OUTPUT);
  pinMode(VEH_GRN_PIN, OUTPUT);
  pinMode(PED_RED_PIN, OUTPUT);
  pinMode(PED_GRN_PIN, OUTPUT);

  // Выключаем всё
  digitalWrite(VEH_RED_PIN, LOW);
  digitalWrite(VEH_YEL_PIN, LOW);
  digitalWrite(VEH_GRN_PIN, LOW);
  digitalWrite(PED_RED_PIN, LOW);
  digitalWrite(PED_GRN_PIN, LOW);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    while (1) delay(100);
  }

  esp_now_register_recv_cb(onReceive);

  // Регистрируем peer (второй столб)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;
  peerInfo.ifidx   = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ESP-NOW add peer failed — check peerAddress");
  }

  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());

  currentState = ST_VEHICLE_GREEN;
  stateStart   = millis();
  lastRxTime   = millis();
}

// ========== Loop ==========

void loop() {
  unsigned long now = millis();

  // Проверка таймаута связи
  if (now - lastRxTime > TIMEOUT_MS && currentState != ST_FAULT) {
    currentState = ST_FAULT;
    stateStart = now;
    sendState();
  }

  // Выход из аварии при появлении связи
  if (currentState == ST_FAULT && (now - lastRxTime <= TIMEOUT_MS)) {
    currentState = ST_VEHICLE_GREEN;
    stateStart = now;
    sendState();
  }

  updateStateMachine();

  setOutputs();

  // Heartbeat — отправляем состояние по ESP-NOW
  if (now - lastTxTime >= HEARTBEAT_MS) {
    sendState();
    lastTxTime = now;
  }

  // Для отладки
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 2000) {
    Serial.print("State: ");
    Serial.print(currentState);
    Serial.print("  Peer timeout: ");
    Serial.println(now - lastRxTime > TIMEOUT_MS ? "YES" : "OK");
    lastPrint = now;
  }
}
