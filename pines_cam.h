// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

const char *emReloj = "⏰ ";
const char *emDorm = "😴";

const char *welcome = "📱 COMANDOS ESP32-CAM\n/alertDura: on/off alerta de duracion.\n/apagAuto: on/off apagarse de: 19:00 - 07:00.\n/configDatos: Config. de las credenciales.\n/configOTA: Activar funcion de actualizacion mediante OTA.\n/reiniciar: Reiniciar la ESP32-CAM.\n\n💡 INFORMACION DEL LED\n- Parpadeo-Rapido: Esta en modo configuracion o error de acceso a la memoria.\n- Parpadeo-Normal: Calibrando sensor al iniciar.\n- Un Parpadeo: Captura una foto.\n- Encendido: Cuando se detecta movimiento (solo si esta activado la opcion).\n\n🛜 CONFIGURAR WIFI\nCuando no encuentra conexion al WiFi el dispositivo crea el AP ESPCAM-BOT para solicitar los datos.";
const char *bienv = "BIENVENIDO 👋\nCalibrando SENSOR, esperar 5 seg...⏳";
const char *bienv2 = "🚔 == ESPCAM EN LINEA == 🚨🚨";