# Profil Waveshare — cartographie IO

Ce document est la référence du profil `Waveshare-ESP32-S3`. Il distingue trois niveaux:

- `binding_port`: ressource physique sélectionnable par la configuration IO;
- `io_slot`: endpoint logique publié par `IOModule` (`aNN`, `iNN`, `dNN`);
- `domain_slot`: rôle métier Pool relié à un `io_slot`.

La résolution se fait dans cet ordre:

```text
domain_slot (besoin métier) -> io_slot (endpoint stable) -> binding_port (ressource physique)
```

Le domaine choisit par exemple `ActuatorFiltrationPump -> d00`; la configuration de
`d00` choisit ensuite le port physique `300 / EXIO1`. La valeur `binding_port=0`
signifie « non connecté ». Un binding peut être changé en configuration sans modifier
le rôle métier ni les topics runtime de l'IO slot.

Les valeurs du profil ne nécessitent aucune migration NVS. La capacité IO compilée est
`{16, 13, 16, 16, 13, 16}`: 16 analogiques, 13 entrées digitales, 16 sorties digitales,
puis les trois capacités de configuration correspondantes.

## Topologie matérielle

| Composant | Bus/adresse | Fonction |
|---|---|---|
| Bus I2C IO | SDA GPIO42, SCL GPIO41, 400 kHz | Bus commun |
| TCA9554 | I2C `0x20`, expander `0` | EXIO1..EXIO8 |
| MCP23017 | I2C `0x21`, expander `1` | GPA0..GPA6 en entrée, GPB0..GPB7 en sortie |
| INA226 | I2C `0x40` | Shunt, tension, courant, puissance |
| SHT40 | I2C `0x44` | Température et humidité |
| ADS1115 interne | I2C `0x48` | A0..A3 single-ended |
| ADS1115 externe | I2C `0x49` | Paires différentielles A0-A1 et A2-A3 |
| BMP280 | I2C `0x76` | Température et pression |
| BME688 | I2C `0x77` | Température, humidité, pression et résistance gaz |
| OneWire eau | GPIO20 | DS18B20 eau |
| OneWire air | GPIO19 | DS18B20 air |

GPIO19 et GPIO20 sont exclusivement utilisés par OneWire. GPIO3 n'est pas exposé.
Dans l'environnement de production `Waveshare-ESP32-S3`, le TFT local est actif et
réserve GPIO21 (backlight), GPIO45 (CS), GPIO1 (DC), GPIO47 (RST), GPIO2 (MOSI) et
GPIO48 (SCLK). Leurs identifiants de binding génériques sont retirés de `kBindingPorts`
et des listes de configuration lorsque `FLOW_ENABLE_TFT_S3=1`; ils restent disponibles
dans les builds sans TFT.

## Binding ports

Le profil de production avec TFT déclare 52 binding ports: 21 sources analogiques,
15 entrées digitales et 16 sorties digitales. Les builds sans TFT ajoutent les six
GPIO génériques en entrée et en sortie, soit 64 ports. Un binding port décrit une capacité physique; il ne crée un
endpoint runtime que lorsqu'un IO slot lui est affecté.

Chaque entrée de `kBindingPorts` porte un `boardLabel` correspondant au marquage
matériel (`GPIO04`, `GPA0`, `GPB0`, `EXIO1`, etc.). L'API `/api/io/topology`
expose la configuration stable des ports et des slots, tandis que `/api/io/runtime`
expose leurs états et valeurs actualisés. Les deux réponses sont préparées dans des
tampons bornés en PSRAM avant leur transmission HTTP.
Le schéma `3` distingue explicitement l'état `manually_disabled` de l'état `error`.
Lorsqu'un `expanderXX` est désactivé, ses binding ports et tous les slots qui leur sont
liés restent visibles, mais portent l'état `manually_disabled` avec la raison
`expander_disabled`. Aucun accès matériel à cet expander n'est alors effectué.
La topologie expose le marquage dans le champ `board_port`, accompagné du champ `direction` (`input` ou
`output`). La page **Entrées/Sorties** affiche le nom physique et le sens du port
séparément du numéro de canal interne du driver. La colonne `IoId` contient l'identifiant
numérique de l'endpoint lié, ou « Non affecté » lorsque le port est libre. Dans la table
IOSlots, `Driver` et `Canal interne` restent vides lorsque l'endpoint n'a aucun
`binding_port`; lorsqu'il est lié, ces valeurs sont résolues depuis le BindingPort et non
depuis une valeur runtime par défaut. Le sens est représenté par une icône compacte;
le texte « Entrée » ou « Sortie » est disponible au survol et au focus clavier.

### Mesures analogiques

| ID | Constante | Kind/canal | Ressource physique |
|---:|---|---|---|
| 100 | `PortAdsInternal0` | `ADS_INTERNAL_SINGLE`, 0 | ADS1115 `0x48` A0 |
| 101 | `PortAdsInternal1` | `ADS_INTERNAL_SINGLE`, 1 | ADS1115 `0x48` A1 |
| 102 | `PortAdsInternal2` | `ADS_INTERNAL_SINGLE`, 2 | ADS1115 `0x48` A2 |
| 103 | `PortAdsInternal3` | `ADS_INTERNAL_SINGLE`, 3 | ADS1115 `0x48` A3 |
| 110 | `PortAdsExternal0` | `ADS_EXTERNAL_DIFF`, 0 | ADS1115 `0x49` A0-A1 |
| 111 | `PortAdsExternal1` | `ADS_EXTERNAL_DIFF`, 1 | ADS1115 `0x49` A2-A3 |
| 120 | `PortOneWireWater` | `DS18_WATER`, GPIO20 | DS18B20 eau |
| 121 | `PortOneWireAir` | `DS18_AIR`, GPIO19 | DS18B20 air |
| 130 | `PortSht40Temp` | `SHT40`, 0 | Température |
| 131 | `PortSht40Humidity` | `SHT40`, 1 | Humidité |
| 132 | `PortBmp280Temp` | `BMP280`, 0 | Température |
| 133 | `PortBmp280Pressure` | `BMP280`, 1 | Pression |
| 134 | `PortBme688Temp` | `BME680`, 0 | BME688 température |
| 135 | `PortBme688Humidity` | `BME680`, 1 | BME688 humidité |
| 136 | `PortBme688Pressure` | `BME680`, 2 | BME688 pression |
| 137 | `PortBme688Gas` | `BME680`, 3 | BME688 résistance gaz |
| 138 | `PortIna226ShuntMv` | `INA226`, 0 | Tension shunt en mV |
| 139 | `PortIna226BusV` | `INA226`, 1 | Tension bus en V |
| 140 | `PortIna226CurrentMa` | `INA226`, 2 | Courant en mA |
| 141 | `PortIna226PowerMw` | `INA226`, 3 | Puissance en mW |
| 142 | `PortIna226LoadV` | `INA226`, 4 | Tension charge en V |

### Entrées digitales

| ID | Constante | Kind/canal | Affectation par défaut |
|---:|---|---|---|
| 200 | `PortDin0` | GPIO4 optocouplé | `i12`, Water Meter |
| 201 | `PortDin1` | GPIO5 | `i01` |
| 202 | `PortDin2` | GPIO6 | `i02` |
| 203 | `PortDin3` | GPIO7 | `i03` |
| 204 | `PortDin4` | GPIO8 | `i04` |
| 205 | `PortDin5` | GPIO9 | `i05` |
| 206 | `PortDin6` | GPIO10 | `i06` |
| 207 | `PortDin7` | GPIO11 | `i07` |
| 220 | `PortMcpInGpa0` | MCP GPA0 / canal 0 | `i08`, PIR |
| 221 | `PortMcpInGpa1` | MCP GPA1 / canal 1 | Non affecté |
| 222 | `PortMcpInGpa2` | MCP GPA2 / canal 2 | Non affecté |
| 223 | `PortMcpInGpa3` | MCP GPA3 / canal 3 | `i09`, pH Level |
| 224 | `PortMcpInGpa4` | MCP GPA4 / canal 4 | `i10`, Chlorine Level |
| 225 | `PortMcpInGpa5` | MCP GPA5 / canal 5 | `i11`, Pool Level |
| 226 | `PortMcpInGpa6` | MCP GPA6 / canal 6 | Non affecté |
| 240..245 | `PortGpio*Input` | GPIO1, 2, 21, 45, 47, 48 | Builds sans TFT uniquement |

### Sorties digitales

| ID | Constante | Kind/canal | Affectation par défaut |
|---:|---|---|---|
| 300 | `PortExio1` | TCA9554 bit 0 | `d00`, Filtration Pump |
| 301 | `PortExio2` | TCA9554 bit 1 | `d01`, pH Pump |
| 302 | `PortExio3` | TCA9554 bit 2 | `d02`, Chlorine Pump |
| 303 | `PortExio4` | TCA9554 bit 3 | `d03`, Robot |
| 304 | `PortExio5` | TCA9554 bit 4 | `d04`, Remplissage |
| 305 | `PortExio6` | TCA9554 bit 5 | `d05`, Electrolyse |
| 306 | `PortExio7` | TCA9554 bit 6 | `d06`, Lights |
| 307 | `PortExio8` | TCA9554 bit 7 | `d07`, Water Heater |
| 320 | `PortMcpOutGpb0` | MCP GPB0 / canal 8 | `d08` |
| 321 | `PortMcpOutGpb1` | MCP GPB1 / canal 9 | `d09` |
| 322 | `PortMcpOutGpb2` | MCP GPB2 / canal 10 | `d10` |
| 323 | `PortMcpOutGpb3` | MCP GPB3 / canal 11 | `d11` |
| 324 | `PortMcpOutGpb4` | MCP GPB4 / canal 12 | `d12` |
| 325 | `PortMcpOutGpb5` | MCP GPB5 / canal 13 | `d13` |
| 326 | `PortMcpOutGpb6` | MCP GPB6 / canal 14 | `d14` |
| 327 | `PortMcpOutGpb7` | MCP GPB7 / canal 15 | `d15` |
| 340..345 | `PortGpio*Output` | GPIO1, 2, 21, 45, 47, 48 | Builds sans TFT uniquement |

Un GPIO standard ne doit pas être lié simultanément à un slot d'entrée et à un slot de sortie.
Avec le TFT actif, les six GPIO réservés ne doivent être liés à aucun IO slot.

## IO slots

Les IO slots portent les identifiants runtime stables publiés par `IOModule`:

- `a00..a15` pour les mesures analogiques;
- `i00..i12` pour les entrées digitales;
- `d00..d15` pour les sorties digitales.

Leur nom, leur calibration, leur polarité, leur mode et leur `binding_port` sont
configurables et persistés en NVS. Les tableaux suivants décrivent les valeurs par
défaut du profil.

### Entrées analogiques

| IO slot | Nom | Binding port | Source |
|---|---|---:|---|
| `a00` | ORP | 100 | ADS1115 interne A0 |
| `a01` | pH | 101 | ADS1115 interne A1 |
| `a02` | PSI | 102 | ADS1115 interne A2 |
| `a03` | Spare | 103 | ADS1115 interne A3 |
| `a04` | Water Temperature | 120 | OneWire GPIO20 |
| `a05` | Air Temperature | 121 | OneWire GPIO19 |
| `a06` | Current | 140 | INA226 courant |
| `a07` | Voltage | 139 | INA226 tension bus |
| `a08` | a08 | 0 | Non connecté |
| `a09` | a09 | 0 | Non connecté |
| `a10` | a10 | 0 | Non connecté |
| `a11` | a11 | 0 | Non connecté |
| `a12` | a12 | 0 | Non connecté |
| `a13` | a13 | 0 | Non connecté |
| `a14` | a14 | 0 | Non connecté |
| `a15` | a15 | 0 | Non connecté |

Les slots `a08..a15` sont disponibles et configurables, mais restent non connectés
par défaut. Les bindings SHT40, BMP280, BME688, les autres mesures INA226 et
l'ADS1115 externe restent sélectionnables. Aucun de ces slots libres n'est associé
à un rôle métier du domaine Pool.

### Entrées digitales

| IO slot | Nom | Binding port | Mode |
|---|---|---:|---|
| `i00` | GPIO04 | Non connecté | État |
| `i01` | GPIO05 | 201 | État |
| `i02` | GPIO06 | 202 | État |
| `i03` | GPIO07 | 203 | État |
| `i04` | GPIO08 | 204 | État |
| `i05` | GPIO09 | 205 | État |
| `i06` | GPIO10 | 206 | État |
| `i07` | GPIO11 | 207 | État |
| `i08` | PIR | 220 | État, actif haut |
| `i09` | pH Level | 223 | État |
| `i10` | Chlorine Level | 224 | État |
| `i11` | Pool Level | 225 | État |
| `i12` | Water Meter | 200 / GPIO4 optocouplé | Compteur, front montant, debounce 100 ms |

### Sorties digitales

| IO slot | Nom | Binding port |
|---|---|---:|
| `d00` | Filtration Pump | 300 |
| `d01` | pH Pump | 301 |
| `d02` | Chlorine Pump | 302 |
| `d03` | Robot | 303 |
| `d04` | Remplissage | 304 |
| `d05` | Electrolyse | 305 |
| `d06` | Lights | 306 |
| `d07` | Water Heater | 307 |
| `d08` | MCP B0 | 320 |
| `d09` | MCP B1 | 321 |
| `d10` | MCP B2 | 322 |
| `d11` | MCP B3 | 323 |
| `d12` | MCP B4 | 324 |
| `d13` | MCP B5 | 325 |
| `d14` | MCP B6 | 326 |
| `d15` | MCP B7 | 327 |

Toutes les sorties démarrent à OFF et sont actives à l'état haut. `d00` conserve la
politique existante de préservation du latch TCA9554 lors d'un redémarrage chaud.

## Domain slots Pool

Seuls les rôles métier occupent un domain slot. Les DIN génériques `i00..i07`,
les sorties MCP `d08..d15` et les GPIO standards n'en occupent pas.

Un domain slot ne connaît pas directement le matériel. Il fixe le type d'endpoint et
l'IO slot attendu par `PoolLogicModule` ou `PoolDeviceModule`; le binding physique est
résolu ensuite par `IOModule`.

| Domain ID | Constante | Type | IO slot |
|---:|---|---|---|
| 1 | `SensorOrp` | Analogique | `a00` |
| 2 | `SensorPh` | Analogique | `a01` |
| 3 | `SensorPsi` | Analogique | `a02` |
| 4 | `SensorSpareAnalog` | Analogique | `a03` |
| 5 | `SensorWaterTemp` | Analogique | `a04` |
| 6 | `SensorAirTemp` | Analogique | `a05` |
| 7 | `SensorCurrent` | Analogique | `a06` |
| 8 | `SensorVoltage` | Analogique | `a07` |
| 9 | `SensorPir` | Entrée digitale | `i08` |
| 10 | `SensorPhLevel` | Entrée digitale | `i09` |
| 11 | `SensorChlorineLevel` | Entrée digitale | `i10` |
| 12 | `SensorPoolLevel` | Entrée digitale | `i11` |
| 13 | `SensorWaterMeter` | Compteur digital | `i12` |
| 14 | `ActuatorFiltrationPump` | Sortie digitale | `d00` |
| 15 | `ActuatorPhPump` | Sortie digitale | `d01` |
| 16 | `ActuatorChlorinePump` | Sortie digitale | `d02` |
| 17 | `ActuatorRobot` | Sortie digitale | `d03` |
| 18 | `ActuatorFillPump` | Sortie digitale | `d04` |
| 19 | `ActuatorChlorineGenerator` | Sortie digitale | `d05` |
| 20 | `ActuatorWaterHeater` | Sortie digitale | `d07` |
| 22 | `ActuatorLights` | Sortie digitale | `d06` |

Le domaine contient 13 capteurs, 8 presets de pool devices et 21 liaisons
`domain_slot -> io_slot`. `DeviceLights` occupe `pd06` et pilote `d06`; le chauffage
conserve l'identifiant `pd07`.
