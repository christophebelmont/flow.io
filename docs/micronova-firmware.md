# Firmware Micronova

Le profil `Micronova` est un firmware Flow.io dedie au pilotage d'une chaudiere a granules equipee d'une carte Micronova. Il est isole des profils piscine `FlowIO`, `Supervisor` et `FlowConnectDisplay`.

## Architecture

Le profil assemble les modules suivants :

- `LogHubModule`, `LogDispatcherModule`, `LogSerialSinkModule`
- `EventBusModule`
- `ConfigStoreModule`, `DataStoreModule`
- `WifiModule`, `WifiProvisioningModule`
- `WebInterfaceModule` pour le provisioning Wi-Fi et l'edition locale du `ConfigStore`
- `MQTTModule`
- `IOModule` pour la sortie digitale locale et la sonde DS18B20 locale
- `MicronovaBusModule`
- `MicronovaBoilerModule`
- `MicronovaMqttBridgeModule`
- `SystemModule`, `SystemMonitorModule`

Les modules piscine (`PoolLogicModule`, `PoolDeviceModule`, HMI piscine) et la configuration distante Supervisor/I2C ne sont pas embarques dans ce profil.

Le nom mDNS est fourni par le champ `system/devicename`. Sa valeur par defaut est `flowio`.

## Bus Micronova

`MicronovaBusModule` gere uniquement l'UART vers la carte Micronova.

Parametres par defaut :

- UART : `SERIAL_8N2`
- baudrate : `1200`
- `reply_timeout_ms` : `200`
- `turnaround_delay_ms` : `80`
- `repeat_delay_ms` : `100`
- RX : GPIO `17` sur le `BoardSpec` `MicronovaBoardRev1` (equivalent ESP32 de `GPIO0` sur le D1 Mini ESP8266 legacy)
- TX : GPIO `16` sur le `BoardSpec` `MicronovaBoardRev1` (equivalent ESP32 de `GPIO2` sur le D1 Mini ESP8266 legacy)
- `enable_rx_pin` : GPIO `21` sur le `BoardSpec` `MicronovaBoardRev1` (equivalent ESP32 de `GPIO4` sur le D1 Mini ESP8266 legacy)
- I2C local optionnel : SDA GPIO `22`, SCL GPIO `23` sur le `BoardSpec` `MicronovaBoardRev1`

Ces pins sont des valeurs de carte, pas des constantes metier du bus. `MicronovaBusModule` lit les defauts du `BoardSpec` puis `ConfigStore` peut toujours les surcharger localement avec les cles `micronova/uart`.

## IO locale Micronova

Le `BoardSpec` `MicronovaBoardRev1` expose aussi deux IO locales reutilisant `IOModule` :

- sortie digitale auxiliaire : GPIO `19`, endpoint IO `d00`, nom `aux_output`
- sonde de temperature DS18B20 : GPIO `18`, endpoint IO `a00`, nom `local_temperature`

La sortie est une sortie GPIO non momentanee, active a l'etat haut par defaut. La sonde DS18B20 utilise le bus 1-Wire local du board Micronova.

Les valeurs sont publiees par le producteur runtime MQTT de `IOModule` :

- `rt/io/output/d00`
- `rt/io/input/a00`

Le payload est le format JSON runtime IO standard :

```json
{"id":"d00","name":"aux_output","available":true,"value":false,"ts":12345}
```

Commandes MQTT pour la sortie GPIO19 :

- `micronova/io/output/d00/set`
- `micronova/command/aux_output/set`

Payloads acceptes :

- `ON`, `1`, `true` : active la sortie
- `OFF`, `0`, `false` : desactive la sortie

Pour eviter un conflit avec `enable_rx_pin` sur GPIO21, les defauts I2C de `IOModule` viennent du `BoardSpec` Micronova et pointent vers GPIO22/GPIO23. `IOModule` n'initialise le bus I2C que si un peripherique I2C est effectivement configure.

Les lectures envoient deux octets :

```text
readCode, address
```

Les ecritures envoient quatre octets :

```text
writeCode, address, value, checksum
```

Le checksum est calcule par le bus :

```text
checksum = writeCode + address + value
```

Le bus attend une reponse de deux octets pour les lectures :

```text
checksum, value
```

Il utilise des files circulaires statiques, priorise les ecritures sur les lectures et gere les repetitions ON/OFF sans `delay(100)`.

## ConfigStore

Principales branches locales :

- `micronova/uart` : `rx_pin`, `tx_pin`, `enable_rx_pin`
- `micronova/serial` : `baudrate`, `reply_timeout_ms`, `turnaround_delay_ms`, `repeat_delay_ms`, `offline_timeout_ms`
- `micronova/poll` : `normal_interval_ms`, `fast_interval_ms`, `fast_cycles`
- `micronova/registers/<registre>` : `read_code`, `write_code`, `address`, `scale`, `enabled`
- `micronova/registers/power_on` : `write_code`, `address`, `value`, `repeat_count`, `repeat_delay_ms`
- `micronova/registers/power_off` : `write_code`, `address`, `value`, `repeat_count`, `repeat_delay_ms`
- `io/output/d00` : `binding_port`, `d00_active_high`, `d00_initial_on`, `d00_momentary`
- `io/input/a00` : `binding_port`, `a00_c0`, `a00_c1`, `a00_prec`
- `io/drivers/bus` : `sda`, `scl` avec defauts board Micronova GPIO22/GPIO23
- `io/drivers/ds18b20` : `poll_ms`

Les adresses memoire Micronova peuvent varier selon les cartes et les firmwares de chaudiere. Verifier les registres sur le materiel cible avant de piloter une chaudiere reelle.

## Registres par defaut

| Cle | Lecture | Ecriture | Adresse | Scale | Actif |
| --- | ---: | ---: | ---: | ---: | --- |
| `stove_state` | `0x00` | - | `0x21` | `1.0` | oui |
| `room_temperature` | `0x00` | - | `0x01` | `0.5` | oui |
| `fumes_temperature` | `0x00` | - | `0x3E` | `1.0` | oui |
| `power_level` | `0x20` | `0xA0` | `0x89` | `1.0` | oui |
| `fan_speed` | `0x20` | `0xA0` | `0x8A` | `1.0` | oui |
| `target_temperature` | `0x20` | `0xA0` | `0x8B` | `1.0` | oui |
| `water_temperature` | TODO | - | TODO | `1.0` | non |
| `water_pressure` | TODO | - | TODO | `1.0` | non |
| `alarm_code` | TODO | - | TODO | `1.0` | non |

Commandes par defaut :

| Commande | Code | Adresse | Valeur | Repetitions |
| --- | ---: | ---: | ---: | ---: |
| ON | `0x80` | `0x58` | `0x5A` | 10 fois, 100 ms |
| OFF | `0x80` | `0x21` | `0x00` | 10 fois, 100 ms |

## Etat en francais

| Code | Texte |
| ---: | --- |
| 0 | Éteint |
| 1 | Démarrage |
| 2 | Chargement granulés |
| 3 | Allumage |
| 4 | Allumé |
| 5 | Nettoyage brasier |
| 6 | Nettoyage final |
| 7 | Veille |
| 8 | Granulés manquants |
| 9 | Échec allumage |
| 10 | Alarme |

Le firmware publie les libelles francais directement dans `DataStore` et MQTT. `power_state` est derive ainsi :

- `OFF` : codes `0`, `6`, `7`, `9`
- `ON` : codes `1`, `2`, `3`, `4`, `5`
- `ALARM` : codes `8`, `10`
- `UNKNOWN` : autre code

## DataStore

Valeurs runtime :

- `micronova/online`
- `micronova/stove_state_code`
- `micronova/stove_state_text`
- `micronova/power_state`
- `micronova/power_level`
- `micronova/fan_speed`
- `micronova/target_temperature`
- `micronova/room_temperature`
- `micronova/fumes_temperature`
- `micronova/water_temperature`
- `micronova/water_pressure`
- `micronova/alarm_code`
- `micronova/last_update_ms`
- `micronova/last_command`

Les changements sont relayes par `EventBus` via `micronova.value.updated` et `micronova.online.changed`.

## MQTT

Les topics sont formates par `MQTTModule`, donc ils sont prefixes par le `baseTopic` et le `deviceId` configures.

Topics d'etat :

- `micronova/status/connection`
- `micronova/status/state`
- `micronova/status/onoff`
- `micronova/status/power_state`
- `micronova/status/power_level`
- `micronova/status/stove_state`
- `micronova/status/stove_state_code`
- `micronova/status/alarm_code`
- `micronova/status/last_command`

Topics capteurs :

- `micronova/sensor/ambtemp`
- `micronova/sensor/fumetemp`
- `micronova/sensor/water_temperature`
- `micronova/sensor/water_pressure`
- `micronova/sensor/power`
- `micronova/sensor/fan`
- `micronova/sensor/tempset`

Topics de commande explicites :

- `micronova/command/power/set` : `ON`, `OFF`, `1`, `0`, `true`, `false`
- `micronova/command/power_level/set` : entier `0..5`
- `micronova/command/fan/set` : entier `0..5`
- `micronova/command/temperature/set` : entier
- `micronova/command/refresh` : payload non vide

Topic compact compatible :

- `micronova/command/cmd`

Payloads compacts :

- `ON` : allumer
- `OFF` : eteindre
- `E` : extinction, traitee comme `OFF`
- `P0` a `P5` : puissance
- `F0` a `F5` : ventilation
- `Txx` : consigne entiere, par exemple `T21`

Le flux de commande est :

```text
MQTT -> MicronovaMqttBridgeModule -> EventBus -> MicronovaBoilerModule -> MicronovaBusModule
```

Le flux de publication est :

```text
MicronovaBusModule -> MicronovaBoilerModule -> DataStore/EventBus -> MicronovaMqttBridgeModule -> MQTT
```

Apres une commande, le module chaudiere passe temporairement en polling rapide : 1 minute par defaut pendant 30 cycles. Il ne publie pas d'etat ON/OFF suppose, il attend les prochaines lectures de la carte.

## Compilation

Compiler le firmware Micronova :

```sh
pio run -e Micronova
```

Verifier les profils existants :

```sh
pio run -e FlowIO
pio run -e Supervisor
pio run -e FlowConnectDisplay
```
