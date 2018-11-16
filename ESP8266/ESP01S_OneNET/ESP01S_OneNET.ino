/*
  Basic ESP8266 MQTT example

  This sketch demonstrates the capabilities of the pubsub library in combination
  with the ESP8266 board/library.

  It connects to an MQTT server then:
  - publishes "hello world" to the topic "outTopic" every two seconds
  - subscribes to the topic "inTopic", printing out any messages
    it receives. NB - it assumes the received payloads are strings not binary
  - If the first character of the topic "inTopic" is an 1, switch ON the ESP Led,
    else switch it off

  It will reconnect to the server if the connection is lost using a blocking
  reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
  achieve the same result without blocking the main loop.

  To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"

*/
#include <ESP8266WiFi.h>
#include "PubSubClient.h"
#include <ArduinoJson.h>

//#define BUILTIN_LED 2
#define IO0_PIN 0
#define IO2_PIN 2

// Update these with values suitable for your network.

const char* ssid = "ssid";
const char* password = "password";
const char* mqtt_server = "183.230.40.39";

WiFiClient espClient;
PubSubClient client(espClient);

int value_io0 = 0;
int value_io2 = 0;

int reconnectWaitSec = 5;
bool needfeedback = false;

bool _analyzeFail = false;
char* _lastCreq;

long lastMsg = 0;
char ack_buf[1];
char msg_buf[21];
unsigned short json_len = 18;
uint8_t* packet_p;

StaticJsonDocument<200> doc;
char msgJson[] = "{\"IO0\":0,\"IO2\":0}";

void setup_wifi() {
  pinMode(IO0_PIN, OUTPUT);
  pinMode(IO2_PIN, OUTPUT);

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  delay(1000);
  Serial.println("Running...");
}

void publishdata(char* topic, char* msg) {
  json_len = strlen(msg); //packet length count the end char '\0'
  msg_buf[0] = char(0x03); //palyLoad packet byte 1, one_net mqtt Publish packet payload byte 1, type3 , json type2
  msg_buf[1] = char(json_len >> 8); //high 8 bits of json_len (16bits as short int type)
  msg_buf[2] = char(json_len & 0xff); //low 8 bits of json_len (16bits as short int type)
  memcpy(msg_buf + 3, msg, strlen(msg));
  msg_buf[3 + strlen(msg)] = 0;
  Serial.print("Publish message to ");
  Serial.print(topic);
  Serial.print(" : ");
  Serial.println(msg);
  client.publish(topic, msg_buf, 3 + strlen(msg), false); // msg_buf as payload length which may have a "0x00"byte
  Serial.println("- - - - - OK.");
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  int topiclen = strlen(topic);
  if (topiclen > 5 && topic[0] == '$' && topic[1] == 'c' && topic[2] == 'r' && topic[3] == 'e' && topic[4] == 'q' && topic[5] == '/') {
    needfeedback = true;
    _lastCreq = new char[topiclen + 1];
    for (int i = 0; i < topiclen; i++) {
      _lastCreq[i] = topic[i];
    }
    _lastCreq[topiclen] = 0;
  } else {
    needfeedback = false;
  }

  Serial.println("Analyze playload to Json...");
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, payload);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("Analyze playload failed: "));
    Serial.println(error.c_str());
    if (needfeedback) {
      ack_buf[0] = '0';
    }
    return;
  }

  // Get the root object in the document
  JsonObject root = doc.as<JsonObject>();

  Serial.println("Analyze Json...");
  // Fetch values.
  int io = root["IO"];
  int value = root["Value"];
  if (io == 0) {
    value_io0 = value == 1 ? 1 : 0;
    Serial.print("Set IO_0 : ");
    Serial.println(value_io0);
    digitalWrite(IO0_PIN, value_io0 == 1 ? HIGH : LOW);
  }
  if (io == 2) {
    value_io2 = value == 1 ? 1 : 0;
    Serial.print("Set IO_2 : ");
    Serial.println(value_io2);
    digitalWrite(IO2_PIN, value_io2 == 1 ? HIGH : LOW);
  }
  ack_buf[0] = '1';
  Serial.println("Callback over.");
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
     if (client.connect("device id", "product id", "key")) {
      Serial.println("connected");
      // ... and resubscribe
      //client.subscribe("DevCtrl");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.print(" try again in ");
      Serial.print(reconnectWaitSec);
      Serial.println(" seconds ");
      // Wait 5 seconds before retrying
      delay(reconnectWaitSec * 1000);
      if (reconnectWaitSec < 600) {
        reconnectWaitSec += 5;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 6002);  //not 1883 , one net use the port of 6002 as mqtt server
  client.setCallback(callback);
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (needfeedback || now - lastMsg > 5000) {
    if (needfeedback) {
      needfeedback = false;
      _lastCreq[3] = 's';
      _lastCreq[4] = 'p';
      Serial.print("Publish Ack message to ");
      Serial.print(_lastCreq);
      Serial.print(" : ");
      Serial.print(ack_buf);
      client.publish(_lastCreq, ack_buf, 1, false);
      Serial.println("   ...OK.");
    }
    lastMsg = now;
    if (value_io0 == 0) {
      msgJson[7] = '0';
    } else
    {
      msgJson[7] = '1';
    }
    if (value_io2 == 0) {
      msgJson[15] = '0';
    } else
    {
      msgJson[15] = '1';
    }
    publishdata("$dp", msgJson);
  }
}
