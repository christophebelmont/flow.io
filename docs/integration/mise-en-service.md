# Mise en service — Waveshare ESP32-S3

Cette page décrit la compilation, le flash et les premières vérifications du firmware principal flow.io sur **Waveshare ESP32-S3-POE-ETH-8DI-8RO**.

> Les relais peuvent commuter des tensions dangereuses. Couper et vérifier l'alimentation avant tout câblage; faire réaliser le raccordement secteur par une personne qualifiée.

## 1. Cible et prérequis

Le firmware à utiliser est l'environnement PlatformIO `Waveshare-ESP32-S3`. Contrairement à l'ancienne architecture `FlowIO` + `Supervisor`, ce profil exécute sur le même ESP32-S3:

- la logique métier et les équipements piscine;
- les entrées/sorties et les extensions capteurs;
- Ethernet, Wi-Fi, provisioning et interface web;
- MQTT, Home Assistant et mises à jour;
- HMI, buzzer, LED d'état et TFT local.

Prévoir PlatformIO, un câble USB-C de données et une alimentation adaptée à la version de la carte. La fiche Waveshare indique une alimentation 5 V par USB-C ou 7–36 V par bornier; la variante PoE accepte aussi l'alimentation réseau IEEE 802.3af.

## 2. Compiler et flasher

Depuis la racine du dépôt:

```sh
~/.platformio/penv/bin/pio run -e Waveshare-ESP32-S3
~/.platformio/penv/bin/pio run -e Waveshare-ESP32-S3 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

Si plusieurs ports série sont présents, ajouter `--upload-port <port>` à la commande de flash et `-p <port>` au moniteur.

L'environnement exécute les scripts de génération de version, modèle de données, manifeste Runtime UI et image SPIFFS avant l'export des binaires.

## 3. Câblage intégré de la carte

### Réseau et bus

| Interface | Affectation compilée |
|---|---|
| W5500 Ethernet | MOSI 13, MISO 14, SCLK 15, CS 16, INT 12, RST 39 |
| I2C IO | SDA 42, SCL 41, 400 kHz |
| UART HMI | UART2, RX 44, TX 43, 115200 bauds |
| Console | UART0 / USB, 115200 bauds |
| OneWire eau | GPIO20 |
| OneWire air | GPIO19 |
| Buzzer actif | GPIO46, actif haut |

Le bus I2C utilise par défaut le TCA9554 `0x20`, le MCP23017 `0x21`, l'INA226 `0x40`, le SHT40 `0x44`, les ADS1115 `0x48` et `0x49`, le BMP280 `0x76` et le BME688 `0x77`. Ne raccorder que les périphériques réellement présents et éviter les collisions d'adresse.

### Entrées digitales isolées

| Borne/canal | GPIO | IO slot | Binding port |
|---|---:|---|---:|
| DI1 | 4 | `i00` | 200 |
| DI2 | 5 | `i01` | 201 |
| DI3 | 6 | `i02` | 202 |
| DI4 | 7 | `i03` | 203 |
| DI5 | 8 | `i04` | 204 |
| DI6 | 9 | `i05` | 205 |
| DI7 | 10 | `i06` | 206 |
| DI8 | 11 | `i07` | 207 |

Ces entrées sont des IO génériques et n'ont pas de domain slot Pool par défaut.

### Relais

| Relais | IO slot | Binding port | Rôle par défaut |
|---|---|---:|---|
| CH1 / EXIO1 | `d00` | 300 | filtration |
| CH2 / EXIO2 | `d01` | 301 | pompe pH |
| CH3 / EXIO3 | `d02` | 302 | pompe chlore |
| CH4 / EXIO4 | `d03` | 303 | robot |
| CH5 / EXIO5 | `d04` | 304 | remplissage |
| CH6 / EXIO6 | `d05` | 305 | électrolyseur |
| CH7 / EXIO7 | `d06` | 306 | libre |
| CH8 / EXIO8 | `d07` | 307 | chauffage |

Toutes les sorties sont actives à l'état haut et démarrent à OFF. `d00` préserve l'état matériel du latch TCA9554 lors d'un redémarrage chaud.

## 4. Raccorder la carte Companion

La carte flow.io Companion se raccorde au connecteur d'extension du Waveshare au moyen d'une nappe dédiée. Cette liaison reporte les alimentations et signaux utiles vers les borniers fonctionnels de la Companion, sans modifier leur affectation logicielle.

Procédure recommandée:

1. couper toutes les alimentations du Waveshare, de la Companion et des équipements;
2. raccorder la nappe sur les deux connecteurs en respectant son orientation et la broche 1;
3. vérifier que chaque connecteur est complètement engagé et qu'aucune rangée n'est décalée;
4. raccorder les capteurs et modules sur les ports fonctionnels de la Companion;
5. contrôler la continuité et les tensions avant de remettre l'ensemble sous tension.

La Companion présente notamment les raccordements pH, ORP, pression d'eau, températures d'eau et d'air, niveaux du bassin et des cuves, compteur d'eau, entrées digitales, extensions I2C et HMI Nextion. Elle permet ainsi de regrouper les modules piscine autour d'un câblage intégré plutôt que de les relier individuellement au connecteur interne du Waveshare.

Les libellés de la Companion décrivent le câblage physique. La correspondance logicielle reste définie par la chaîne `domain_slot -> io_slot -> binding_port` documentée dans la [cartographie IO](../core/waveshare-io-map.md).

## 5. TFT local

Le build de production active `FLOW_ENABLE_TFT_S3=1` et réserve:

| Signal ST7789 | GPIO |
|---|---:|
| Backlight | 21 |
| CS | 45 |
| DC | 1 |
| RST | 47 |
| MOSI | 2 |
| SCLK | 48 |

Ne pas affecter les binding ports génériques associés à ces GPIO tant que le TFT est actif.

## 6. Extensions et rôles métier

Les capteurs métier et les extensions MCP23017 ne correspondent pas nécessairement aux huit borniers DI/relai intégrés. Leur chaîne d'affectation est:

```text
domain_slot -> io_slot -> binding_port -> driver/canal physique
```

Exemples:

- `SensorWaterTemp -> a04 -> 120 -> DS18B20 GPIO20`;
- `SensorPoolLevel -> i11 -> 225 -> MCP23017 GPA5`;
- `ActuatorFiltrationPump -> d00 -> 300 -> TCA9554 EXIO1`.

Consulter la [cartographie exhaustive](../core/waveshare-io-map.md) avant de câbler les capteurs ou de modifier un binding.

## 7. Vérifications au premier démarrage

Vérifier dans le moniteur série:

1. l'identification du profil Waveshare et la détection de la PSRAM;
2. l'initialisation de l'I2C, des expanders et des drivers présents;
3. l'obtention d'une adresse Ethernet ou Wi-Fi;
4. le démarrage de `time`, `io`, `poollogic`, `pooldev` et `sysmon`;
5. si MQTT est activé, la connexion au broker et les publications runtime;
6. l'absence d'erreur de domain slot non configuré ou sans binding.

Dans l'interface web, la page **Entrées/Sorties** permet de contrôler la topologie, les valeurs runtime et l'affectation des binding ports. Vérifier d'abord les entrées sans charge, puis chaque relais avec un circuit de test adapté avant de raccorder les équipements piscine.

## 8. Profils secondaires

Les environnements `FlowIO` et `Supervisor` restent compilables pour les installations historiques à deux ESP32. Ils ne doivent pas être utilisés pour une nouvelle installation Waveshare sauf besoin de compatibilité explicite.
