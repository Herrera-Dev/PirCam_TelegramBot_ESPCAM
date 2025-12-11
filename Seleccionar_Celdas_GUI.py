#!/usr/bin/env python3
import json
from PIL import Image, ImageDraw
import matplotlib.pyplot as plt
import sys

# CALCULO DE CELDAS
def calcular_celdas(img_w, img_h, filas, columnas):
    cell_w = img_w / columnas
    cell_h = img_h / filas

    celdas = []
    for f in range(filas):
        for c in range(columnas):
            x1 = int(c * cell_w)
            y1 = int(f * cell_h)
            x2 = int((c + 1) * cell_w)
            y2 = int((f + 1) * cell_h)
            celdas.append({
                "id": f"{f},{c}",
                "fila": f,
                "columna": c,
                "x1": x1, "y1": y1,
                "x2": x2, "y2": y2
            })
    return celdas

# =============================
if __name__ == "__main__":

    if len(sys.argv) < 4:
        print("Uso: python3 Seleccionar_Celdas_GUI.py [imagen.jpg] [filas] [columnas]")
        sys.exit(1)

    imagen_path = sys.argv[1]
    filas = int(sys.argv[2])
    columnas = int(sys.argv[3])

    # Cargar imagen
    img = Image.open(imagen_path)
    w, h = img.size

    # Calcular celdas
    celdas = calcular_celdas(w, h, filas, columnas)

    # Lista de celdas seleccionadas
    seleccionadas = []

    # Mostrar imagen
    fig, ax = plt.subplots()
    ax.imshow(img)
    ax.set_title("Haz clic en las celdas. CERRAR VENTANA para guardar JSON e imagen")

    # Dibujar grid
    for c in celdas:
        ax.plot([c["x1"], c["x2"], c["x2"], c["x1"], c["x1"]],
                [c["y1"], c["y1"], c["y2"], c["y2"], c["y1"]],
                'r', linewidth=1)

    # Para clic
    def onclick(event):
        if event.xdata is None or event.ydata is None:
            return

        x, y = event.xdata, event.ydata

        for c in celdas:
            if c["x1"] <= x <= c["x2"] and c["y1"] <= y <= c["y2"]:
                if c not in seleccionadas:
                    seleccionadas.append(c)
                    print("Celda:", c["id"])
                    # Dibujar celda seleccionada en verde
                    ax.plot([c["x1"], c["x2"], c["x2"], c["x1"], c["x1"]],
                            [c["y1"], c["y1"], c["y2"], c["y2"], c["y1"]],
                            'g', linewidth=2)
                    fig.canvas.draw()
                break

    fig.canvas.mpl_connect('button_press_event', onclick)

    # Ventana interactiva
    plt.show()

    # Guardar JSON
    salida_json = "celdas.json"
    with open(salida_json, "w") as f:
        json.dump(seleccionadas, f, indent=4)

    print("\nJSON generado:", salida_json)

    # Guardar imagen marcada
    img_marked = img.copy()
    draw = ImageDraw.Draw(img_marked)

    for c in celdas:
        if c in seleccionadas:
            # Celda seleccionada en verde
            draw.rectangle([(c["x1"], c["y1"]), (c["x2"], c["y2"])], outline="green", width=3)
            draw.text((c["x1"] + 3, c["y1"] + 3), c["id"], fill="yellow")
        else:
            # Celda no seleccionada en rojo
            draw.rectangle([(c["x1"], c["y1"]), (c["x2"], c["y2"])], outline="red", width=2)
            draw.text((c["x1"] + 3, c["y1"] + 3), c["id"], fill="yellow")

    salida_png = "imagen_celdas.png"
    img_marked.save(salida_png)

    print("Imagen generada:", salida_png)
