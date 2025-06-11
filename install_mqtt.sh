#!/bin/bash

echo "Installing MQTT (Mosquitto Broker and Clients)..."

sudo apt update

sudo apt install -y mosquitto mosquitto-clients

sudo systemctl enable mosquitto

# Backup the default config
sudo cp /etc/mosquitto/mosquitto.conf /etc/mosquitto/mosquitto.conf.bak

# Create custom config for MQTT with WebSocket support
sudo tee /etc/mosquitto/mosquitto.conf > /dev/null <<EOF
# MQTT over TCP
listener 1883
protocol mqtt

# MQTT over WebSocket
listener 9001 0.0.0.0
protocol websockets

allow_anonymous true
EOF

#Allow MQTT traffic through the firewall
sudo ufw allow 9001/tcp
#
# Restart Mosquitto to apply changes
sudo systemctl restart mosquitto

echo "Mosquitto installed and configured:"
echo "   • MQTT over TCP     : port 1883"
echo "   • MQTT over WebSocket: port 9001"
echo "   • Anonymous access  : ENABLED"
echo ""
echo "🔧 You can now test using:"
echo "   mosquitto_pub -h localhost -t test/topic -m 'hello'"
echo "   mosquitto_sub -h localhost -t '#'"

