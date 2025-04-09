# ESP32-CAM con TelegramBot

Consiste en un sistema de monitoreo de seguridad basado en un **ESP32-CAM**, que interactúa con un bot de **Telegram**. El dispositivo captura fotos usando la cámara integrada y las envía a los usuarios registrados cuando detecta movimiento mediante un sensor PIR. Además, tiene múltiples funcionalidades controlables a través de Telegram, como **activar o desactivar el modo de vigilancia, tomar fotos bajo demanda, o controlar el modo de ahorro de energía.**

> ⚠️ Al encenderse, la ESP32-CAM intenta cargar las configuraciones almacenadas. Si no encuentra el token del bot, las credenciales Wi-Fi o al menos un usuario, entra en modo configuración usando **WiFiManager** para obtener estos datos e intentar corregirlos.

<p align="center">
  <img src="/imagenes/Screenshot_0.jpg" alt="Foto" width="700">
</p>

---

## Configuracion del Entorno

- **IDE:** Arduino `v1.8.x`
- **Framework:** Arduino ESP32 `v3.0.4`
  Usar el Arduino IDE `2.x.x` para descargar el framework de esp32.
- **Board:** `AI Thinker ESP32-CAM`
- **Partition:** `Regular 4MB with spiffs(1.2MB APP/1.5MB SPIFFS).`
  En caso de usar la funcion de **OTA**: `Minimal SPIFFS(1.9 APP with OTA/ 190KB SPIFFS).`

### Librerías

- **WiFiManager** `v2.0.17`: Para gestionar la conexión Wi-Fi.
- **UniversalTelegramBot** `v1.3.0`: Para la integración con el bot de Telegram.
- **ArduinoJson** `v7.2.1`: Para el manejo de JSON en la comunicación con el bot de Telegram.
- **TelnetStream** `v1.3.0`: Usar solo si no dispone de una comunicacion serial.

---

## Componentes

- 1x Board ESP32-CAM y antena externa (opcional).
- 1x Sensor de movimiento PIR HC-SR501.
- 1x Step-Up MT3608 DC-DC
- 1x Modulo de carga TP4056.
- 1x Portabateria 18650.
- 1x Bateria 18650.
- 1x Interruptor 6P SPDT.
- 2x Resistencias 100 kohm, 10 kohm y 100 ohm.
- 1x Diodo led.
- 1x Conector para la alimentacion

---

## Diagrama

<p align="center">
  <img src="/imagenes/PirCam_TelegramBot_ESPCAM.jpg" alt="Circuito" width="800">
</p>

---

> ⚠️ Primero verificar que funcione bien la **PSRAM** de la la placa **ESP32-CAM** usando el modelo de placa: **AI Thinker ESP32-CAM** al parecer algunas unidades tienen ese defecto desde fabrica.

```c
#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  delay(1000);

  if (psramFound()) {
    Serial.println("PSRAM está habilitada y funcionando.");
  } else {
    Serial.println("PSRAM no está habilitada o disponible.");
  }
}

void loop() {
}
```

> Si ocurren fallas como `Error al iniciar la camara`, `Error al capturar la foto` o `reinicios por culpa del WDT` recomendacion desactivar la funcion del **Watchdogs**.

---

## Caracteristicas del Bot de Telegram

- **`/estado`**: Muestra el estado actual del sistema:

  - Hora actual y el número de usuarios en modo vigilancia.
  - Estado del dispositivo (ON o OFF).
  - Modo vigilancia (ON o OFF).
  - Modo de ahorro de energía (ON o OFF).
  - Alerta de duración del movimiento (ON o OFF).
  - Apagado automatico: (ON o OFF);
  - Temperatura interna de la placa **(su sensor no es confiable)**.
  - Intensidad de la señal de red Wi-Fi.
  - Tipo de alimentacion externa o bateria.

- **`/VIGILAR` o `/NO-VIGILAR`**: Activa o desactiva el modo vigilancia. Cuando está activado, el ESP32-CAM enviará una foto cada vez que el sensor PIR detecte movimiento.
- **`/foto`**: Permite al usuario solicitar una foto en cualquier momento.
- **`/APAGAR` o `/ENCENDER`**: Apaga o enciende el dispositivo. Al apagarse, el ESP32-CAM entra en modo de bajo consumo (deep sleep), pero se despierta periódicamente para revisar nuevos comandos.
- **`/modDormido` o `/modDespierto`**: Activa o desactiva el modo de ahorro de energía. Cuando está activado, el ESP32-CAM entra en deep sleep y solo se despierta al detectar movimiento para enviar una foto.
- **`/comandos`**: Muestra **mas comandos** con una descripcion de sus funciónes e informacion de las **alertas del led**:

  - **`/alertDura`**: on/off encender el led durante el tiempo que sensor pir detecte movimiento.
  - **`/apagAuto`**: on/off El dispositivo se apagara por las NOCHES.
  - **`/configOTA`**: Activar la funcion de actualizacion mediante OTA (solo disponible para el adm).
  - **`/configDatos`**: El dispositivo se se reinicia para poder config. los datos de inicio con **WifiManager**(`credenciales WiFi`, `token`, `ID de usuarios`) (solo disponible para el adm).
  - **/reiniciar**: Reinicia la ESP32-CAM.

  ```c
  💡 INFORMACION DEL LED
  - Parpadeo_Rapido: Esta en modo configuracion o error de acceso a la memoria.
  - Parpadeo_Normal: Calibrando sensor luego de iniciar.
  - Un Parpadeo: Captura una foto.
  - Encendido: Cuando se detecta movimiento (solo si esta activado la opcion `/alertDura`).
  ```

- **Alerta de batería**: Notificar si la batería baja por debajo del 15% cuando se esta usando como fuente de alimentacion la bateria.
- **Lentitud con el bot**: Cuando esta en modo-vigilar notificar si el bot tarda en verificar la existencia de nuevos mensajes y eso genera un retraso en cada interaccion.
  > ⚠️ Las mensajes de posible fallo se muestra en los mensajes de bienvenida o en el **portal de WifiManager**.

### Funciones Dev

- **Depuración Remota con TelnetStream:** Incorpora la librería TelnetStream, que permite realizar la depuración del dispositivo de forma remota a través de la red Wi-Fi mediante una conexión Telnet. Esto resulta especialmente útil para analizar el comportamiento del dispositivo en tiempo real sin necesidad de una conexión física al puerto serial, ideal para dispositivos como la ESP32-CAM.

- **Actualización OTA (Over-the-Air):** **(Aunque esta funcion ya biene integrada en WifiManager)**. El proyecto incluye la funcionalidad de actualización OTA, lo que permite cargar nuevo firmware de forma remota a través de la red Wi-Fi. Esto resulta especialmente útil en la ESP32-CAM, ya que este modelo no cuenta con un puerto USB integrado. Esta función simplifica el mantenimiento sin necesidad de acceso físico al dispositivo.

---

## Capturas

<p align="center">
  <img src="/imagenes/Screenshot_4.jpg" alt="Estado 1" width="45%">
  <img src="/imagenes/Screenshot_1.jpg" alt="Estado 2" width="45%">
</p>

<p align="center">
  <img src="/imagenes/Screenshot_2.jpg" alt="Estado 1" width="45%">
  <img src="/imagenes/Screenshot_3.jpg" alt="Estado 2" width="45%">
</p>

---

## Flujo de Operaciones

1. **Encendido y Carga de Configuración**: Al encenderse, el dispositivo carga la configuración desde la memoria flash. Si los datos no están disponibles (`token del bot`, `credenciales Wi-Fi`, o `usuario registrado`), entra en **modo configuración** **`(AP mode)`** usando **WiFiManager** para recopilar la información.

2. **Interacción con el Bot**: Una vez configurado, el bot de Telegram interactúa con el dispositivo a través de los comandos definidos. Cada comando tiene una acción específica, como cambiar el modo de vigilancia, tomar fotos o modificar configuraciones de ahorro de energía.
3. **Eventos**: Si hubo un reinicio inesperado informar las causa o de algun otro problema referido a los datos de inicio.
4. **Notificaciones**: El sistema envía notificaciones a los usuarios registrados en caso de que la batería baje por debajo del 15%, calentamiento y cuando el sensor PIR detecta movimiento.

---

## Consideraciones

- Tener una conexión Wi-Fi estable para interactuar con el bot de Telegram.
- El modo **deep sleep** es útil cuando se utiliza la batería para prolongar la duracion de la batería pero desde la detencion del movimiento hast que sea enviado hay un retraso mas largo de milisegundos que al no usar el modo deep sleep.

> ⚠️ Si el dispositivo está conectado a una fuente de alimentación externa, se recomienda no utilizar la batería simultáneamente. El modulo TP4056 no esta diseñado para cargar la bateria y alimentar el dispositivo al mismo tiempo, lo que podría provocar sobrecargas y ser peligroso. Usa la batería únicamente cuando no dispongas de una fuente externa.

---

## Alternar entre fuente externa y bateria

Circuito para conmutar la alimentacion entre la bateria y una fuente externa 5V. [ver imagen](imagenes/conmutacion.jpeg)

- [Añadir cargador de batería a ESP8266 y ESP32](https://emariete.com/cargador-bateria-esp8266-esp32-bien-hecho/)

---

## Nivel de la bateria

Para usar la funcion del voltimetro es necesario modificar la placa ESP32-CAM desoldando el `GPIO33` que esta asignado al led integrado de la placa. [ver imagen](imagenes/voltimetro.jpeg)

- [Battery Status Monitoring System using ESP8266 & Arduino IoT Cloud](https://iotprojectsideas.com/battery-status-monitoring-system-using-esp8266-arduino-iot-cloud/)
- [ESP8266 Monitor its Own Battery Level using IoT](https://iotprojectsideas.com/esp8266-monitor-its-own-battery-level-using-iot/)
- [IoT Based Battery Status Monitoring System using ESP8266](https://how2electronics.com/iot-based-battery-status-monitoring-system-using-esp8266/)
