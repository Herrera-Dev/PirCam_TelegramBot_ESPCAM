from flask import Flask, Response, stream_with_context, render_template
import requests
import time
import socket

app = Flask(__name__)
MDNS = "ESPCAM-BOT"     # Nombre del host ESPmDNS de la esp32-cam.
SOLO_HOST = True       # True: Visualización solo disponible desde el host y no toda la red local.

def resolve_mdns(hostname):
    ip = None
    try:
        ip = socket.gethostbyname(f"{hostname}.local")
        return ip
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
    
    # print(f"✅ IP de la ESP32-CAM: http://{ip}")
    print(f"🌐 Abrir: http://localhost:5000")
    print(f"🏠 Solo localHost: {SOLO_HOST}")
    return ip

ESP32_IP = buscar_ip(MDNS)
@app.route("/stream")
def proxy_mjpeg():
    def generate():
        last_data_time = time.time()
        
        while True:
            try:
                with requests.get(f"http://{ESP32_IP}/video", stream=True, timeout=5) as r:
                    r.raise_for_status()
                    
                    for chunk in r.iter_content(chunk_size=1024):
                        if chunk:
                            last_data_time = time.time()
                            yield chunk
                        else:
                            if time.time() - last_data_time > 5:
                                raise RuntimeError("ESP32-CAM no está enviando datos")
                            
            except Exception as e:
                # print(f"- 🔴 ERROR: {e}")
                print(f"- 🔴 ERROR: Algo salio mal.")
                yield (
                    b"--123456789000000000000987654321\r\n"
                    b"Content-Type: image/jpeg\r\n\r\n"
                    b"\xff\xd8\xff\xe0\x00\x10\x4a\x46\x49\x46\x00\x01\x01\x01\x00\x48\x00\x48\x00\x00\xff\xdb\x00\x43\x00\x03\x02\x02\x03\x02\x02\x03\x03\x03\x03\x04\x03\x03\x04\x05\x08\x05\x05\x04\x04\x05\x0a\x07\x07\x06\x08\x0c\x0a\x0c\x0c\x0b\x0a\x0b\x0b\x0d\x0e\x12\x10\x0d\x0e\x11\x0e\x0b\x0b\x10\x16\x10\x11\x13\x14\x15\x15\x15\x0c\x0f\x17\x18\x16\x14\x18\x12\x14\x15\x14\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00\xff\xc4\x00\x14\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00\xff\xd9"
                    b"\r\n"
                )
                time.sleep(1)

    headers = {
        "Content-Type": "multipart/x-mixed-replace; boundary=123456789000000000000987654321",
        "Access-Control-Allow-Origin": "*",
    }
    return Response(stream_with_context(generate()), headers=headers)

@app.route("/")
def index():
    if SOLO_HOST:
        h = "localhost"
    else:
        h = "Red local"
    return render_template("index.html", esp32_ip=ESP32_IP, tipo_host=h)

if __name__ == "__main__":
    host = "localhost" if SOLO_HOST else "0.0.0.0"
    app.run(host=host, port=5000)
