#include "secrets.h"

#include <NtpClientLib.h>
#include <Servo.h>
#include <ESP8266WiFi.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>


/* ------------------------------------------- */
/* INI PINS                                    */
/* ------------------------------------------- */

#define PIN_SERVO_PWM D7

#define PIN_FEED_1 D4
#define PIN_FEED_2 D1
#define PIN_FEED_3 D2

#define PIN_FEED_DETECTOR D6

/* ------------------------------------------- */
/* INI SERVO                                   */
/* ------------------------------------------- */

Servo servo;

/* ------------------------------------------- */
/* INI                                         */
/* ------------------------------------------- */

#define INI 0
#define AWAITING 1
#define FEED 2
#define FEEDING 3

byte current_action;
bool action_changed;
bool action_changed_processed;

byte feed_button_pressed = 0;

byte feed_counter = 0;

bool was_high = false;

#define TIMER_BLOCKED 2000
unsigned long blocked_timer;

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASSWORD;
int status = WL_IDLE_STATUS;

WiFiClient client;

int8_t timeZone = 1;
int8_t minutesTimeZone = 0;
const PROGMEM char *ntpServer = "pool.ntp.org";

Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_SERVERPORT, MQTT_CID, MQTT_USERNAME, MQTT_KEY);
Adafruit_MQTT_Subscribe feed_subscribe = Adafruit_MQTT_Subscribe(&mqtt, "futterautomat/feed");
Adafruit_MQTT_Publish lastfeed_publish = Adafruit_MQTT_Publish(&mqtt, "futterautomat/lastfeed");

void feedCallback(char *data, uint16_t len)
{
  Serial.println(data);

  if (strcmp(data, "Hauchduennes Pfefferminzblaettchen") == 0)
  {
    Serial.println("PIN_FEED_1");
    feed_button_pressed = PIN_FEED_1;
    feed_counter = 1;
    return;
  }
  if (strcmp(data, "Quarter pounder with cheese") == 0)
  {
    Serial.println("PIN_FEED_2");
    feed_button_pressed = PIN_FEED_2;
    feed_counter = 2;
    return;
  }
  if (strcmp(data, "Mr. Creosote") == 0)
  {
    Serial.println("PIN_FEED_3");
    feed_button_pressed = PIN_FEED_3;
    feed_counter = 3;
    return;
  }
}

/*
 * WiFi init stuff
 */
void startWiFiClient()
{
  while (WiFi.localIP().toString() == "0.0.0.0")
  {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    Serial.println(status);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
  delay(10000);
  Serial.print("You're connected to the network");
  Serial.println("IP address: " + WiFi.localIP().toString());
}

void setup()
{
  Serial.begin(9600);

  startWiFiClient();

  NTP.setInterval(63);
  NTP.begin(ntpServer, timeZone, true, minutesTimeZone);
    
  feed_subscribe.setCallback(feedCallback);
  mqtt.subscribe(&feed_subscribe);

  setAction(INI);
}

void loop()
{
  MQTT_connect();
  mqtt.processPackets(1);

  // if (!mqtt.ping())
  // {
  //   mqtt.disconnect();
  // }

  action_changed_processed = true;

  input();
  process();
  output();

  if (action_changed_processed)
  {
    action_changed = false;
  }
}

void MQTT_connect()
{
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected())
  {
    return;
  }

  Serial.println("Connecting to MQTT...");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0)
  { // connect will return 0 for connected
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 10 seconds...");
    mqtt.disconnect();
    delay(10000); // wait 10 seconds
    retries--;
    if (retries == 0)
    {
      // basically die and wait for WDT to reset me
      while (1)
        ;
    }
  }
  Serial.println("MQTT Connected!");
}

/* ------------------------------------------- */
/* INPUT                                       */
/* ------------------------------------------- */

void input()
{
  switch (current_action)
  {
  case AWAITING:
    inputAwaiting();
    break;
  case FEEDING:
    inputFeeding();
    break;
  }
}

void inputAwaiting()
{
  byte feed_1 = digitalRead(PIN_FEED_1);
  byte feed_2 = digitalRead(PIN_FEED_2);
  byte feed_3 = digitalRead(PIN_FEED_3);

  if (feed_1 == LOW)
  {
    Serial.println("PIN_FEED_1");
    feed_button_pressed = PIN_FEED_1;
    feed_counter = 1;
    return;
  }
  if (feed_2 == LOW)
  {
    Serial.println("PIN_FEED_2");
    feed_button_pressed = PIN_FEED_2;
    feed_counter = 2;
    return;
  }
  if (feed_3 == LOW)
  {
    Serial.println("PIN_FEED_3");
    feed_button_pressed = PIN_FEED_3;
    feed_counter = 3;
    return;
  }
}

void inputFeeding()
{
  if (action_changed)
  {
    blocked_timer = millis();
  }

  if (millis() - blocked_timer > TIMER_BLOCKED)
  {
    servo.write(0);
    feed_button_pressed = 0;
    setAction(AWAITING);
  }


  byte feed_detected = digitalRead(PIN_FEED_DETECTOR);

  if (feed_detected == HIGH)
  {
    was_high = true;
    return;
  }

  if ((was_high) && (feed_detected == LOW))
  {
    Serial.println(F("Feed detected"));
    Serial.println(NTP.getDateStr() + " " + NTP.getTimeStr());

    char dateTimeStr[17];   // array to hold the result.

    sprintf(dateTimeStr, "%s %s", NTP.getDateStr().c_str(), NTP.getTimeStr().c_str());
    lastfeed_publish.publish(dateTimeStr);

    servo.write(0);
    feed_button_pressed = 0;

    if (feed_counter <= 0)
    {
      setAction(AWAITING);
    }
    else
    {
      delay(500);
      setAction(FEED);
    }

    return;
  }
}

/* ------------------------------------------- */
/* PROCESS                                     */
/* ------------------------------------------- */

void process()
{
  switch (current_action)
  {
  case INI:
    processIni();
    break;
  case AWAITING:
    processAwaiting();
    break;
  }
}

void processIni()
{
  pinMode(PIN_FEED_1, INPUT_PULLUP);
  pinMode(PIN_FEED_2, INPUT_PULLUP);
  pinMode(PIN_FEED_3, INPUT_PULLUP);

  pinMode(PIN_FEED_DETECTOR, INPUT_PULLUP);

  pinMode(13, OUTPUT);

  servo.write(0);
  servo.attach(PIN_SERVO_PWM);
  servo.write(0);

  setAction(AWAITING);
}

void processAwaiting()
{
  if (feed_button_pressed != 0)
  {
    Serial.println("processAwaiting");
    setAction(FEED);
  }
}

/* ------------------------------------------- */
/* OUTPUT                                      */
/* ------------------------------------------- */

void output()
{
  switch (current_action)
  {
  case FEED:
    outputFeed();
  }
}

void outputFeed()
{
  was_high = false;
  feed_counter--;
  servo.write(10);
  Serial.println(F("Start feeding"));
  setAction(FEEDING);
}

/* ------------------------------------------- */

void setAction(byte action)
{
  current_action = action;
  action_changed = true;
  action_changed_processed = false;
}
