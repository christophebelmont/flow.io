# IOModule (`moduleId: io`)

## Rôle

`IOModule` est la couche d'entrées/sorties du firmware principal Waveshare.

Le module regroupe actuellement:

- l'inventaire des endpoints IO exposés aux autres modules
- les drivers GPIO, ADS1115, DS18B20, TCA9554, MCP23017 et PCF8574
- le scheduler d'acquisition
- la persistance et l'application de la configuration IO
- les snapshots runtime pour MQTT
- les valeurs Runtime UI liées aux mesures principales
- la synchronisation Home Assistant des capteurs et sorties déclarés par le profil

Le service public exposé au reste du firmware est `IOServiceV2`.

La cartographie complète du profil Waveshare est documentée dans
[`docs/core/waveshare-io-map.md`](../core/waveshare-io-map.md), avec les tables
`binding_port`, `io_slot` et `domain_slot`.

Type: module actif.

## Dépendances

En build Waveshare, les dépendances déclarées sont:

- `loghub`
- `datastore`
- `mqtt`

Le profil Waveshare lui associe en plus:

- les bus 1-Wire `oneWireWater` et `oneWireAir`
- la table de ports physiques définie dans `src/Profiles/Waveshare/WaveshareIoLayout.h`
- les définitions d'endpoints construites dans `src/Profiles/Waveshare/WaveshareIoAssembly.cpp`

## Affinité et cadence

- core: `1`
- task: `io`
- stack: `2560`
- boucle: `10 ms`

Jobs planifiés en interne:

- `ads_fast`
- `ds_slow`
- `din_poll`

## Services exposés

- `io` -> `IOServiceV2`

Fonctions principales de `IOServiceV2`:

- inventaire des endpoints: `count`, `idAt`, `meta`
- lecture générique: `readValue`
- lecture digitale: `readDigital`
- écriture digitale: `writeDigital`
- lecture analogique: `readAnalog`
- suivi du cycle IO: `tick`, `lastCycle`

## Capacités statiques

Capacités compile-time actuelles dans `src/Modules/IOModule/IOModule.h`:

| Capacité | Valeur |
|---|---:|
| entrées analogiques | 16 |
| entrées digitales | 13 |
| sorties digitales | 16 |
| slots de configuration correspondants | 16 / 13 / 16 |

Plages d'`IoId` utilisées:

- sorties digitales: `IO_ID_DO_BASE .. IO_ID_DO_BASE + 15`
- entrées digitales: `IO_ID_DI_BASE .. IO_ID_DI_BASE + 12`
- entrées analogiques: `IO_ID_AI_BASE .. IO_ID_AI_BASE + 15`

## Backends physiques pris en charge

Backends actuellement gérés par le module:

- GPIO direct
- sorties via `PCF8574`
- entrées analogiques `ADS1115`
- sondes `DS18B20`
- capteurs `SHT40`, `BMP280`, `BME680`
- mesures électriques `INA226`
- compteur d'impulsions sur GPIO avec debounce

Les `IoBackend` visibles dans le service sont:

- `IO_BACKEND_GPIO`
- `IO_BACKEND_PCF8574`
- `IO_BACKEND_ADS1115_INT`
- `IO_BACKEND_ADS1115_EXT_DIFF`
- `IO_BACKEND_DS18B20`
- `IO_BACKEND_SHT40`
- `IO_BACKEND_BMP280`
- `IO_BACKEND_BME680`
- `IO_BACKEND_INA226`

## Modèle de binding actuel

Le module ne déduit pas seul le câblage métier. Le binding est fourni par le profil Waveshare.

### Catalogue des ports physiques

`src/Profiles/Waveshare/WaveshareIoLayout.h` déclare:

- les `PhysicalPortId`
- la table `kBindingPorts`
- les bindings par défaut des rôles analogiques
- les bindings par défaut des entrées digitales
- les bindings par défaut des sorties digitales

Ports déclarés actuellement:

- ADS interne: `PortAdsInternal0..3`
- ADS externe différentiel: `PortAdsExternal0..1`
- DS18B20: `PortOneWireWater`, `PortOneWireAir`
- SHT40: `PortSht40Temp`, `PortSht40Humidity`
- BMP280: `PortBmp280Temp`, `PortBmp280Pressure`
- BME688 via driver BME680: `PortBme688Temp`, `PortBme688Humidity`, `PortBme688Pressure`, `PortBme688Gas`
- INA226: `PortIna226ShuntMv`, `PortIna226BusV`, `PortIna226CurrentMa`, `PortIna226PowerMw`, `PortIna226LoadV`
- entrées digitales intégrées: `PortDin0..7`
- entrées d'extension: `PortMcpInGpa0..6`
- sorties relais TCA9554: `PortExio1..8`
- sorties d'extension: `PortMcpOutGpb0..7`
- GPIO génériques déclarés pour les builds sans TFT

### Instanciation des endpoints

`src/Profiles/Waveshare/WaveshareIoAssembly.cpp` lit le domaine actif et appelle:

- `defineAnalogInput`
- `defineDigitalInput`
- `defineDigitalOutput`

Le profil Waveshare instancie aujourd'hui:

- 16 entrées analogiques (`a00..a15`)
- 13 entrées digitales (`i00..i12`)
- 16 sorties digitales (`d00..d15`)

## Affectation actuelle des rôles Waveshare

### Entrées analogiques

| Rôle par défaut | Port physique par défaut |
|---|---|
| `SensorOrp` | `PortAdsInternal0` |
| `SensorPh` | `PortAdsInternal1` |
| `SensorPsi` | `PortAdsInternal2` |
| `SensorSpareAnalog` | `PortAdsInternal3` |
| `SensorWaterTemp` | `PortOneWireWater` |
| `SensorAirTemp` | `PortOneWireAir` |
| `SensorCurrent` | `PortIna226CurrentMa` |
| `SensorVoltage` | `PortIna226BusV` |

### Entrées digitales

| Rôle par défaut | Port physique par défaut | Mode |
|---|---|---|
| `SensorPir` | `PortMcpInGpa0` | état |
| `SensorPhLevel` | `PortMcpInGpa3` | état |
| `SensorChlorineLevel` | `PortMcpInGpa4` | état |
| `SensorPoolLevel` | `PortMcpInGpa5` | état |
| `SensorWaterMeter` | `PortDin0` (optocoupleur GPIO4) | compteur, front montant, debounce `100000 us` |

### Sorties digitales

| Rôle par défaut | Port physique par défaut |
|---|---|
| `ActuatorFiltrationPump` | `PortExio1` |
| `ActuatorPhPump` | `PortExio2` |
| `ActuatorChlorinePump` | `PortExio3` |
| `ActuatorRobot` | `PortExio4` |
| `ActuatorFillPump` | `PortExio5` |
| `ActuatorChlorineGenerator` | `PortExio6` |
| éclairage `d06` (`Lights`) | `PortExio7` |
| `ActuatorWaterHeater` | `PortExio8` |

## Configuration et NVS

Structures de configuration utilisées:

- `IOModuleConfig`
- `IOAnalogSlotConfig`
- `IODigitalInputSlotConfig`
- `IODigitalOutputSlotConfig`

Branches de configuration exposées par le module:

- `Io`
- `IoDebug`
- `IoInputA0 .. IoInputA5`
- `IoOutputD0 .. D7`
- `IoInputD0 .. D3`

Paramètres principaux:

- activation module: `enabled`
- initialisation du bus I2C principal partagé: `i2c_sda`, `i2c_scl`
- polling: `ads_poll_ms`, `ds_poll_ms`, `digital_poll_ms`
- ADS: adresses, gain, rate
- expanders PCF8574/TCA9554/MCP23017: activation, adresse et masque par instance `expanderXX`
- inversion électrique éventuelle des sorties d'expander: propriété fixe du profil matériel, non configurable
- traces: `trace_enabled`, `trace_period_ms`
- calibration et précision des entrées analogiques
- binding, `activeHigh`, `momentary`, `pulseMs` des sorties digitales
- binding, `pullMode`, `edgeMode`, debounce des entrées digitales

### Sémantique `activeHigh` / `edgeMode` pour les compteurs GPIO

Pour les entrées digitales en mode compteur, le driver ne raisonne pas directement en "front physique brut sur le pin".
Il convertit d'abord le niveau lu en un état logique `logicalOn`:

- `activeHigh=true`:
  `HIGH` = actif, `LOW` = inactif
- `activeHigh=false`:
  `LOW` = actif, `HIGH` = inactif

Le champ `edgeMode` est ensuite appliqué sur cette transition logique:

- `rising`: passage logique `inactif -> actif`
- `falling`: passage logique `actif -> inactif`
- `both`: les deux transitions logiques

Conséquence importante:
sur une entrée active bas (`activeHigh=false`), le front logique montant correspond physiquement à un front descendant sur le pin, et le front logique descendant correspond physiquement à un front montant sur le pin.

Tableau complet du comportement actuel du driver compteur GPIO:

| `activeHigh` | `edgeMode` | Interruption physique armée sur le pin | Transition logique effectivement comptée | Interprétation pratique |
|---|---|---|---|---|
| `true` | `falling` | front descendant | `actif -> inactif` | fin d'une impulsion active haut |
| `true` | `rising` | front montant | `inactif -> actif` | début d'une impulsion active haut |
| `true` | `both` | les deux fronts | les deux transitions | compte montée + descente |
| `false` | `falling` | front montant | `actif -> inactif` | fin d'une impulsion active bas |
| `false` | `rising` | front descendant | `inactif -> actif` | début d'une impulsion active bas |
| `false` | `both` | les deux fronts | les deux transitions | compte descente + remontée |

Exemples pratiques:

- capteur avec pull-up externe et impulsion active bas:
  `activeHigh=false`, `edgeMode=rising` comptera le début de l'impulsion, donc le front physique descendant
- capteur avec pull-up externe et impulsion active bas:
  `activeHigh=false`, `edgeMode=falling` comptera la fin de l'impulsion, donc le front physique montant
- capteur avec impulsion active haut:
  `activeHigh=true`, `edgeMode=rising` comptera le front physique montant

Cette convention est cohérente avec une interprétation "métier" de `edgeMode` dans le domaine logique du signal.
Elle peut toutefois surprendre si l'on s'attend à ce que `rising` et `falling` désignent toujours les fronts physiques bruts du GPIO.

### Persistance NVS du cumul compteur

Pour les entrées digitales en mode compteur, le cumul long terme `counter_total` est persisté en NVS selon deux conditions complémentaires:

- immédiatement dès qu'au moins `32` nouvelles impulsions ont été accumulées depuis la dernière écriture réussie
- au plus tard toutes les `3 minutes` si la valeur a changé depuis la dernière écriture réussie, même si le seuil des `32` impulsions n'a pas été atteint

Cette politique limite l'usure NVS tout en réduisant la perte potentielle de cumul après redémarrage sur des compteurs à faible fréquence.

## DataStore

Le module publie ses valeurs runtime via `Modules/IOModule/IORuntime.h`.

Écritures utilisées:

- `setIoEndpointFloat(...)`
- `setIoEndpointBool(...)`
- `setIoEndpointInt(...)`

Clés utilisées:

- base `DataKeys::IoBase`
- un index runtime par endpoint exposé

## MQTT runtime

`IOModule` implémente `IRuntimeSnapshotProvider`.

Routes publiées actuellement:

- `rt/io/input/aN`
- `rt/io/input/iN`
- `rt/io/output/dN`

Le module peut aussi construire des snapshots de groupe:

- entrées
- sorties

## Runtime UI

Le module expose actuellement des valeurs Runtime UI pour:

- température eau
- température air
- pH
- ORP
- compteur eau

Les identifiants sont déclarés dans `IOModule::RuntimeUiValueId`.

## Home Assistant

Le module IO n'enregistre pas seul toutes les entités Home Assistant. Dans le profil Waveshare, `src/Profiles/Waveshare/WaveshareIoAssembly.cpp` synchronise:

- les capteurs analogiques déclarés
- les binary sensors ou sensors des entrées digitales
- les switches liés aux sorties digitales présentes

Cette synchronisation repose sur:

- `HAModule`
- la table `PoolBinding`
- les suffixes runtime `rt/io/...`

## Particularités de fonctionnement

- les sorties peuvent être impulsionnelles (`momentary`)
- les PCF8574 déclarés dans `expanderXX` sont réservés aux endpoints IO génériques
- les compteurs digitaux peuvent être persistés en NVS
- le module maintient `IoCycleInfo` pour exposer la liste des `IoId` modifiés sur le dernier cycle
- les labels exposés par `endpointLabel()` viennent des définitions construites par le profil
