#include <Preferences.h>
Preferences mem;
void InitOTA()
{
  ArduinoOTA.onStart([]()
                     {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else { // U_SPIFFS
        type = "filesystem";
      }
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); });

  ArduinoOTA.onEnd([]()
                   { 
    Serial.println("\nEnd"); 
    mem.begin("memory", false);
    mem.putString("error", "OTA_UPLOADING_OK");
    mem.end();
    ESP.restart(); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
      mem.begin("memory", false);
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        mem.putString("error", "OTA_AUTH_ERROR");
      } else if (error == OTA_BEGIN_ERROR) {
        mem.putString("error", "OTA_BEGIN_ERROR");
      } else if (error == OTA_CONNECT_ERROR) {
        mem.putString("error", "OTA_CONNECT_ERROR");
      } else if (error == OTA_RECEIVE_ERROR) {
        mem.putString("error", "OTA_RECEIVE_ERROR");
      } else if (error == OTA_END_ERROR) {
        mem.putString("error", "OTA_END_ERROR");
      }
      mem.end();
      delay(10);
      ESP.restart();}
      );

  ArduinoOTA.begin();
  Serial.println("");
  Serial.println("OTA iniciado");
}
