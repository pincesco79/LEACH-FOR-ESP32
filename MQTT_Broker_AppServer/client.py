import paho.mqtt.client as mqtt
import sys
import random
import sqlite3
import json

mqtt_broker = "localhost"
mqtt_port = 1883
client_id = f'python-mqtt-{random.randint(0, 1000)}'

def init_db():
    conn = sqlite3.connect("iot_network.db")
    cursor = conn.cursor()
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS sensor_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sensor_type TEXT,
            value REAL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    """)
    conn.commit()
    conn.close()

def store_data_in_db(topic, value):
    try:
        val = float(value)
    except ValueError:
        print(value + '\n')
        print(topic + '\n')
        print("Invalid data received")
        return
    
    sensor_map = {
        "sensor/Temp": "Temp",
        "sensor/Hum": "Hum",
        "sensor/Soil": "Soil",
        "sensor/Rain": "Rain"
    }
    
    sensor_type = sensor_map.get(topic, "Unknown")
    
    conn = sqlite3.connect("iot_network.db")
    cursor = conn.cursor()
    cursor.execute("INSERT INTO sensor_data (sensor_type, value) VALUES (?, ?)", (sensor_type, val))
    conn.commit()
    print("Data stored in DB!\n")
    cursor.close()
    conn.close()

def onMessage(client, userdata, msg):
    print(msg.topic + ": " + msg.payload.decode())
    store_data_in_db(msg.topic, msg.payload.decode())

def onConnect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("Successfully connected to MQTT broker!")
        client.subscribe("sensor/Rain")
        client.subscribe("sensor/Temp")
        client.subscribe("sensor/Soil")
        client.subscribe("sensor/Hum")
    else:
        print("Could not connect to MQTT broker!")

init_db()

client = mqtt.Client(client_id=client_id, callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = onConnect
client.on_message = onMessage
client.username_pw_set("dayana", "12345")

if client.connect(mqtt_broker, mqtt_port, 60) != 0:
    print("Could not connect to MQTT broker!")
    sys.exit(-1)

try:
    print("Press CTRL+C to exit...")
    client.loop_forever()
except KeyboardInterrupt:
    print("Disconnecting from broker")
    client.disconnect()
