
#define WDT 0          // Funcionamiento del WatchDog.
#define TIEMP_MOV_F 1  // El tiempo de movimiento manejamos de forma manual o por el pontenciometro del PIR.
#define RESET_NVC 0    // Limpiar la memoria NVC.
#define DEPURACION_T 0 // Depurar por telnet.
#define OTA 1          // Actualizacion por OTA
#define CALIB_PIR 1    // Esperar unos segundos para que el sensor pir se calibre.
#define INICIAR_VID 1  // Iniciar video al arrancar.

#include <nvs_flash.h>
#if WDT
#include <esp_task_wdt.h>
#endif
#if DEPURACION_T
#include <TelnetStream.h>
#endif
#if OTA
#include <ArduinoOTA.h>
#include "config_OTA.hpp"
#endif

#include "esp_system.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "fb_gfx.h"
#include "esp_http_server.h"
#include "esp_sleep.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <Ticker.h>
#include "pines_cam.h"

#ifdef __cplusplus
extern "C"
{
#endif
  uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nX-Timestamp: %s\r\nContent-Length: %u\r\n\r\n";

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

const int BOT_MTBS = 2400;          // mseg.
const int RETRASO_FOT = 600;        // mseg.
const int ESPERAR_MOV_FIJO = 15000; // mseg. Tiempo de espera antes de detectar nuevo mov.
const float CALIBRACION_BAT = 0.20; // mas voltaje
byte FPS_VIDEO = 7;                 // Emular limitacion de FPS (~10fps = 100ms  experiment)

const int tPanico = 60; // Reinciar en caso de congelarse mas de 60 seg. via Watchdog.
byte hApagAut = 19;     // PM, Apagdo automatico 19:00 horas.
const int hEncAut = 7;  // AM, Encendido automatico 07:00 horas.

RTC_DATA_ATTR int ultimoMsjBot = 0;
struct tm timeinfo;
struct users
{
  char id[12];
  char nomb[10];
  bool estado;
};

char ip[16] = "0.0.0.0";
char Gateway[16] = "0.0.0.0";
char Mask[16] = "0.0.0.0";
char dns1[16] = "8.8.8.8";
char dns2[16] = "8.8.4.4";

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
bool usrActivo = false;
bool alertBotLent = true;
bool invitadActive = false;

byte desc = 0;
String errorRes = "";
unsigned long tAntMensaj;
unsigned long tAntBat;

typedef struct
{
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

bool client_active = false;
bool stream_active = false;
httpd_handle_t stream_httpd = NULL; // Streaming
httpd_config_t config;

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
  invitadActive = memoria.getBool("invActive", false);
  ultimoMsjBot = memoria.getInt("ultMsj", 0);

  memoria.getBytes("sIp", ip, sizeof(ip));
  memoria.getBytes("sGat", Gateway, sizeof(Gateway));
  memoria.getBytes("sSub", Mask, sizeof(Mask));

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
      for (uint8_t i = 0; i < 2; i++)
      {
        digitalWrite(pinLed, HIGH);
        delay(100);
        alertaLED();
      }
      delay(2400);
    }
  }

  usrActivo = usuarios[0].estado || usuarios[1].estado;
}
void setString(const char *err)
{
  memoria.begin("memory", false);
  memoria.putString("error", err);
  memoria.end();
}
void setBool(const char *d, bool v)
{
  memoria.begin("memory", false);
  memoria.putBool(d, v);
  memoria.end();
}
void setInt(const char *d, int v)
{
  memoria.begin("memory", false);
  memoria.putInt(d, v);
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
  config.xclk_freq_hz = 15000000; // default: 20000000 | 20MHz
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound())
  {
    // Serial.(println(F("Calidad Alta"));
    config.frame_size = FRAMESIZE_VGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 30;          // default: 10 | 0-63 un número más bajo significa mayor calidad
    config.fb_count = 2;               // Buffer para el video y captura de fotos.
  }
  else
  {
    // Serial.println(F("Calidad Baja"));
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 30; // default: 12
    config.fb_count = 2;
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
    delay(6000);
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

  WiFiManagerParameter nIp("IP", "IP estatica", ip, sizeof(ip));
  WiFiManagerParameter nGat("Gateway", "Gateway", Gateway, sizeof(Gateway));
  WiFiManagerParameter nSub("Mask", "Mask", Mask, sizeof(Mask));

  wm.addParameter(&nuevoToken);
  wm.addParameter(&nuevoAdm);
  wm.addParameter(&nuevoInv);
  wm.addParameter(&customMessage);

  wm.addParameter(&nIp);
  wm.addParameter(&nGat);
  wm.addParameter(&nSub);

  // wm.setShowInfoErase(true;
  wm.setBreakAfterConfig(true); // Guardar a un que las credenciales wifi sean incorrectas.
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(10);

  wm.startConfigPortal(hostname, hostpass);

  Serial.println(F("CONFIGURACION TERMINADO"));
  strncpy(ip, nIp.getValue(), sizeof(ip));
  strncpy(Gateway, nGat.getValue(), sizeof(Gateway));
  strncpy(Mask, nSub.getValue(), sizeof(Mask));
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
  memoria.putBytes("sIp", ip, sizeof(ip));
  memoria.putBytes("sGat", Gateway, sizeof(Gateway));
  memoria.putBytes("sSub", Mask, sizeof(Mask));
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
    Serial.println(F("MODO CONFIGURACION"));
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
  wm.setHostname(hostname);
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(3);
  // wm.resetSettings();

  if (strlen(ip) > 7 && String(ip) != "0.0.0.0" && !funcOTA)
  {
    IPAddress staticIp, staticGateway, staticMask;
    staticIp.fromString(ip);
    staticGateway.fromString(Gateway);
    staticMask.fromString(Mask);
    wm.setSTAStaticIPConfig(staticIp, staticGateway, staticMask);
  }

  if (wm.autoConnect())
  {
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), IPAddress(8, 8, 8, 8), IPAddress(8, 8, 4, 4));

    Serial.print(F("Gateway: "));
    Serial.println(WiFi.gatewayIP());
    Serial.print(F("Mask: "));
    Serial.println(WiFi.subnetMask());
    Serial.print(F("MAC: "));
    Serial.println(WiFi.macAddress());
    Serial.print(F("DNS 1: "));
    Serial.println(WiFi.dnsIP(0));
    Serial.print(F("DNS 2: "));
    Serial.println(WiFi.dnsIP(1));

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
          errorRes += " E: Conexion a Internet o API.\n";
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

    // Iniciar mDNS
    if (MDNS.begin(hostname))
    {
      Serial.print("mDNS iniciado: http://" + String(hostname) + ".local\n");
    }
    else
    {
      Serial.println(F("Error al iniciar mDNS"));
    }
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
    setBool("funcOta", false);
    digitalWrite(pinLed, HIGH);
    InitOTA();

    bot.sendMessage(usuarios[0].id, "Actualizacion por OTA habilitado. ⚙\n- IP: " + WiFi.localIP().toString() + "\n- Pass: " + String(hostpass), "");
    while (true)
    {
      ArduinoOTA.handle();
      delay(10);
    }
    digitalWrite(pinLed, LOW);
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
        if (strlen(usuarios[i].id) > 9 && invAct(i))
        {
          bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Equipo APAGADO AUT.📵", "", estadoMenu(i), true);
        }
      }
      stream_active = false;
      stopCameraServer();
      mod_apagado(3680);
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
static esp_err_t index_handler(httpd_req_t *req)
{
  const char *html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>ESPCAM-BOT</title>
  <style>
    body { background: black; color: lime; font-family: monospace; text-align: center; }
    #video-container { position: relative; display: inline-block; }
    #hora { position: absolute; top: 10px; left: 10px; background: rgba(0, 0, 0, 0.7); padding: 5px 10px; font-size: 40px; border-radius: 4px; }
  </style>
</head>
<body>
  <div id="video-container">
    <div id="hora">Cargando hora...</div>
    <canvas id="video" width="800px" height="600px"></canvas>
    <script>
      const canvas = document.getElementById('video');
      const ctx = canvas.getContext('2d');
      const horaElem = document.getElementById('hora');

      let streamTimeout;
      let controller = new AbortController();

      function iniciarStream() {
        controller = new AbortController();

        streamTimeout = setTimeout(() => {
          console.warn('Stream congelado. Recargando página...');
          location.reload(); // o window.location.href = "/";
        }, 4000);

        fetch('/video', { signal: controller.signal }).then(res => {
          const reader = res.body.getReader();
          let buffer = new Uint8Array();

          function concatBuffer(a, b) {
            let tmp = new Uint8Array(a.length + b.length);
            tmp.set(a, 0);
            tmp.set(b, a.length);
            return tmp;
          }

          function findJPEG(buf) {
            for (let i = 0; i < buf.length - 1; i++) {
              if (buf[i] === 0xFF && buf[i + 1] === 0xD8) {
                for (let j = i + 2; j < buf.length - 1; j++) {
                  if (buf[j] === 0xFF && buf[j + 1] === 0xD9) {
                    return buf.slice(i, j + 2);
                  }
                }
              }
            }
            return null;
          }

          function leer() {
            reader.read().then(({ value, done }) => {
              if (done) return;
              clearTimeout(streamTimeout); // llegó algo, reiniciar timeout
              streamTimeout = setTimeout(() => {
                console.warn('Stream colgado. Recargando...');
                location.reload();
              }, 4000);

              buffer = concatBuffer(buffer, value);

              const textChunk = new TextDecoder().decode(value);
              const match = textChunk.match(/X-Timestamp:\s(.+)\r\n/);
              if (match) {
                horaElem.textContent = match[1];
              }

              const jpeg = findJPEG(buffer);
              if (jpeg) {
                const blob = new Blob([jpeg], { type: 'image/jpeg' });
                const url = URL.createObjectURL(blob);
                const img = new Image();
                img.onload = () => {
                  ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
                  URL.revokeObjectURL(url);
                };
                img.src = url;
                buffer = new Uint8Array(); // limpiar buffer
              }

              leer();
            }).catch(err => {
              console.error('Error al leer stream:', err);
              location.reload();
            });
          }

          leer();
        }).catch(err => {
          console.error('Error de conexión:', err);
          location.reload();
        });
      }

      iniciarStream();
    </script>

  </div>
</body>
</html>
)rawliteral";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
  client_active = true;

  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[128];

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "close");

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK)
    return res;

  while (stream_active)
  {
    delay(1000 / FPS_VIDEO); // FPS

    fb = esp_camera_fb_get();
    if (!fb)
    {
      Serial.println(F("Camera capture failed"));
#if DEPURACION_T
      EnviarTelnet("Camera capture failed");
#endif
      res = ESP_FAIL;
      break;
    }

    if (fb->format != PIXFORMAT_JPEG)
    {
      bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
      esp_camera_fb_return(fb);
      fb = NULL;
      if (!jpeg_converted)
      {
        Serial.println(F("JPEG compression failed"));
#if DEPURACION_T
        EnviarTelnet("JPEG compression failed");
#endif
        res = ESP_FAIL;
        break;
      }
    }
    else
    {
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }

    String hor = hActual();

    size_t hlen = snprintf((char *)part_buf, sizeof(part_buf), _STREAM_BOUNDARY);
    res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);

    if (res == ESP_OK)
    {
      hlen = snprintf((char *)part_buf, sizeof(part_buf), _STREAM_PART, hor.c_str(), _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }

    if (res == ESP_OK)
    {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }

    if (fb)
      esp_camera_fb_return(fb);
    else if (_jpg_buf)
      free(_jpg_buf);

    if (res != ESP_OK)
      break;
  }

  // IMPORTANTE: cerrar correctamente la transmisión
  client_active = false;
  httpd_resp_send_chunk(req, NULL, 0);
  return res;
}

void startCameraServer()
{
  config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t stream_uri = {
      .uri = "/video",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL,
  };

  Serial.print(F("\nIP de la transmision: http://"));
  Serial.println(WiFi.localIP());
  Serial.printf("Puerto de la transmision: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK)
  {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
  else
  {
    Serial.println(F("Error al arrancar el servicio."));
  }

  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(stream_httpd, &index_uri);
}

void stopCameraServer()
{
  if (stream_httpd)
  {
    stream_active = false;
    delay(200);

    // Forzar cierre
    httpd_stop(stream_httpd);
    stream_httpd = NULL;

    Serial.println(F("Servidor de cámara completamente detenido"));
  }
}

String TomarFoto(bool x = false)
{
  String tiempo = "⏰ " + hActual();
  x ? String("😴") + tiempo : tiempo;

  String getAll = "";
  String getBody = "";

  bool fot = false;
  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb); // deshacerse de la imagen almacenada en el buffer

  // TOMAR UNA NUEVA FOTO
  delay(RETRASO_FOT);
  digitalWrite(pinLed, HIGH);
  fb = NULL;

  for (uint8_t i = 0; i < 2; i++) // Intentar sacar otra foto si falla.
  {
    fb = esp_camera_fb_get(); // Sacar la foto.
    if (fb)                   // Capturo la foto bien?
    {
      fot = true;
      break;
    }
    else // Error al sacar la foto.
    {
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    delay(100);
  }

  digitalWrite(pinLed, LOW);
  if (alertTiemp && movimiento)
  {
    digitalWrite(pinLed, HIGH);
  }

  if (alertMov) // MOVIMIENTO DETECTADO
  {
    for (uint8_t i = 0; i < 2; i++)
    {
      if (usuarios[i].estado && invAct(i))
      {
        if (client_active)
        {
          tiempo += " - 🔊 MOV. DETECTADO";
        }
        else
        {
          bot.sendMessage(usuarios[i].id, "MOVIMIENTO DETECTADO 🔊", "");
        }

        if (!fot)
        {
          bot.sendMessage(usuarios[i].id, "Fallo la captura de la camara", "");
        }
      }
    }
    alertMov = false;
  }
  else // ORDEN DE SACAR FOTO
  {
    for (uint8_t i = 0; i < 2; i++)
    {
      if (solicFot[i] && !fot && invAct(i))
      {
        bot.sendMessage(usuarios[i].id, "Fallo la captura de la camara", "");
      }
    }
  }
  delay(100);

  if (clientTCP.connect(myDomain, 443) && fot) // En base a : https://RandomNerdTutorials.com/telegram-esp32-cam-photo-arduino/
  {
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
    Serial.println(F("Foto enviada."));

    int waitTime = 4000; // tiempo de espera de 4 segundos
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
      if (invAct(i) && usuarios[i].estado || solicFot[i])
        bot.sendMessage(String(usuarios[i].id), "Error de API o camara.", "");
    }
  }
  solicFot[0] = false;
  solicFot[1] = false;
  return getBody;
}

// Comandos Telegram
void botTelegram(int newMsj)
{
  for (int i = 0; i < newMsj; i++)
  {
    String chat_id = String(bot.messages[i].chat_id);
    if (bot.messages[i].message_id == ultimoMsjBot)
    {
      continue;
    }

    if (chat_id.equals(String(usuarios[0].id)))
    {
      usrAct = 0;
    }
    else if (chat_id.equals(String(usuarios[1].id)) && invAct(1))
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
      usrActivo = usuarios[0].estado || usuarios[1].estado;

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
          if (strlen(usuarios[i].id) > 9 && invAct(i))
          {
            bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Equipo APAGADO 🖥️", "", estadoMenu(i), true);
          }
        }
        mod_apagado(600);
      }
      else
      {
        for (uint8_t i = 0; i < 2; i++)
        {
          if (strlen(usuarios[i].id) > 9 && invAct(i))
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
        if (stream_active)
        {
          bot.sendMessage(chat_id, "Hay una transmision en curso 📽️");
          continue;
        }
      }
      setBool("modDormir", modAhorro = !modAhorro);
      String x = estadoMenu(usrAct);
      if (modAhorro)
      {
        setBool("apagadMan", apagadoMan = false);
        ultimoMsjBot = bot.messages[0].message_id;
        setInt("ultMsj", ultimoMsjBot);
        delay(100);

        for (uint8_t i = 0; i < 2; i++)
        {
          if (strlen(usuarios[i].id) > 9 && invAct(i))
          {
            bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Modo DORMIR activado 🌙", "", x, true);
            bot.sendMessage(usuarios[i].id, "Durmiendo.. 😴", "");
          }
        }
        mod_dormir();
      }
      else
      {
        for (uint8_t i = 0; i < 2; i++)
        {
          if (strlen(usuarios[i].id) > 9 && invAct(i))
          {
            bot.sendMessageWithReplyKeyboard(usuarios[i].id, "Modo DESPIERTO activado ☀️", "", x, true);
          }
        }
      }
      continue;
    }

    if (strcmp(text, "/alertDura") == 0)
    {
      setBool("alertTiemp", alertTiemp = !alertTiemp);
      String msj = "Duracion del mov.: ";
      msj += alertTiemp ? "ACTIVADO ✅" : "DESACTIVADO ❌";
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
        setInt("ultMsj", ultimoMsjBot);

        for (uint8_t i = 0; i < 2; i++)
        {
          if (strlen(usuarios[i].id) > 9 && invAct(i))
          {
            bot.sendMessage(usuarios[i].id, "Reiniciando...⬇️", "");
          }
        }

        bot.sendMessage(chat_id, "⚙ Ingresar al Punto de Acceso: http://192.168.4.1 \n- WiFi: " + String(hostname) + "\n- Pass: " + String(hostpass), "");
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
        if (strlen(usuarios[i].id) > 9 && invAct(i))
        {
          bot.sendMessage(usuarios[i].id, txt, "");
        }
      }
      continue;
    }

    if (strcmp(text, "/iniciarVideo") == 0)
    {
      if (stream_active)
      {
        bot.sendMessage(chat_id, "Ya existe una transmision", "");
        continue;
      }

      ultimoMsjBot = bot.messages[0].message_id;
      stream_active = true;
      bot.sendMessage(chat_id, "Iniciando servidor, espere...", "");
      startCameraServer();
      delay(2000);
      String result = String(hostname);
      result.toLowerCase();
      bot.sendMessage(chat_id, "Transmision disponible 📽️\n- Abrir: http://" + result + ".local", "");
      continue;
    }

    if (strcmp(text, "/pararVideo") == 0)
    {
      if (!stream_active)
      {
        bot.sendMessage(chat_id, "No hay transmision", "");
        continue;
      }

      ultimoMsjBot = bot.messages[0].message_id;
      stream_active = false;
      stopCameraServer();
      bot.sendMessage(chat_id, "Deteniendo...\nTransmision de video terminada 📡", "");
      if (strlen(usuarios[!usrAct].id) > 9 && invAct(1))
      {
        bot.sendMessage(usuarios[!usrAct].id, "TRANSMISION DE VIDEO TERMINADA 📽️", "");
      }
      continue;
    }

    if (strcmp(text, "/alertBotLento") == 0)
    {
      ultimoMsjBot = bot.messages[0].message_id;
      alertBotLent = !alertBotLent;
      String txt = alertBotLent ? "ACTIVADO ✅" : "DESACTIVADO ❌";
      bot.sendMessage(chat_id, "Alerta de Bot lento: " + txt, "");
      continue;
    }

    if (strcmp(text, "/estadoInv") == 0)
    {
      if (usrAct == 0)
      {
        ultimoMsjBot = bot.messages[0].message_id;
        invitadActive = !invitadActive;
        setBool("invActive", invitadActive);

        String txt = invitadActive ? "Invitado ACTIVO ✅" : "Invitado DESACTIVADO ❌";
        bot.sendMessage(chat_id, txt);
        continue;
      }
      else
      {
        bot.sendMessage(chat_id, "No permitido", "");
        continue;
      }
    }

    if (strcmp(text, "/reiniciar") == 0)
    {
      ultimoMsjBot = bot.messages[0].message_id;
      setInt("ultMsj", ultimoMsjBot);
      for (uint8_t i = 0; i < 2; i++)
      {
        if (strlen(usuarios[i].id) > 9 && invAct(i))
        {
          bot.sendMessage(usuarios[i].id, "Reiniciando...⬇️", "");
        }
      }
      delay(1000);
      ESP.restart();
      continue;
    }

#if OTA
    if (strcmp(text, "/updateOTA") == 0)
    {
      ultimoMsjBot = bot.messages[0].message_id;

      if (usrAct == 0)
      {
        setBool("funcOta", true);
        setInt("ultMsj", ultimoMsjBot);

        for (uint8_t i = 0; i < 2; i++)
        {
          if (strlen(usuarios[i].id) > 9 && invAct(i))
          {
            bot.sendMessage(usuarios[0].id, "Reiniciando...⬇️", "");
          }
        }
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

    // Pruebas
    char command[4];
    strncpy(command, text, 3);
    command[3] = '\0';

    if (strcmp(command, "#01") == 0 && usrAct == 0) // Emular FPS
    {
      char msj[10];
      strcpy(msj, text + 3);
      FPS_VIDEO = atoi(msj);
      ultimoMsjBot = bot.messages[0].message_id;
      bot.sendMessage(chat_id, "Nueva conf: " + String(FPS_VIDEO) + "FPS");
      continue;
    }

    if (strcmp(command, "#02") == 0 && usrAct == 0) // Reset NVC
    {
      ultimoMsjBot = bot.messages[0].message_id;
      bot.sendMessage(chat_id, "Reset NVC");
      delay(3000);
      resetNVC();
      ESP.restart();
      continue;
    }

    if (strcmp(command, "#03") == 0 && usrAct == 0) // Cambiar hora apagado aut.
    {
      char msj[10];
      strcpy(msj, text + 3);
      hApagAut = atoi(msj);
      ultimoMsjBot = bot.messages[0].message_id;
      bot.sendMessage(chat_id, "Hora apagado aut.: " + String(hApagAut) + "h.");
      continue;
    }

    // -----
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
    Serial.println(F("MOVIMIENTO DETECTADO!"));
    movimiento = true;
    alertMov = true;
    enviarFoto = true;
    tTranscurr = millis();
    tAntMov = millis();
#if DEPURACION_T
    EnviarTelnet("Movimiento detectado");
#endif

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
            if (usuarios[i].estado && invAct(i))
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

#if DEPURACION_T
      EnviarTelnet("Movimiento detectado");
#endif

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
          if (usuarios[i].estado && invAct(i))
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
      if (usuarios[i].estado && invAct(i))
      {
        bot.sendMessage(usuarios[i].id, "BATERIA BAJA " + String(bat) + "% 🪫", "");
      }
    }
  }
  return bat;
}

void temperatura()
{
  float temp = (temprature_sens_read() - 32) / 1.8;
  if (temp > 115)
  {
    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9 && invAct(i))
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
    fuent = "🔌 -Fuente: Conexion externa.\n";
  }

  uint8_t cont = usuarios[0].estado + usuarios[1].estado;

  String msj = "ESTADO:  🕑" + hActual() + " - 👮🏻‍♂️" + cont;
  msj += client_active ? "  📺1\n\n" : "\n\n";
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
  msj += "🚨 -Alerta de bot lento:  ";
  msj += alertBotLent ? "ON ✅\n" : "OFF❌\n";
  msj += "👨‍🦱 -Usuario invitado: ";
  msj += invitadActive ? " ON✅\n" : "OFF❌\n";
  msj += "🌡️ -Temperatura: " + String(temperatureRead()) + "°C.\n";
  msj += "🛜 -SSID: " + String(WiFi.SSID()) + "\n";
  msj += "📍- IP local: " + String(WiFi.localIP().toString()) + "\n";
  msj += "📶 -Señal de red:  " + String(WiFi.RSSI()) + " dBm.\n";
  msj += fuent + "\n";

  if (stream_active)
  {
    String result = String(hostname);
    result.toLowerCase();
    msj += "\n📽️ **TRANSMITIENDO VIDEO** 📽️\n";
    msj += "- http://" + result + ".local\n";
  }
  return msj;
}

String estadoMenu(bool usr)
{
  String t1, t2, t3, t4;
  t1 = apagado ? "ENCENDER" : "APAGAR";
  t3 = modAhorro ? "modDespiert" : "modDormido";
  t4 = usuarios[usr].estado ? "NO-VIGILAR" : "VIGILAR🚨";

  String kJsonA = "[[\"/" + t4 + "\", \"📸\", \"/" + t1 + "\"], [\"/comandos\", \"/estado\", \"/" + t3 + "\"], [\"/iniciarVideo\", \"/pararVideo\"]]";
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
  if (tDur > 35 && usrActivo && alertBotLent)
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
  String h, txt;
  esp_sleep_wakeup_cause_t causaReset = esp_sleep_get_wakeup_cause(); // función en el ESP32 que permite verificar la causa que despertó al dispositivo
  switch (causaReset)
  {
  case ESP_SLEEP_WAKEUP_TIMER: // ----------------------TEMPORIZADOR APAGADO
    for (uint8_t i = 0; i < 5; i++)
    {
      nuevosMsj();
      delay(700);
    }

    if (apagadoAuto && (hActual().substring(0, 2).toInt() == hEncAut) && !apagadoMan)
    {
      setBool("estApag", apagado = false);
      for (uint8_t i = 0; i < 2; i++)
      {
        if (strlen(usuarios[i].id) > 9 && invAct(i))
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

    txt = eventosError();
    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9 && invAct(i))
      {
        bot.sendMessage(usuarios[i].id, bienv2 + txt, "");
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

    while (n < 15)
    {
      nuevosMsj();
      delay(1000);
      if (n == 14 && digitalRead(pinPir))
      {
        n = 0;
      }
      n++;
    }

    if (modAhorro && usrActivo)
    {
      mod_dormir();
    }

    txt = eventosError();
    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9 && invAct(i))
      {
        bot.sendMessage(usuarios[i].id, bienv2 + txt, "");
      }
    }
    break;

  default: // ------------------------------------------ENCENDIDO
    Serial.print(F("Calibrando sensor SENSOR, esperar 5 seg. ->"));
    for (uint8_t i = 0; i < 2; i++)
    {
      if (strlen(usuarios[i].id) > 9 && invAct(i))
      {
#if CALIB_PIR
        bot.sendMessage(usuarios[i].id, "BIENVENIDO 👋\nCalibrando SENSOR, esperar 5 seg...⏳", "");
#else
        bot.sendMessage(usuarios[i].id, "BIENVENIDO 👋", "");
      }
    }
#endif

#if CALIB_PIR
        for (uint8_t i = 0; i < 10; i++)
        {
          alertaLED();
          delay(500); // retardo
        }
        digitalWrite(pinLed, LOW);
#endif
        Serial.println(F(" Listo."));
        setBool("estApag", apagado = false);
        setBool("apagadMan", apagadoMan = false);
        setBool("modDormir", modAhorro = false);
        nuevosMsj(false);

        txt = eventosError();
        for (uint8_t i = 0; i < 2; i++)
        {
          if (strlen(usuarios[i].id) > 9 && invAct(i))
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
      MDNS.addService("telnet", "tcp", 23);
      TelnetStream.begin();
#endif
      tAntMensaj = millis();
      tAntBat = millis();

#if INICIAR_VID
      startCameraServer();
      stream_active = true;
#endif
    }

    void loop()
    {
#if WDT
      esp_task_wdt_reset();
#endif
      if (enviarFoto)
      {
        if (usrActivo || solicFot[0] || solicFot[1])
        {
          TomarFoto();
          enviarFoto = false;
        }
      }
      nuevosMsj();
#if TIEMP_MOV_F
      usrActivo ? sensorMovimientoFijo() : (void)0;
#else
  usrActivo ? sensorMovimientoAuto() : (void)0;
#endif

      if ((millis() - tAntBat) > 180000) // 3 min.
      {
        WiFiClient client;
        if (!client.connect("www.google.com", 80))
        {
          if (desc > 2 && !stream_active)
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

    void resetNVC()
    {
      // LIMPIAR LA MEMORIA NVS
      nvs_flash_erase();
      nvs_flash_init();
      while (true)
        ;
    }

    bool invAct(int i)
    {
      if (i == 0)
      {
        return true;
      }
      else
      {
        if (invitadActive)
        {
          return true;
        }
        else
        {
          return false;
        }
      }
    }