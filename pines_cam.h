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

const char *welcome = "📱 COMANDOS ESP32-CAM\n/alertDura : on/off alerta de duracion del movimiento.\n/apagAuto: on/off apagarse de: 19:00 - 07:00.\n/configDatos: AP para config. las credenciales.\n/updateOTA: Activar funcion de actualizacion mediante OTA.\n/alertBotLento: on/off notificar si la conexion con el bot va lento.\n/alertBotLento: on/off de las alertas cuando la conexion con el bot esta lento.\n/iniciarVideo: Iniciar transmision de video a travez de su red local.\n/pararVideo: Detener la transmision de video en su red local.\n/estadoInv: on/off el usuario invitado.\n/estadoInv: on/off del usuario invitado.\n/reiniciar: Reiniciar la ESP32 - CAM.\n\n💡 INFORMACION DEL LED\n- PARPADEO_RAPIDO: Esta en modo config. de credenciales.\n- PARPADEO_NORMAL: Calibrando sensor al iniciar.\n- UN_PARPADEO: Captura una foto.\n- DOS_PARPADEOS: Error de acceso a la memoria.\n- ENCENDIDO: actualizacion OTA o duracion del mov. detectado (solo si esta activado la opcion).\n\n🛜 CONFIGURAR WIFI\n Cuando no encuentra conexion al WiFi el dispositivo crea el AP con el nombre ESPCAM-BOT donde debe de ingresar esos datos.";
const char *bienv2 = "🚔 == ESPCAM EN LINEA == 🚨🚨";