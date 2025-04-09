
/*-----
  Proyecto base para capturar fotos: https://RandomNerdTutorials.com/telegram-esp32-cam-photo-arduino/
*/

#define WDT 0          // Funcionamiento del WatchDog.
#define TIEMP_MOV_F 1  // El tiempo de movimiento manejamos de forma manual o por el pontenciometro del PIR.
#define RESET_NVC 0    // Limpiar la memoria NVC.
#define DEPURACION_T 0 // Depurar por telnet.
#define OTA 1          // Actualizacion por OTA

#if RESET_NVC
#include <nvs_flash.h>
#endif
#if WDT
#include <esp_task_wdt.h>
#endif
#if DEPURACION_T
#include <ESPmDNS.h>
#include <TelnetStream.h>
#endif
#if OTA
#include <ArduinoOTA.h>
#include "config_OTA.hpp"
#endif

#include <Arduino.h>
#include <WiFi.h>
#include "esp_sleep.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <WiFiManager.h>
#include <Ticker.h>
#include "pines_cam.h"

Preferences memoria;
WiFiClientSecure clientTCP;
UniversalTelegramBot bot("", clientTCP);
#define pinLed 12
#define pinPir 13
#define analogBat 33

const char *npt = "pool.ntp.org";
const char *myDomain = "api.telegram.org";
const char *hostname = "ESPCAM-BOT";
const char *hostpass = "12345678";

const int BOT_MTBS = 2500;          // mSeg.
const int RETRASO_FOT = 600;        // mSeg.
const int ESPERAR_MOV_FIJO = 15000; // mSeg. Tiempo de espera antes de detectar nuevo mov.
const float CALIBRACION_BAT = 0.20; // mayor CALIBRACION_BAT +voltaje

const int tPanico = 60;  // Reinciar en caso de congelarse mas de 60 seg. via Watchdog.
const int hApagAut = 19; // PM Apagdo automatico 19:00 horas.
const int hEncAut = 7;   // AM Encendido automatico 07:00 horas.

RTC_DATA_ATTR int ultimoMsjBot = 0;
struct tm timeinfo;
struct users
{
  char id[12];
  char nomb[10];
  bool estado;
};

char token[50] = "vacio";
users usuarios[2] = {{"0", "Adm", false}, {"0", "Inv", false}};
bool solicFot[2] = {false, false};
bool alertMov = false;
bool usrAct;

char text[20];
bool modConfig = false;
bool enviarFoto = false;
bool apagado = false;
bool movimiento = false;
bool modAhorro = false;
bool alertTiemp = true;
bool apagadoAuto = false;
bool apagadoMan = false;
bool funcOTA = false;
bool activo = false;

byte desc = 0;
String errorRes = "";
unsigned long tAntMensaj;
unsigned long tAntBat;

void getDatos()
{
  memoria.begin("memory", true);
  delay(20);
  modAhorro = memoria.getBool("modDormir", false);
  alertTiemp = memoria.getBool("alertTiemp", false);
  apagado = memoria.getBool("estApag", false);
  modConfig = memoria.getBool("config", false);
  apagadoAuto = memoria.getBool("apagAut", false);
  apagadoMan = memoria.getBool("apagadMan", false);
  funcOTA = memoria.getBool("funcOta", false);
  errorRes = memoria.getString("error", "");

  memoria.getBytes("tkn", token, sizeof(token));
  size_t peopleSize = memoria.getBytes("users", usuarios, sizeof(usuarios));
  delay(10);
  memoria.end();

  if (peopleSize == 0)
  {
    Serial.println(F("No se pudieron cargar los datos."));
    errorRes += "E: Cargar datos.";
    for (uint8_t i = 0; i < 254; i++)
    {
      delay(100);
      alertaLED();
    }
    digitalWrite(pinLed, LOW);
    activo = usuarios[0].estado || usuarios[1].estado;
  }
}
void setBool(const char *d, bool v)
{
  memoria.begin("memory", false);
  memoria.putBool(d, v);
  memoria.end();
}
void setString(const char *err)
{
  memoria.begin("memory", false);
  memoria.putString("error", err);
  memoria.end();
}

void configCamara()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound())
  {
    // Serial.println("Calidad Alta");
    config.frame_size = FRAMESIZE_SVGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 35;           // 10  0-63 un número más bajo significa mayor calidad
    config.fb_count = 1;
  }
  else
  {
    // Serial.println("Calidad Baja");
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 25; // 12  0-63 un número más bajo significa mayor calidad
    config.fb_count = 1;
  }
  delay(1500);

  // inicio de cámara
  esp_err_t err = esp_camera_init(&config);
  delay(500);

  if (err != ESP_OK)
  {
    err = esp_camera_init(&config);
  }

  if (err != ESP_OK)
  {
    Serial.printf("ERROR AL INICIAR LA CAMARA, ERROR: 0x%x", err);
    bot.sendMessage(usuarios[0].id, "⏳Error al iniciar la camara\nReiniciando... ⬇", "");
    delay(3000);
    ESP.restart();
  }
}

void confWifiManager()
{
  WiFi.mode(WIFI_AP);
  WiFiManager wm;

  WiFiManagerParameter nuevoToken("Token", "Token Telegram", token, sizeof(token));
  WiFiManagerParameter nuevoAdm("Admin", "ID Admin", usuarios[0].id, sizeof(usuarios[0].id));
  WiFiManagerParameter nuevoInv("Invitado", "ID Invitado", usuarios[1].id, sizeof(usuarios[1].id));
  const String n = "<p style='color: red; text-align: center;'>" + errorRes + "</p>";
  WiFiManagerParameter customMessage(n.c_str());
  wm.addParameter(&nuevoToken);
  wm.addParameter(&nuevoAdm);
  wm.addParameter(&nuevoInv);
  wm.addParameter(&customMessage);

  // wm.setShowDnsFields(true);
  // wm.setShowInfoErase(false);
  wm.setBreakAfterConfig(true); // Guardar a un que las credenciales Telegram a un que las credenciales wifi sean incorrectas.
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(10);
  wm.startConfigPortal(hostname, hostpass);

  Serial.println("CONFIGURACION TERMINADO");
  strncpy(token, nuevoToken.getValue(), sizeof(token));
  strncpy(usuarios[0].id, nuevoAdm.getValue(), sizeof(usuarios[0].id));
  strncpy(usuarios[1].id, nuevoInv.getValue(), sizeof(usuarios[1].id));

  if (strlen(token) < 2)
  {
    strncpy(token, "vacio", sizeof(token));
  }

  for (int i = 0; i < 2; i++)
  {
    if (strlen(usuarios[i].id) < 10)
    {
      strncpy(usuarios[i].id, "0", sizeof(usuarios[i].id));
      usuarios[i].estado = false;
    }
  }

  memoria.begin("memory", false);
  memoria.putBytes("tkn", token, sizeof(token));
  memoria.putBytes("users", usuarios, sizeof(usuarios));
  memoria.end();
  delay(1000);
  ESP.restart();
}

void configInicio()
{
  if (modConfig)
  {
    // Serial.println(F("MODO CONFIGURACION"));
    Ticker ledParp;
    setBool("config", false);
    ledParp.attach(0.1, alertaLED);
    confWifiManager();
    ledParp.detach();
    delay(1000);
    ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(3);
  // wm.setSTAStaticIPConfig(IPAddress(192, 168, 1, 15), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0), IPAddress(1, 1, 1, 1));
  // wm.resetSettings();

  if (wm.autoConnect())
  {
    bot.updateToken(token);
    clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    delay(2000);

    byte tiemp = 0;
    while (!clientTCP.connect(myDomain, 443) && tiemp < 70)
    {
      if (tiemp == 69)
      {
        if (errorRes.length() < 60)
        {
          errorRes += " E: Conexion a Internet, API.\n";
          setString(errorRes.c_str());
        }
        setBool("config", true);
        delay(1000);
        ESP.restart();
      }
      tiemp++;
      delay(200);
    }
    clientTCP.stop();
  }
  else
  {
    Serial.println(F("NO HAY CONEXION WIFI"));
    if (errorRes.length() < 60)
    {
      errorRes += " E: Conexion al WiFi.\n";
      setString(errorRes.c_str());
    }
    setBool("config", true);
    delay(1000);
    ESP.restart();
  }

  if (String(usuarios[0].id) == "0" && String(usuarios[1].id) == "0")
  {
    Serial.println(F("NO HAY USUARIOS REGISTRADOS"));
    if (errorRes.length() < 60)
    {
      errorRes += " E: No hay usuarios.\n";
      setString(errorRes.c_str());
    }
    setBool("config", true);
    delay(1000);
    ESP.restart();
  }

#if OTA
  if (funcOTA)
  {
    // Serial.println(F(FUNCION OTA DISPONIBLE));
    InitOTA();
    setBool("funcOta", false);
    bot.sendMessage(usuarios[0].id, "Actualizacion por OTA habilitado. ⚙\nSu IP: " + WiFi.localIP().toString(), "");
    while (true)
    {
      ArduinoOTA.handle();
      delay(10);
    }
  }
#endif
  delay(100);
}

String eventosError()
{
  String errorCras = "";
  esp_reset_reason_t resetReason = esp_reset_reason();
  switch (resetReason)
  {
  case ESP_RST_UNKNOWN:
    errorCras = "R: causa desconocida.";
    break;
  case ESP_RST_EXT:
    errorCras = "R: Pin externo.";
    break;
  case ESP_RST_PANIC:
    errorCras = "R: Panico (crash)";
    break;
  case ESP_RST_INT_WDT:
    errorCras = "R: Watchdog (WDT)";
    break;
  case ESP_RST_TASK_WDT:
    errorCras = "R: Tarea del watchdog";
    break;
  case ESP_RST_WDT:
    errorCras = "R: Otro tipo de WDT reinicio";
    break;
  case ESP_RST_BROWNOUT:
    errorCras = "R: Bajo voltaje (brownout)";
    break;
  case ESP_RST_SDIO:
    errorCras = "R: Error en SDIO";
    break;
    /*case ESP_RST_SW:
    errorCras = "Reinicio por ESP.restart()";
    break;
    case ESP_RST_POWERON:
    errorCras = "Reinicio por encendido.";
    break;
    case ESP_RST_DEEPSLEEP:
    errorCras = "R: salida de Deep Sleep.";
    break;*/
  }

  if (!errorCras.isEmpty() || !errorRes.isEmpty())
  {
    String txt = "\n\n⚠ EVENTOS ";
    if (!errorRes.isEmpty())
    {
      txt += "\n- " + errorRes;
    }
    if (!errorCras.isEmpty())
    {
      txt += "\n- " + errorCras;
    }
    setString("");
    errorCras = txt;
  }
  return errorCras;
}

void alertaLED()
{
  digitalWrite(pinLed, !digitalRead(pinLed)); // Cambia el estado del LED
}

void mod_dormir()
{
#if WDT
  // esp_task_wdt_delete(NULL);
  esp_task_wdt_deinit();
#endif
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1); // Despertar con señal alta
  delay(2000);
  esp_deep_sleep_start();
}

void mod_apagado(int n)
{
#if WDT
  // esp_task_wdt_delete(NULL);
  esp_task_wdt_deinit();
#endif
  unsigned long tmp = n * 1000000; // n = segundos
  esp_sleep_enable_timer_wakeup(tmp);
  delay(1000);
  esp_deep_sleep_start();
}

void apaAutomat()
{
  if (apagadoAuto)
  {
    if (hActual().substring(0, 2).toInt() == hApagAut)
    {
      ultimoMsjBot = bot.messages[0].message_id;
      setBool("estApag", apagado = true);
      for (uint8_t i = 0; i < 2; i++)
      {
        if (strlen(usuarios[i].id) > 9)
        {
          bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Equipo APAGADO AUT.📵", "", estadoMenu(i), true);
        }
      }
      mod_apagado(3700);
    }
  }
}

#if WDT
void iniciar_WDT()
{
  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdt_config = {.timeout_ms = tPanico * 1000, .trigger_panic = true};
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
}
#endif

// -------------------------------
String TomarFoto(bool x = false)
{
  String tiempo = String(emReloj) + hActual();
  x ? tiempo + String(emDorm) : tiempo;

  String getAll = "";
  String getBody = "";

  bool fot = false;
  camera_fb_t *fb = NULL; // Deseche la primera imagen por mala calidad.
  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb); // deshacerse de la imagen almacenada en el buffer

  // TOMAR UNA NUEVA FOTO
  delay(RETRASO_FOT);
  digitalWrite(pinLed, HIGH);
  fb = NULL;
  for (uint8_t i = 0; i < 3; i++)
  {                           // Intentar sacar otra foto si falla.
    fb = esp_camera_fb_get(); // Sacar la foto.
    if (fb)
    { // Capturo la foto bien?
      fot = true;
      break;
    }
    else
    { // Error al sacar la foto.
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    delay(500);
  }

  digitalWrite(pinLed, LOW);
  if (alertTiemp && movimiento)
  {
    digitalWrite(pinLed, HIGH);
  }

  if (alertMov)
  { // MOVIMIENTO DETECTADO
    for (uint8_t i = 0; i < 2; i++)
    {
      if (usuarios[i].estado)
      {
        bot.sendMessage(usuarios[i].id, "MOVIMIENTO DETECTADO 🔊", "");
        if (!fot)
        {
          bot.sendMessage(usuarios[i].id, "Fallo la captura de la camara", "");
          // Serial.println(F("Fallo la captura de la camara"));
        }
      }
    }
    alertMov = false;
  }
  else
  { // ORDEN DE SACAR FOTO
    for (uint8_t i = 0; i < 2; i++)
    {
      if (solicFot[i])
      {
        if (!fot)
        {
          bot.sendMessage(usuarios[i].id, "Fallo la captura de la camara", "");
          // Serial.println(F("Fallo la captura de la camara"));
        }
      }
    }
  }
  delay(100);

  if (clientTCP.connect(myDomain, 443) && fot)
  {
    // Serial.print("\nConectando a " + String(myDomain));
    // Serial.println(F(" ---> Conexión exitosa"));

    String head;
    String tail;
    for (uint8_t i = 0; i < 2; i++)
    {
      if (usuarios[i].estado || solicFot[i])
      {
        head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + String(usuarios[i].id) + "\r\n--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"caption\"; \r\n\r\n" + tiempo + "\r\n--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
        tail = "\r\n--RandomNerdTutorials--\r\n";

        size_t imageLen = fb->len;
        size_t extraLen = head.length() + tail.length();
        size_t totalLen = imageLen + extraLen;

        clientTCP.println("POST /bot" + String(token) + "/sendPhoto HTTP/1.1");
        clientTCP.println("Host: " + String(myDomain));
        clientTCP.println("Content-Length: " + String(totalLen));
        clientTCP.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
        clientTCP.println();
        clientTCP.print(head);

        uint8_t *fbBuf = fb->buf;
        size_t fbLen = fb->len;
        for (size_t n = 0; n < fbLen; n = n + 1024)
        {
          if (n + 1024 < fbLen)
          {
            clientTCP.write(fbBuf, 1024);
            fbBuf += 1024;
          }
          else if (fbLen % 1024 > 0)
          {
            size_t remainder = fbLen % 1024;
            clientTCP.write(fbBuf, remainder);
          }
        }
        clientTCP.print(tail);
      }
    }
    esp_camera_fb_return(fb);
    // Serial.println(F("Foto enviada."));

    int waitTime = 5000; // tiempo de espera de 5 segundos
    long startTimer = millis();
    boolean state = false;

    // Serial.print(F("Esperando respuesta."));
    while ((startTimer + waitTime) > millis())
    {
      // Serial.print(".");
      delay(100);
      while (clientTCP.available())
      {
        char c = clientTCP.read();
        if (state == true)
          getBody += String(c);
        if (c == '\n')
        {
          if (getAll.length() == 0)
            state = true;
          getAll = "";
        }
        else if (c != '\r')
          getAll += String(c);
        startTimer = millis();
      }
      if (getBody.length() > 0)
        break;
    }
    clientTCP.stop();
    Serial.println("\n");
    // Serial.println(getBody);
    // Serial.println(F("Respuesta de envio de foto recibido correctamente."));
    delay(10);
  }
  else
  {
    getBody = "Error al conectarse a la api.telegram.org.";
    // Serial.println(getBody);

    for (uint8_t i = 0; i < 2; i++)
    {
      if (usuarios[i].estado || solicFot[i])
        bot.sendMessage(String(usuarios[i].id), "Hubo un error de la API o de la camara.", "");
    }
  }
  solicFot[0] = false;
  solicFot[1] = false;
  return getBody;
}

void botTelegram(int newMsj)
{
  for (int i = 0; i < newMsj; i++)
  {
    String chat_id = String(bot.messages[i].chat_id);
    if (bot.messages[i].message_id == ultimoMsjBot)
    {
      continue;
    }

    if (chat_id.equals(String(usuarios[i].id)))
    {
      usrAct = 0;
    }
    else if (chat_id.equals(String(usuarios[1].id)))
    {
      usrAct = 1;
    }
    else
    {
      bot.sendMessage(chat_id, "Usuario no autorizado", "");
      continue;
    }

    bot.messages[i].text.toCharArray(text, sizeof(text));
    Serial.println(text);

    //--------------------------------------
    if (strcmp(text, "/start") == 0 || strcmp(text, "/comandos") == 0)
    {
      bot.sendMessageWithReplyKeyboard(chat_id, welcome, "", estadoMenu(usrAct), true);
      continue;
    }

    if (strcmp(text, "/estado") == 0)
    {
      ultimoMsjBot = bot.messages[0].message_id;
      bot.sendMessage(chat_id, estadoAct(usrAct), "");
      // bot.sendMessage(usuarios[0].id, String(ESP.getHeapSize() / 1024) + " - " + String(ESP.getFreeHeap() / 1024), "");
      continue;
    }

    if (strcmp(text, "/VIGILAR🚨") == 0 || strcmp(text, "/NO-VIGILAR") == 0)
    {
      if (apagado && strcmp(text, "/VIGILAR🟢") == 0)
      {
        bot.sendMessage(chat_id, F("Primero debe ENCENDER el equipo ⛔"));
        continue;
      }

      usuarios[usrAct].estado = !usuarios[usrAct].estado;
      activo = (usuarios[0].estado || usuarios[1].estado) ? true : false;

      memoria.begin("memory", false);
      memoria.putBytes("users", usuarios, sizeof(usuarios));
      memoria.end();

      if (usuarios[usrAct].estado)
      {
        bot.sendMessageWithReplyKeyboard(chat_id, "MODO VIGILAR ACTIVADO 🚨🕵️‍♂️", "", estadoMenu(usrAct), true);
        if (modAhorro)
        {
          ultimoMsjBot = bot.messages[0].message_id;
          bot.sendMessage(chat_id, "Durmiendo.. 😴", "");
          mod_dormir();
        }
      }
      else
      {
        bot.sendMessageWithReplyKeyboard(chat_id, "MODO VIGILAR DESACTIVADO ❌🕵️‍♂️", "", estadoMenu(usrAct), true);
        digitalWrite(pinLed, LOW);
        movimiento = false;
      }
      delay(1000);
      continue;
    }

    if (strcmp(text, "/ENCENDER") == 0 || strcmp(text, "/APAGAR") == 0)
    {
      setBool("estApag", apagado = !apagado);
      setBool("apagadMan", apagadoMan = apagado);
      if (apagado)
      {
        ultimoMsjBot = bot.messages[0].message_id;
        setBool("modDormir", modAhorro = false);
        delay(1);
        for (uint8_t i = 0; i < 2; i++)
        {
          bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Equipo APAGADO 🖥️", "", estadoMenu(i), true);
        }
        mod_apagado(600);
      }
      else
      {
        for (uint8_t i = 0; i < 2; i++)
        {
          if (strlen(usuarios[i].id) > 9)
          {
            bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Equipo ENCENDIDO 🖥️", "", estadoMenu(i), true);
          }
        }
      }
      continue;
    }

    if (strcmp(text, "/foto") == 0 || strcmp(text, "📸") == 0)
    {
      if (apagado)
      {
        bot.sendMessage(chat_id, "Primero debe ENCENDER el equipo ⛔");
        continue;
      }
      solicFot[usrAct] = true;
      enviarFoto = true;
      continue;
    }

    if (strcmp(text, "/modDormido") == 0 || strcmp(text, "/modDespiert") == 0)
    {
      if (strcmp(text, "/modDormido") == 0)
      {
        if (apagado)
        {
          bot.sendMessage(chat_id, "Primero debe ENCENDER el equipo ⛔");
          continue;
        }
        if (!usuarios[usrAct].estado && !usuarios[!usrAct].estado)
        {
          bot.sendMessage(chat_id, "No hay ningun usuario VIGILANDO 👮‍♂️");
          continue;
        }
      }
      setBool("modDormir", modAhorro = !modAhorro);
      String x = estadoMenu(usrAct);
      if (modAhorro)
      {
        setBool("apagadMan", apagadoMan = false);
        ultimoMsjBot = bot.messages[0].message_id;
        for (uint8_t i = 0; i < 2; i++)
        {
          if (strlen(usuarios[i].id) > 9)
          {
            bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Modo DORMIR activado 🌙", "", x, true);
            bot.sendMessage(usuarios[i].id, "Durmiendo.. 😴", "");
          }
        }
        mod_dormir();
      }
      else
      {
        bot.sendMessageWithReplyKeyboard(usuarios[usrAct].id, "Modo DESPIERTO activado ☀️", "", x, true);
        bot.sendMessageWithReplyKeyboard(usuarios[!usrAct].id, "Modo DESPIERTO activado ☀️", "", x, true);
      }
      continue;
    }

    if (strcmp(text, "/alertDura") == 0)
    {
      setBool("alertTiemp", alertTiemp = !alertTiemp);
      String msj = "Duracion del mov.: ";
      msj += alertTiemp ? "Activado ✅" : "Desactivado ❌";
      bot.sendMessage(chat_id, msj, "");
      digitalWrite(pinLed, LOW);
      continue;
    }

    if (strcmp(text, "/configDatos") == 0)
    {
      if (usrAct == 0)
      {
        setBool("config", true);
        ultimoMsjBot = bot.messages[0].message_id;
        bot.sendMessage(chat_id, "Reiniciando... ⬇️\n\n⚙ Ingresar al Punto de Acceso: http://192.168.4.1 \n- WiFi: " + String(hostname) + "\n- Pass: " + String(hostpass), "");
        delay(1000);
        ESP.restart();
      }
      else
      {
        bot.sendMessage(chat_id, "No permitido", "");
        continue;
      }
    }

    if (strcmp(text, "/apagAuto") == 0)
    {
      ultimoMsjBot = bot.messages[0].message_id;
      apagadoAuto = !apagadoAuto;
      setBool("apagAut", apagadoAuto);
      String txt = apagadoAuto ? ("Apagado automatico ON a las " + String(hApagAut) + " hrs. 🕐") : "Apagado automatico OFF";

      for (uint8_t i = 0; i < 2; i++)
      {
        if (strlen(usuarios[i].id) > 9)
        {
          bot.sendMessage(usuarios[i].id, txt, "");
        }
      }
      continue;
    }

#if OTA
    if (strcmp(text, "/configOTA") == 0)
    {
      ultimoMsjBot = bot.messages[0].message_id;
      if (usrAct == 0)
      {
        setBool("funcOta", true);
        bot.sendMessage(usuarios[0].id, "Reiniciando... ⬇", "");
        delay(1000);
        ESP.restart();
      }
      else
      {
        bot.sendMessage(chat_id, "No permitido", "");
        continue;
      }
    }
#endif

    if (strcmp(text, "/reiniciar") == 0)
    {
      ultimoMsjBot = bot.messages[0].message_id;
      bot.sendMessage(chat_id, "Reiniciando... ⬇", "");
      delay(1000);
      ESP.restart();
      continue;
    }
    bot.sendMessage(chat_id, "Comando desconocido 👨‍🔧");
  }
}

#if TIEMP_MOV_F
void sensorMovimientoFijo()
{
  static unsigned long tTranscurr = millis();
  static unsigned long tAntMov = millis();
  if (digitalRead(pinPir) && !movimiento)
  {
    // Serial.println(F("MOVIMIENTO DETECTADO!"));
    movimiento = true;
    alertMov = true;
    enviarFoto = true;
    tTranscurr = millis();
    tAntMov = millis();

    if (alertTiemp)
    {
      digitalWrite(pinLed, HIGH);
    }
  }
  else if (movimiento)
  {
    if ((millis() - tAntMov) > ESPERAR_MOV_FIJO)
    {
      if (!digitalRead(pinPir))
      {
        // Serial.println(F("MOVIMIENTO TERMINADO!"));
        movimiento = false;

        int t = (millis() - tTranscurr) / 1000;
        // Serial.print(F("Duracion: "));
        // Serial.println(t);

        if (alertTiemp)
        {
          digitalWrite(pinLed, LOW);
          String msj = "Fin del movimiento: " + String(t) + " Seg.";
          for (uint8_t i = 0; i < 2; i++)
          {
            if (usuarios[i].estado)
            {
              bot.sendMessage(usuarios[i].id, msj, "");
            }
          }
          delay(10);
        }
      }
      else
      {
        tAntMov = millis() - 5000;
      }
    }
  }
}
#else
void sensorMovimientoAuto()
{
  static unsigned long tTranscurr = millis();
  static unsigned long tAntMov = millis();
  if (digitalRead(pinPir))
  { // Si se detecta movimiento (pirPin = HIGH)
    if (!movimiento)
    {
      // Serial.println(F("MOVIMIENTO DETECTADO!"));
      movimiento = true;
      alertMov = true;
      enviarFoto = true;
      tTranscurr = millis();

      if (alertTiemp)
      {
        digitalWrite(pinLed, HIGH);
      }
    }
  }
  else
  { // Si no se detecta movimiento (pirPin = LOW)
    if (movimiento)
    {
      // Serial.println(F("MOVIMIENTO TERMINADO!"));
      movimiento = false;

      int t = (millis() - tTranscurr) / 1000;
      // Serial.print(F("Duracion: "));
      // Serial.println(t);

      if (alertTiemp)
      {
        digitalWrite(pinLed, LOW);
        String msj = "Fin del movimiento: " + String(t) + " Seg.";
        for (uint8_t i = 0; i < 2; i++)
        {
          if (usuarios[i].estado)
          {
            bot.sendMessage(usuarios[i].id, msj, "");
          }
        }
        delay(10);
      }
    }
  }
}
#endif

String hActual()
{
  char buffer[10];
  if (!getLocalTime(&timeinfo))
  {
    // Serial.println(F("No se pudo obtener el tiempo"));
    return "00:00:00";
  }
  else
  {
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    return String(buffer);
  }
}

double voltaje()
{
  float vol;
  int pinValor = 0;

  for (uint8_t i = 0; i < 10; i++)
  {
    pinValor += analogRead(analogBat);
  }
  pinValor = pinValor / 10;
  vol = (((pinValor * 3.3) / 4095) * 2 + CALIBRACION_BAT);

  return vol;
}

int bateria(double vol)
{
  int bat = ((vol - 2.9) / (4.2 - 2.9)) * 100;

  if (bat >= 100)
  {
    bat = 100;
  }
  if (bat <= 0)
  {
    bat = 1;
  }

  if (bat < 15 && vol > 2)
  {
    for (uint8_t i = 0; i < 2; i++)
    {
      if (usuarios[i].estado)
      {
        bot.sendMessage(usuarios[i].id, "BATERIA BAJA " + String(bat) + "% 🪫", "");
      }
    }
  }
  return bat;
}

void temperatura()
{
  float temp = temperatureRead();
  if (temp > 110)
  {
    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9)
      {
        bot.sendMessage(usuarios[i].id, "⚠️ Temperatura: " + String(temp) + "°C", "");
      }
    }
  }
}

String estadoAct(bool n)
{
  double bat = voltaje();
  String fuent;
  if (bat > 2)
  {
    fuent = "🔋 -Fuente:  Bateria " + String(bateria(bat)) + "% - " + String(bat) + "V.\n";
  }
  else
  {
    fuent = "🔌 -Fuente: Conexion externa.";
  }

  uint8_t cont = usuarios[0].estado + usuarios[1].estado;

  String msj = "ESTADO:  🕑" + hActual() + " - 👮🏻‍♂️" + cont + "\n\n";
  msj += "📱 -Equipo:  ";
  msj += apagado ? "OFF\n" : "ON\n";
  msj += "🕵️‍♂️ -Modo:  ";
  msj += usuarios[n].estado ? "VIGILANDO🚨\n" : "NO VIGILANDO\n";
  msj += "😴 -Modo dormir:  ";
  msj += modAhorro ? "ON ✅\n" : "OFF ❌\n";
  msj += "⏳ -Alerta de duracion:  ";
  msj += alertTiemp ? "ON ✅\n" : "OFF❌\n";
  msj += "🌙 -Apagado aut. NOCHES:  ";
  msj += apagadoAuto ? "ON ✅\n" : "OFF❌\n";
  msj += "🌡️ -Temperatura: " + String(temperatureRead()) + "°C.\n";
  msj += "🛜 -Señal de red:  " + String(WiFi.RSSI()) + " dBm.\n";
  msj += fuent;
  return msj;
}

String estadoMenu(bool usr)
{
  String t1, t2, t3, t4;
  t1 = apagado ? "ENCENDER" : "APAGAR";
  t3 = modAhorro ? "modDespiert" : "modDormido";
  t4 = usuarios[usr].estado ? "NO-VIGILAR" : "VIGILAR🚨";

  String kJsonA = "[[\"/" + t4 + "\", \"📸\", \"/" + t1 + "\"], [\"/comandos\", \"/estado\", \"/" + t3 + "\"]]";
  return kJsonA;
}

void nuevosMsj(bool newMsj = true)
{
  unsigned long tAntDur = millis();
  if (millis() - tAntMensaj > BOT_MTBS)
  {
    int numMsj = bot.getUpdates(bot.last_message_received + 1);
    while (numMsj)
    {
      newMsj ? botTelegram(numMsj) : (void)0;
      numMsj = bot.getUpdates(bot.last_message_received + 1);
    }
    tAntMensaj = millis();
  }

  float tDur = (millis() - tAntDur) / 1000;
  if (tDur > 25 && activo)
  {
    bot.sendMessage(usuarios[0].id, "Lentitud con el BOT: " + String(tDur) + " seg.", "");
  }
}

// -------------------------------
void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#if WDT
  esp_task_wdt_reset();
#endif
#if RESET_NVC
  resetNVC();
#endif
  Serial.begin(115200);
  Serial.println(F("============="));
  analogReadResolution(12);
  pinMode(pinPir, INPUT);
  pinMode(pinLed, OUTPUT);

  getDatos();
  digitalWrite(pinLed, HIGH);
  delay(60);
  digitalWrite(pinLed, LOW);
  configInicio();

  configTime(-14400, 0, npt);
  time_t now = time(nullptr);
  while (now < 24 * 3600)
  {
    delay(100);
    now = time(nullptr);
  }
  delay(200);
  configCamara();

  uint8_t n = 0;
  String h;
  esp_sleep_wakeup_cause_t causaReset = esp_sleep_get_wakeup_cause(); // función en el ESP32 que permite verificar la causa que despertó al dispositivo
  switch (causaReset)
  {
  case ESP_SLEEP_WAKEUP_TIMER: // ----------------------TEMPORIZADOR APAGADO
    // bot.sendMessage(usuarios[0].id, "Despertado", "");
    for (uint8_t i = 0; i < 5; i++)
    {
      nuevosMsj();
      delay(1000);
    }

    if (apagadoAuto && (hActual().substring(0, 2).toInt() == hEncAut) && !apagadoMan)
    {
      setBool("estApag", apagado = false);
      for (uint8_t i = 0; i < 2; i++)
      {
        if (strlen(usuarios[i].id) > 9)
        {
          bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Equipo ENCENDIDO AUT.🖥️", "", estadoMenu(i), true);
        }
      }
    }

    if (apagado)
    {
      bool x;
      if (hApagAut > hEncAut)
      {
        x = (hActual().substring(0, 2).toInt() >= hApagAut || hActual().substring(0, 2).toInt() < hEncAut);
      }
      else
      {
        x = (hActual().substring(0, 2).toInt() >= hApagAut && hActual().substring(0, 2).toInt() < hEncAut);
      }
      (apagadoAuto && x && !apagadoMan) ? mod_apagado(3680) : mod_apagado(600);
    }

    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9)
      {
        bot.sendMessage(usuarios[i].id, bienv2, "");
      }
    }
    bateria(voltaje());
    break;

  case ESP_SLEEP_WAKEUP_EXT0: // -----------------------INTERRUPTOR EXTERNO
    // Serial.println(F("Se despertó por GPIO (EXT0)"));
    alertMov = true;
    TomarFoto(true);
    bateria(voltaje());
    delay(1000);

    while (n < 10)
    {
      nuevosMsj();
      delay(1000);
      if (n == 14 && digitalRead(pinPir))
      {
        n = 0;
      }
      n++;
    }

    if (modAhorro && activo)
    {
      mod_dormir();
    }
    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9)
      {
        bot.sendMessage(usuarios[i].id, bienv2, "");
      }
    }
    break;

  default: // ------------------------------------------ENCENDIDO
    Serial.print(F("Calibrando sensor SENSOR, esperar 5 seg. ->"));
    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9)
      {
        bot.sendMessage(usuarios[i].id, bienv, "");
      }
    }

    for (uint8_t i = 0; i < 10; i++)
    {
      alertaLED();
      delay(600);
    }
    digitalWrite(pinLed, LOW);

    Serial.println(F(" Listo."));
    setBool("estApag", apagado = false);
    setBool("apagadMan", apagadoMan = false);
    setBool("modDormir", modAhorro = false);
    nuevosMsj(false);

    String txt = eventosError();
    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9)
      {
        bot.sendMessageWithReplyKeyboard(usuarios[i].id, (estadoAct(i) + txt), "", estadoMenu(i), true);
      }
    }
    break;
  }
  bateria(voltaje());

#if WDT
  iniciar_WDT();
#endif

#if DEPURACION_T
  MDNS.begin("ESPCAM");
  MDNS.addService("telnet", "tcp", 23);
  TelnetStream.begin();
#endif
  tAntMensaj = millis();
  tAntBat = millis();
}

void loop()
{
#if WDT
  esp_task_wdt_reset();
#endif
  if (enviarFoto)
  {
    if (activo || solicFot[0] || solicFot[1])
    {
      TomarFoto();
      enviarFoto = false;
    }
  }
  nuevosMsj();
#if TIEMP_MOV_F
  activo ? sensorMovimientoFijo() : (void)0;
#else
  activo ? sensorMovimientoAuto() : (void)0;
#endif

  if ((millis() - tAntBat) > 120000) // 2 min.
  {
    WiFiClient client;
    if (!client.connect("www.google.com", 80))
    {
      if (desc == 2)
      {
        // Serial.println(F("Perdida de conexion."));
        setString("E: Perdida de conexion.\n");
        delay(1000);
        ESP.restart();
      }
      desc++;
      WiFi.reconnect();
    }
    else
    {
      client.stop();
      desc = 0;
    }
    apaAutomat();
    temperatura();
    bateria(voltaje());
    tAntBat = millis();
  }
  delay(40);
}

// -------------------------------
#if DEPURACION_T
void EnviarTelnet(String txt)
{
  TelnetStream.print(txt);
  TelnetStream.println();
}
#endif

#if RESET_NVC
void resetNVC()
{
  // LIMPIAR LA MEMORIA NVS
  nvs_flash_erase();
  nvs_flash_init();
  while (true)
    ;
}
#endif
