na linuxu zažen install_mqtt.sh(chmod +x install_mqtt.sh)(./install_mqtt.sh)<br />
v 1. terminalu: mosquitto_sub -h localhost -t '#'<br />
iz 2. terminala lahko pošilaš stringe preko: mosquitto_pub -h localhost -t test/topic -m 'hello'<br />
vpiši svoj ip na frontend/src/constants/mqtt_ip.js<br />
na VM nism sigurn na dela ce je network na NAT ampak mogl bi delat na Bridged Adapter, ampak to dela samo z wifi(hotspot), mogoče dela na eduroam ampak nism  sigurn<br />
na http://localhost:3000/mqtt je log za mqtt<br />