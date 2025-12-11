import io, time, socket, subprocess, requests, imagehash, threading, os, json
from flask import Flask, Response, stream_with_context, render_template
from datetime import datetime
from plyer import notification
from PIL import Image

status = True
SOLO_HOST = True        # True: solo desde localhost, False: acceso en toda la red local
MDNS = "espcam-bot"     # Nombre del host ESPmDNS de la esp32-cam, http://espcam-bot.local

duracion = 60        # Segundos de grabación del video.
velocidad = 7        # FPS para renderizar el video.
PauseMov = 10        # Pausar luego de detectar movimiento.
sensibilMov = 2    # Sensibilidad de movimiento. Muy alta: 1-3 | Alta: 4-8 | Media: 9-15 | Baja: 16-30 | Muy baja: 31-64
detectMov = True
guardarImg = False
mostrarImg = False

celdasSelect = []

# -------------------------
app = Flask(__name__)

def resolve_mdns(hostname):
    try:
        return socket.gethostbyname(f"{hostname}.local")
    except socket.gaierror:
        return None

def buscar_ip(mdns):
    print(f"\n------------------------------")
    print(f"🔍 Buscando ESP32-CAM...")
    ip = resolve_mdns(mdns)
    while ip is None:
        ip = resolve_mdns(mdns)
        print(f"❌ No se encontró el dispositivo.")
        time.sleep(4)
    print(f"🌐 Abrir: http://localhost:5001")
    print(f"🏠 Solo localHost: {SOLO_HOST}")
    return ip
ESP32_IP = buscar_ip(MDNS)

def mostrarImagen_wmctrl(imagen, duracion):
    def _mostrar():
        ver = subprocess.Popen(['xdg-open', imagen])
        time.sleep(duracion)

        titulo_ventana = imagen.split('/')[-1]
        try:
            subprocess.run(['wmctrl', '-c', titulo_ventana], check=True)
        except subprocess.CalledProcessError:
            print("No se pudo cerrar la ventana (quizás el título no coincide).")
            pass
    threading.Thread(target=_mostrar, daemon=True).start()

def cargarCeldas():
    ruta = os.path.join(os.path.dirname(os.path.abspath(__file__)), "celdas.json")
    with open(ruta, "r") as f:
        return json.load(f)
celdasSelect = cargarCeldas()
last_hash_celdas = { c["id"]: None for c in celdasSelect }  

# -------------------------
@app.route("/")
def index():
    h = "localhost" if SOLO_HOST else "Red local"
    return render_template("index.html", esp32_ip=ESP32_IP, tipo_host=h)

@app.route("/stream")
def proxy_mjpeg():
    def generate(status):
        last_data_time = time.time()
        cooldown = time.time()
        buffer = b""
        hora_actual = None 

        while True:
            try:
                with requests.get(f"http://{ESP32_IP}/video", stream=True, timeout=5) as r:
                    r.raise_for_status()

                    if status:
                        print("- ✅ RUNNING: Streaming")
                        status = False

                    for chunk in r.iter_content(chunk_size=1024):
                        if not chunk:
                            if time.time() - last_data_time > 5:
                                raise RuntimeError("No se recibe de la ESP32-CAM")
                            continue

                        buffer += chunk
                        last_data_time = time.time()

                        # Buscar inicio y fin de un frame JPEG
                        start = buffer.find(b"\xff\xd8")
                        end = buffer.find(b"\xff\xd9")

                        if start != -1 and end != -1 and end > start:
                            frame_data = buffer[start:end + 2]
                            buffer = buffer[end + 2:]

                            try:
                                img_color = Image.open(io.BytesIO(frame_data))
                                img_gray = img_color.convert("L")
                                combined_region = Image.new("L", img_gray.size, 0)

                                # Copiar cada celda seleccionada sobre la imagen combinada
                                for celda in celdasSelect:
                                    x1, y1, x2, y2 = celda["x1"], celda["y1"], celda["x2"], celda["y2"]
                                    region = img_gray.crop((x1, y1, x2, y2))
                                    combined_region.paste(region, (x1, y1))

                                # Calcular hash de la región combinada
                                hash_actual = imagehash.phash(combined_region)

                                # Comparar con el hash anterior
                                mov_detectado = False
                                if last_hash_celdas.get("combined") is not None:
                                    diff = abs(hash_actual - last_hash_celdas["combined"])
                                    if diff > sensibilMov and (time.time() - cooldown > PauseMov) and detectMov:
                                        mov_detectado = True

                                # Guardar hash actual para la siguiente iteración
                                last_hash_celdas["combined"] = hash_actual

                                if mov_detectado:
                                    hora_actual = datetime.now().strftime("%H:%M:%S")
                                    print(f"[{hora_actual}] - Movimiento detectado 📸")

                                    ruta = "/tmp/mov_esp32cam.jpg"
                                    img_color.save(ruta)
                                    if guardarImg: 
                                        horafrm = hora_actual.replace(":", "-")
                                        ruta_script = os.path.dirname(os.path.abspath(__file__))
                                        ruta_completa = os.path.join(ruta_script, f"mov_esp32cam-{horafrm}.jpg")
                                        img_color.save(ruta_completa)

                                    notification.notify(
                                        title="📸 Movimiento Detectado",
                                        message=f"Movimiento en el area - {hora_actual}",
                                        timeout=5
                                    )
                                    os.system('paplay /usr/share/sounds/freedesktop/stereo/complete.oga')
                                    if mostrarImg: mostrarImagen_wmctrl(ruta, 4)

                                    cooldown = time.time()
                                                            

                                # Enviar frame al cliente
                                yield (
                                    b"--123456789000000000000987654321\r\n"
                                    b"Content-Type: image/jpeg\r\n\r\n" + 
                                    frame_data + b"\r\n"
                                )
                            except Exception as e:
                                print(f"Error procesando frame: {e}")
                                continue
            except Exception as e:
                print(f"- ❌ Error...")
                #print(f"- ❌ Error: {e}")

                # Mostrar frame vacío temporal mientras se reconecta
                yield (
                    b"--123456789000000000000987654321\r\n"
                    b"Content-Type: image/jpeg\r\n\r\n"
                    b"\xff\xd8\xff\xe0\x00\x10\x4a\x46\x49\x46\x00\x01\x01\x01"
                    b"\x00\x48\x00\x48\x00\x00\xff\xdb\x00\x43\x00\x03\x02\x02"
                    b"\x03\x02\x02\x03\x03\x03\x03\x04\x03\x03\x04\x05\x08\x05"
                    b"\x05\x04\x04\x05\x0a\x07\x07\x06\x08\x0c\x0a\x0c\x0c\x0b"
                    b"\x0a\x0b\x0b\x0d\x0e\x12\x10\x0d\x0e\x11\x0e\x0b\x0b\x10"
                    b"\x16\x10\x11\x13\x14\x15\x15\x15\x0c\x0f\x17\x18\x16\x14"
                    b"\x18\x12\x14\x15\x14\xff\xc0\x00\x0b\x08\x00\x01\x00\x01"
                    b"\x01\x01\x11\x00\xff\xc4\x00\x14\x00\x01\x00\x00\x00\x00"
                    b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff"
                    b"\xda\x00\x08\x01\x01\x00\x00\x3f\x00\xff\xd9"
                    b"\r\n"
                )
                notification.notify(title="❌ Error de video", message=f"Sin conexion...", timeout=5)
                status = True
                time.sleep(15)

    headers = {
        "Content-Type": "multipart/x-mixed-replace; boundary=123456789000000000000987654321",
        "Access-Control-Allow-Origin": "*",
    }
    return Response(stream_with_context(generate(status)), headers=headers)

@app.route("/grabar")
def grabar_fragmento():
    import os
    import re
    from datetime import datetime

    timestamp = datetime.now().strftime("%H%M%S_%Y")
    mjpg_file = f"recorded_videos/temp_{timestamp}.mjpg"
    mp4_file = f"recorded_videos/video_{timestamp}.mp4"
    url_stream = "http://localhost:5000/stream"

    try:
        inicio = time.time()
        with requests.get(url_stream, stream=True, timeout=5) as r:
            r.raise_for_status()
            with open(mjpg_file, "wb") as f:
                while time.time() - inicio < duracion:
                    chunk = r.raw.read(1024)
                    if not chunk:
                        break
                    f.write(chunk)

        tiempo_real = time.time() - inicio

        result = subprocess.run([
            "ffmpeg", "-y",
            "-r", str(velocidad),
            "-f", "mjpeg",
            "-i", mjpg_file,
            "-c:v", "libx264",
            "-pix_fmt", "yuv420p",
            "-r", str(velocidad),
            mp4_file
        ], capture_output=True, text=True)

        import re
        log_output = result.stderr
        matches = re.findall(r"frame=\s*(\d+)", log_output)
        frames = int(matches[-1]) if matches else 0

        os.remove(mjpg_file)
        fps_real = frames / tiempo_real if tiempo_real > 0 else 0

        return f"""
        ✅ Grabación finalizada<br>
        ⏱ Duración aprox.: {round(tiempo_real)} seg.<br>
        🎞️ Fotogramas: {frames}<br>
        📈 Esp32-Cam.: {round(fps_real)} FPS aprox.<br>
        📁 Archivo: <a href="/{mp4_file}" target="_blank">{mp4_file}</a>
        """

    except Exception as e:
        return f"❌ Error al grabar: {e}"

@app.route('/movimiento/<estado>')
def movimiento(estado):
    global detectMov
    if estado == 'on':
        detectMov = True
        print("✅ Notificacion de movimiento: ON")
        return "Detector activado"
    elif estado == 'off':
        detectMov = False
        print("🛑 Notificacion de movimiento: OFF")
        return "Detector desactivado"
    return "Estado inválido"

@app.route("/movimiento/status")
def movimiento_status():
    global detectMov
    print("Revisando estado")
    return {"activo": detectMov}
    
# -------------------------
if __name__ == "__main__":
    host = "localhost" if SOLO_HOST else "0.0.0.0"
    app.run(host=host, port=5001)
