# Documentation flow.io

<p align="center">
  <img src="pictures/Logo_flowio.png" alt="Logo flow.io" width="260">
</p>

Le profil matériel et firmware de référence de flow.io est `Waveshare-ESP32-S3`. Il rassemble la logique piscine, les E/S, le réseau, l'interface web, MQTT, Home Assistant, les mises à jour et l'HMI dans un même ESP32-S3.

## Ensemble matériel de référence

### Contrôleur Waveshare ESP32-S3

Le firmware cible le module industriel **Waveshare ESP32-S3-POE-ETH-8DI-8RO**.

<p align="center">
  <img src="pictures/waveshare-esp32-s3-poe-eth-8di-8ro.png" alt="Module Waveshare ESP32-S3-POE-ETH-8DI-8RO utilisé par flow.io" width="520">
</p>

La carte fournit 8 entrées digitales isolées, 8 relais, Ethernet W5500, Wi-Fi/BLE, RS485, RTC, buzzer, LED RGB et boîtier rail DIN. Le profil flow.io complète ces ressources avec ses capteurs analogiques, ses sondes 1-Wire et ses extensions I2C. Voir la [fiche officielle Waveshare](https://www.waveshare.com/esp32-s3-eth-8di-8ro.htm).

### Carte flow.io Companion

La carte **flow.io Companion** sert d'interface d'intégration entre le contrôleur Waveshare et les équipements de la piscine.

<p align="center">
  <img src="pictures/flowio-companion-waveshare.png" alt="Carte flow.io Companion pour Waveshare ESP32-S3" width="820">
</p>

Une nappe dédiée relie le connecteur d'extension du Waveshare à la Companion. Les signaux sont ainsi reportés sur des connecteurs et borniers clairement identifiés pour présenter directement les ports piscine: pH, ORP, pression et niveau d'eau, températures, niveaux des cuves, compteur d'eau, entrées digitales, I2C et HMI Nextion.

Cette organisation facilite le raccordement des modules piscine dans un ensemble compact et intégré. Elle réduit les liaisons fil à fil, simplifie la mise en service et rend le câblage plus lisible et maintenable. Le Waveshare conserve l'exécution du firmware et le pilotage des E/S; la Companion assure leur présentation et leur distribution physique.

## Démarrage rapide

Compilation et flash du firmware principal:

```sh
~/.platformio/penv/bin/pio run -e Waveshare-ESP32-S3
~/.platformio/penv/bin/pio run -e Waveshare-ESP32-S3 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

Continuer avec la [mise en service matérielle](integration/mise-en-service.md), puis utiliser la [cartographie IO Waveshare](core/waveshare-io-map.md) pour le câblage et les affectations.

## Comprendre la cartographie IO

Le profil sépare le besoin métier, l'endpoint logiciel et la ressource physique:

```text
domain_slot (métier)  ->  io_slot (endpoint)  ->  binding_port (matériel)
Filtration Pump       ->  d00                 ->  300 / EXIO1
Water Temperature     ->  a04                 ->  120 / OneWire GPIO20
Pool Level            ->  i11                 ->  225 / MCP23017 GPA5
```

| Niveau | Définition | Persistance |
|---|---|---|
| `domain_slot` | rôle stable du domaine Pool, par exemple ORP, pompe de filtration ou niveau bassin | compilé dans le domaine |
| `io_slot` | endpoint logique de `IOModule`: `aNN`, `iNN` ou `dNN` | structure compilée, configuration du slot en NVS |
| `binding_port` | port physique sélectionnable: GPIO, expander, ADS1115, OneWire ou capteur I2C | valeur du slot stockée en NVS |

La page [Binding ports, IO slots et domain slots](core/waveshare-io-map.md) contient l'inventaire exhaustif et les affectations par défaut.

### Affectations métier principales

| Domain slot | IO slot | Binding port par défaut |
|---|---|---|
| ORP / pH / pression / analogique libre | `a00..a03` | ADS1115 interne `100..103` |
| température eau / air | `a04..a05` | OneWire `120..121` |
| courant / tension | `a06..a07` | INA226 `140` / `139` |
| PIR / niveaux | `i08..i11` | MCP23017 `220`, `223..225` |
| compteur d'eau | `i12` | entrée optocouplée GPIO4, port `200` |
| filtration / pH / chlore / robot | `d00..d03` | `EXIO1..EXIO4`, ports `300..303` |
| remplissage / électrolyse / éclairage / chauffage | `d04..d07` | `EXIO5..EXIO8`, ports `304..307` |

Les entrées isolées de la carte occupent `i00..i07`, mais GPIO4 est réservé par défaut au compteur `i12`; le binding initial de `i00` est donc « non connecté ». `d06` pilote l'éclairage (`Lights`) via `EXIO7`. Les sorties `d08..d15` utilisent le MCP23017 et restent sans rôle métier Pool par défaut.

## Matériel et interfaces du profil

| Ressource | Configuration du firmware |
|---|---|
| Ethernet | W5500: MOSI 13, MISO 14, SCLK 15, CS 16, INT 12, RST 39 |
| I2C IO | SDA 42, SCL 41, 400 kHz |
| Entrées digitales carte | GPIO 4 à 11, slots `i00..i07` |
| Relais carte | TCA9554 `0x20`, `EXIO1..EXIO8`, slots `d00..d07` |
| Extension MCP23017 | `0x21`, GPA en entrée et GPB en sortie |
| OneWire | eau GPIO20, air GPIO19 |
| HMI série | UART2, RX 44, TX 43, 115200 bauds |
| TFT ST7789 local | BL 21, CS 45, DC 1, RST 47, MOSI 2, SCLK 48 |
| Buzzer | GPIO46, actif haut |

Les GPIO 1, 2, 21, 45, 47 et 48 sont réservés au TFT dans l'environnement de production `Waveshare-ESP32-S3`. Ils ne doivent pas être réaffectés comme E/S génériques tant que `FLOW_ENABLE_TFT_S3=1`.

## Parcours de lecture

### Installer et adapter

- [Mise en service matérielle et flash](integration/mise-en-service.md)
- [Cartographie IO du profil Waveshare](core/waveshare-io-map.md)
- [Adapter le projet à un autre domaine](integration/adaptation-domaine.md)

### Comprendre l'architecture

- [Architecture générale](core/architecture.md)
- [Profils, cartes, domaines et bootstrap](core/profiles-board-domain-app.md)
- [Services Core](core/services.md)
- [Modèle `ConfigStore` / `DataStore` / `EventBus` / MQTT](core/data-event-model.md)
- [Topologie MQTT](core/mqtt-topics.md)
- [Exposition Runtime UI](core/runtime-ui-exposure.md)
- [Matrice qualité du profil Waveshare](core/module-quality-gates.md)
- [Schéma d'ensemble du programme](program_structure.md)

### Références historiques

Ces pages restent utiles aux installations à deux contrôleurs `FlowIO` / `Supervisor`, mais elles ne décrivent pas la cible matérielle principale:

- [Protocole I2C `FlowIO` ↔ `Supervisor`](core/flow-supervisor-i2c-protocol.md)
- [Empreinte mémoire du profil historique `FlowIO`](core/memory-footprint-flowio.md)

## Composition du firmware principal

L'environnement `[env:Waveshare-ESP32-S3]` active `FLOW_PROFILE_WAVESHARE=1` et `FLOW_BOARD_WAVESHARE_ESP32_S3=1`. Le bootstrap `src/Profiles/Waveshare/WaveshareBootstrap.cpp` enregistre notamment:

- les services Core de logs, configuration, état runtime, commandes et événements;
- Ethernet, Wi-Fi, provisioning et interface web;
- mise à jour du firmware, temps/RTC, MQTT et Home Assistant;
- HMI UDP/série, buzzer et TFT local;
- `IOModule`, `PoolLogicModule`, `PoolDeviceModule` et supervision système.

Les environnements `FlowIO`, `Supervisor`, `FlowConnectDisplay` et `Micronova` sont conservés comme profils secondaires ou historiques. `WaveshareWokwi` permet la simulation du profil principal.

## Capacités statiques Waveshare

| Domaine | Capacité compile-time |
|---|---:|
| Entrées analogiques / slots config | 16 / 16 |
| Entrées digitales / slots config | 13 / 13 |
| Sorties digitales / slots config | 16 / 16 |
| Domain slots / bindings domaine-IO | 20 / 20 |
| Indices `PoolDevice` | 8, dont 7 presets métier |
| Entités Home Assistant: sensors / binary sensors / switches | 48 / 16 / 16 |
| Entités Home Assistant: numbers / buttons / selects | 30 / 24 / 6 |
| Routes runtime MQTT | 112 |
| File EventBus | 40 |
| Variables de configuration | 768 |

## Référence par module

- [LogHubModule](modules/LogHubModule.md)
- [LogDispatcherModule](modules/LogDispatcherModule.md)
- [LogSerialSinkModule](modules/LogSerialSinkModule.md)
- [LogAlarmSinkModule](modules/LogAlarmSinkModule.md)
- [EventBusModule](modules/EventBusModule.md)
- [ConfigStoreModule](modules/ConfigStoreModule.md)
- [DataStoreModule](modules/DataStoreModule.md)
- [CommandModule](modules/CommandModule.md)
- [SystemModule](modules/SystemModule.md)
- [SystemMonitorModule](modules/SystemMonitorModule.md)
- [HMIModule](modules/HMIModule.md)
- [SupervisorHMIModule](modules/SupervisorHMIModule.md)
- [AlarmModule](modules/AlarmModule.md)
- [WifiModule](modules/WifiModule.md)
- [TimeModule](modules/TimeModule.md)
- [MQTTModule](modules/MQTTModule.md)
- [HAModule](modules/HAModule.md)
- [IOModule](modules/IOModule.md)
- [PoolLogicModule](modules/PoolLogicModule.md)
- [PoolDeviceModule](modules/PoolDeviceModule.md)
