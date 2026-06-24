#include <WiFi.h>
#include <esp_now.h>

#define VEH_RED_RELAY      16
#define VEH_YELLOW_RELAY   17
#define VEH_GREEN_RELAY    18

const uint32_t HEARTBEAT_TIMEOUT = 3000;

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

volatile State currentState = FAULT;

unsigned long lastPacketTime = 0;

void onReceive(const uint8_t *mac,
               const uint8_t *data,
               int len)
{
  Packet packet;

  memcpy(&packet,
         data,
         sizeof(packet));

  currentState = (State)packet.state;

  lastPacketTime = millis();
}

void setOutputs()
{
  digitalWrite(VEH_RED_RELAY, LOW);
  digitalWrite(VEH_YELLOW_RELAY, LOW);
  digitalWrite(VEH_GREEN_RELAY, LOW);

  switch (currentState)
  {
    case IDLE:
    case REQUESTED:
      digitalWrite(VEH_GREEN_RELAY, HIGH);
      break;

    case VEHICLE_YELLOW:
      digitalWrite(VEH_YELLOW_RELAY, HIGH);
      break;

    case PEDESTRIAN_GREEN:
    case PEDESTRIAN_FLASH:
      digitalWrite(VEH_RED_RELAY, HIGH);
      break;

    case FAULT:

      digitalWrite(VEH_YELLOW_RELAY,
                   (millis() / 500) % 2);
      break;
  }
}

void setup()
{
  pinMode(VEH_RED_RELAY, OUTPUT);
  pinMode(VEH_YELLOW_RELAY, OUTPUT);
  pinMode(VEH_GREEN_RELAY, OUTPUT);

  WiFi.mode(WIFI_STA);

  esp_now_init();

  esp_now_register_recv_cb(onReceive);

  lastPacketTime = millis();
}

void loop()
{
  if (millis() - lastPacketTime >
      HEARTBEAT_TIMEOUT)
  {
    currentState = FAULT;
  }

  setOutputs();
}
