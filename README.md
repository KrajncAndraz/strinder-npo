na linuxu zažen install_mqtt.sh(chmod +x install_mqtt.sh)(./install_mqtt.sh)__
v 1. terminalu: mosquitto_sub -h localhost -t '#'__
iz 2. terminala lahko pošilaš stringe preko: mosquitto_pub -h localhost -t test/topic -m 'hello'__
vpiši svoj ip na frontend/src/constants/mqtt_ip.js__
na VM nism sigurn na dela ce je network na NAT ampak mogl bi delat na Bridged Adapter, ampak to dela samo z wifi(hotspot), mogoče dela na eudoram ampak nism  sigurn__
na http://localhost:3000/mqtt je log za mqtt__