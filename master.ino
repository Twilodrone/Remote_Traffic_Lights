#include <WiFi.h>
#include <esp_now.h>

#define BUTTON_PIN       4

#define PED_RED_RELAY    16
#define PED_GREEN_RELAY  17

#define WAIT_RELAY       18
#define WALK_RELAY       19

const uint32_t VEHICLE_CLEAR_TIME    = 5000;
const uint32_t YELLOW_TIME           = 3000;
const uint32_t PEDESTRIAN_GREEN_TIME = 10000;
const uint32_t PEDESTRIAN_FLASH_TIME = 5000;

enum State
{
  IDLE,
  REQUESTED,
  VEHICLE_YELLOW,
  PEDESTRIAN_GREEN,
  PEDESTRIAN_FLASH,
  FAULT
};

struct Packet
{
  uint8_t state;
};

Packet packet;

uint8_t slaveAddress[] =
{
  0x24,
  0x6F,
  0x28,
  0x11,
  0x22,
  0x33
};

State currentState = IDLE;

unsigned long stateStart = 0;
unsigned long lastHeartbeat = 0;

bool requestActive = false;

void sendState()
{
  packet.state = currentState;
  esp_now_send(slaveAddress, (uint8_t *)&packet, sizeof(packet));
}

void setOutputs()
{
  switch (currentState)
  {
    case IDLE:
      digitalWrite(PED_RED_RELAY, HIGH);
      digitalWrite(PED_GREEN_RELAY, LOW);

      digitalWrite(WAIT_RELAY, LOW);
      digitalWrite(WALK_RELAY, LOW);
      break;

    case REQUESTED:
      digitalWrite(PED_RED_RELAY, HIGH);
      digitalWrite(PED_GREEN_RELAY, LOW);

      digitalWrite(WAIT_RELAY, HIGH);
      digitalWrite(WALK_RELAY, LOW);
      break;

    case PEDESTRIAN_GREEN:
      digitalWrite(PED_RED_RELAY, LOW);
      digitalWrite(PED_GREEN_RELAY, HIGH);

      digitalWrite(WAIT_RELAY, LOW);
      digitalWrite(WALK_RELAY, HIGH);
      break;

    case PEDESTRIAN_FLASH:
      digitalWrite(PED_RED_RELAY, LOW);

      digitalWrite(WAIT_RELAY, LOW);
      digitalWrite(WALK_RELAY, HIGH);

      digitalWrite(PED_GREEN_RELAY,
                   (millis() / 500) % 2);
      break;

    default:
      break;
  }
}

void setup()
{
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(PED_RED_RELAY, OUTPUT);
  pinMode(PED_GREEN_RELAY, OUTPUT);

  pinMode(WAIT_RELAY, OUTPUT);
  pinMode(WALK_RELAY, OUTPUT);

  WiFi.mode(WIFI_STA);

  esp_now_init();

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr,
         slaveAddress,
         6);

  esp_now_add_peer(&peerInfo);

  currentState = IDLE;
  setOutputs();
}

void loop()
{
  if (millis() - lastHeartbeat > 500)
  {
    sendState();
    lastHeartbeat = millis();
  }

  switch (currentState)
  {
    case IDLE:

      if (!digitalRead(BUTTON_PIN))
      {
        currentState = REQUESTED;
        stateStart = millis();

        sendState();
      }

      break;

    case REQUESTED:

      if (millis() - stateStart >
          VEHICLE_CLEAR_TIME)
      {
        currentState = VEHICLE_YELLOW;
        stateStart = millis();

        sendState();
      }

      break;

    case VEHICLE_YELLOW:

      if (millis() - stateStart >
          YELLOW_TIME)
      {
        currentState = PEDESTRIAN_GREEN;
        stateStart = millis();

        sendState();
      }

      break;

    case PEDESTRIAN_GREEN:

      if (millis() - stateStart >
          PEDESTRIAN_GREEN_TIME)
      {
        currentState = PEDESTRIAN_FLASH;
        stateStart = millis();

        sendState();
      }

      break;

    case PEDESTRIAN_FLASH:

      if (millis() - stateStart >
          PEDESTRIAN_FLASH_TIME)
      {
        currentState = IDLE;
        stateStart = millis();

        sendState();
      }

      break;
  }

  setOutputs();
}
