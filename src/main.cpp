#include <Arduino.h>
#include <DHTesp.h>
#include <BH1750.h>

#include <WiFi.h>
#include <PubSubClient.h>

#define WIFI_SSID "POCO X3 Pro"
#define WIFI_PASSWORD "ketokpintu"

#define MQTT_BROKER "broker.emqx.io"
#define MQTT_TOPIC_PUBLISH "esp32Rudy/John/data"
#define MQTT_TOPIC_SUBSCRIBE "esp32Rudy/John/data"

#define PIN_SCL 22
#define PIN_SDA 21

#define DHT_PIN 15

#define RED_LED 5
#define GREEN_LED 18
#define YELLOW_LED 19

#define pollrate 1000 // in ms
DHTesp dht;
BH1750 bh;

char g_szDeviceId[30];
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Global Variables
float globalTemp = 0;
float globalHum = 0;
float globalLux = 0;

// Wifi
void WifiConnect()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }  
  Serial.print("System connected with IP address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("RSSI: %d\n", WiFi.RSSI());
}

// MQTT

// void onPublishMessage()
// {
//   char szMsg[50];
//   static int nMsgCount=0;
//   sprintf(szMsg, "%d, Temp: %.2f, Hum: %.2f, Lux: %.2f", nMsgCount++, globalTemp, globalHum, globalLux);
//   mqtt.publish(MQTT_TOPIC_PUBLISH, szMsg);
// }

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.write(payload, len);
  Serial.println();
}

boolean mqttConnect() {
  sprintf(g_szDeviceId, "esp32_%08X",(uint32_t)ESP.getEfuseMac());
  mqtt.setServer(MQTT_BROKER, 1883);
  mqtt.setCallback(mqttCallback);
  Serial.printf("Connecting to %s clientId: %s\n", MQTT_BROKER, g_szDeviceId);

  boolean fMqttConnected = false;
  for (int i=0; i<3 && !fMqttConnected; i++) {
    Serial.print("Connecting to mqtt broker...");
    fMqttConnected = mqtt.connect(g_szDeviceId);
    if (fMqttConnected == false) {
      Serial.print(" fail, rc=");
      Serial.println(mqtt.state());
      delay(1000);
    }
  }

  if (fMqttConnected)
  {
    Serial.println(" success");
    mqtt.subscribe(MQTT_TOPIC_SUBSCRIBE);
    Serial.printf("Subcribe topic: %s\n", MQTT_TOPIC_SUBSCRIBE);
    // onPublishMessage();
  }
  return mqtt.connected();
}

// else

void taskDT (void *pvParameters) {
  for(;;) {
    float temperature = dht.getTemperature();
    if (dht.getStatus() == DHTesp::ERROR_NONE) {
      // Serial.printf("Temperature: %.2f C, Humidity: %.2f%%\n", temperature, humidity);
      globalTemp = temperature;
      
      char szMsg[50];
      static int nMsgCount=0;
      sprintf(szMsg, "%d, Temp: %.2f", nMsgCount++, globalTemp);
      mqtt.publish(MQTT_TOPIC_PUBLISH, szMsg);
    }
    vTaskDelay(5000/portTICK_PERIOD_MS);
  }
}

void taskDH (void *pvParameters) {
  for(;;) {
    float humidity = dht.getHumidity();
    if (dht.getStatus() == DHTesp::ERROR_NONE) {
      globalHum = humidity;

      char szMsg[50];
      static int nMsgCount=0;
      sprintf(szMsg, "%d, Hum: %.2f", nMsgCount++, globalHum);
      mqtt.publish(MQTT_TOPIC_PUBLISH, szMsg);
    }
    vTaskDelay(6000/portTICK_PERIOD_MS);
  }
}

void taskBH (void *pvParameters) {
  for(;;) {
    float lux = bh.readLightLevel();
    // Serial.printf("Light level: %2f Lux \n", lux);
    globalLux = lux;
    vTaskDelay(3000/portTICK_PERIOD_MS);

    char szMsg[50];
    static int nMsgCount=0;
    sprintf(szMsg, "%d, Lux: %.2f", nMsgCount++, globalLux);
    mqtt.publish(MQTT_TOPIC_PUBLISH, szMsg);
  }
}

void checkLogic (void *pvParameters) {
  for (;;) {
    // if suhu 
    if (globalTemp > 28 && globalHum > 80) {
      digitalWrite(RED_LED, HIGH);

      digitalWrite(YELLOW_LED, LOW);
      digitalWrite(GREEN_LED, LOW);
    } else if (globalTemp > 28 && globalHum > 60 && globalHum < 80) {
      digitalWrite(YELLOW_LED, HIGH);

      digitalWrite(RED_LED, LOW);
      digitalWrite(GREEN_LED, LOW);
    } else if (globalTemp < 28 && globalHum < 60) {
      digitalWrite(GREEN_LED, HIGH);

      digitalWrite(YELLOW_LED, LOW);
      digitalWrite(RED_LED, LOW);
    }
    // if lux
    if (globalLux > 400) {
      Serial.printf("Warning, Pintu Dibuka; Lux: %2f Lux \n", globalLux);
    } else if (globalLux < 400) {
      Serial.printf("Pintu tertutup; Lux: %2f Lux \n", globalLux);
    } else {}
    vTaskDelay(pollrate/portTICK_PERIOD_MS);
  }
}

void setup() {
  // put your setup code here, to run once:

  Serial.begin(9600);
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  dht.setup(DHT_PIN, DHTesp::DHT11);

  Wire.begin(PIN_SDA, PIN_SCL);
  bh.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);

  // networking
  WifiConnect();
  mqttConnect();

  xTaskCreatePinnedToCore(taskDH, "taskDH", configMINIMAL_STACK_SIZE+2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskDT, "taskDT", configMINIMAL_STACK_SIZE+2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(taskBH, "taskBH", configMINIMAL_STACK_SIZE+2048, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(checkLogic, "checkLogic", configMINIMAL_STACK_SIZE+2048, NULL, 4, NULL, 0);

  Serial.println("Setup Complete! ");
}

void loop() {
  // put your main code here, to run repeatedly:
  mqtt.loop();
}