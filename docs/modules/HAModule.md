# HAModule (`moduleId: ha`)

## Rôle

Publication Home Assistant MQTT Discovery:
- registre d'entités statiques (sensor, binary_sensor, switch, number, button)
- publication discovery retainée
- refresh automatique sur changements runtime pertinents
- support du champ discovery `has_entity_name` sur les sensors (piloté par l'entité appelante)

Type: module actif (event-driven par notification task).

## Dépendances

- `eventbus`
- `config`
- `datastore`
- `mqtt`

## Affinité / cadence

- core: 0
- task: `ha`
- loop bloquant sur notification (`ulTaskNotifyTake`)

## Services exposés

- `ha` -> `HAService`
  - `addSensor`, `addBinarySensor`, `addSwitch`, `addNumber`, `addButton`
  - `requestRefresh`

## Services consommés

- `eventbus`
- `datastore`
- `mqtt`

## Capacités statiques

Capacités compile-time du profil Waveshare, résolues depuis
`src/Board/WaveshareBoard.h` via `Limits::Ha::Capacity`:

| Type d'entité | Capacité |
|---|---:|
| sensors | 48 |
| binary sensors | 16 |
| switches | 16 |
| numbers | 30 |
| buttons | 24 |
| selects | 6 |

## Config / NVS

Module config: `ha` (`moduleId = ConfigModuleId::Ha`, branche locale `1`):
- `enabled`
- `vendor`
- `device_id` (Identifiant appareil HA)
- `entity_prefix`
- `disc_prefix`
- `model`

Détail `device_id`:
- persistance NVS: `ha_devid`
- si vide: fallback auto basé MAC (hex)
- alimente `unique_id` des entités Discovery
- alimente aussi le segment `<nodeTopicId>` des topics Discovery (`.../<component>/<nodeTopicId>/<objectId>/config`)
- `dev.ids[0]` dans les payloads Discovery suit prioritairement le `deviceId` MQTT effectif (ex: `mq_tid`), puis retombe sur `device_id` si indisponible

## DataStore

Écritures via `HARuntime.h`:
- `HaPublished`
- `HaVendor`
- `HaDeviceId`

## EventBus

Abonnement:
- `DataChanged`

Réactions:
- sur `WifiReady`/`MqttReady` -> tentative/pending de publication auto-discovery

## MQTT

- producteur MQTT enregistré (`producerId` dédié HA) sur le cœur TX unifié
- jobs HA en mode IDs (`producerId + messageId`), sans publication directe module -> broker
- build topic/payload discovery à la demande via `MqttBuildContext` (buffer central MQTT)
- construit topics discovery:
  - `<discoveryPrefix>/<component>/<nodeTopicId>/<objectId>/config`
- préfixe `object_id` configurable par profil:
  - défaut `fio_*` (via `entity_prefix=fio`)
  - profil `Micronova`: `pio_*`
  - si `entity_prefix` est non vide: `object_id = <prefix>_<entity>`
  - si `entity_prefix` est vide: `object_id = <entity>` (sans préfixe)
- payloads incluent:
  - device metadata
  - `device.name` prioritairement depuis `mqtt.deviceName` (fallback: nom d'origine du profil)
  - `device.identifiers[0] = <vendor>-<mqttDeviceIdEffectif>` (priorité à `mqtt.topicDeviceId` / `mq_tid`; fallback sur `ha/device_id`)
  - `unique_id` construit avec `ha/device_id` (ou fallback MAC hex si vide)
  - le segment `<nodeTopicId>` du topic Discovery est dérivé de `ha/device_id` (ou fallback MAC hex si vide)
  - `availability` basée sur topic `status`
- capteurs diagnostic natifs publiés:
  - `alarms_pack` (`rt/alarms/p`)
  - `uptime` (`rt/system/state`, conversion en minutes depuis `upt_ms`)
  - `heap_free_bytes` (`rt/system/state`, conversion en `ko`)
  - `heap_min_free_bytes` (`rt/system/state`, conversion en `ko`)
  - `heap_fragmentation` (`rt/system/state`, valeur `%`)

## Comportement refresh

- `add*` met à jour la table d'entités et demande refresh
- `requestRefresh` remet `published=false` et notifie la task
- publication effective seulement si MQTT connecté et `mqttReady(DataStore)==true`

## Mode one-shot

Le build peut définir `FLOW_HA_ONESHOT_DISCOVERY=1` pour publier l'auto-discovery une seule fois au démarrage.
Ce mode est utilisé par les profils `FlowIO` et `Micronova`:
- les tables d'entités HA sont allouées dynamiquement au lieu d'être conservées en `.bss`
- le producteur MQTT de configuration HA n'est pas instancié
- après publication retained de toutes les entités discovery, les tables sont libérées et la tâche `ha` appelle `vTaskDelete(nullptr)`
- le service HA reste présent mais refuse les nouveaux enregistrements après teardown, afin d'éviter des pointeurs pendants dans les services/callbacks existants
- pour diagnostiquer la séquence de boot one-shot, le build `FlowIO` peut activer `FLOW_HA_BOOT_TRACE=1` (logs de jalons alloc/enqueue/publish/release)
