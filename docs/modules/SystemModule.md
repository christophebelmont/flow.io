# SystemModule (`moduleId: system`)

## Rôle

Expose les commandes système transverses.

Type: module passif.

## Dépendances

- `loghub`
- `cmd`
- `config`
- `eventbus`

## Services consommés

- `cmd` pour enregistrer les handlers
- `config` pour exposer la configuration système
- `eventbus` pour réagir aux changements de configuration
- `loghub` pour logging

## Services exposés

Aucun.

## Config / NVS

Module config: `system` (`moduleId = ConfigModuleId::System`, branche locale `1`)
- `lang` (`sys_lang`)
- `devicename` (`sys_dname`), défaut `flowio`, utilisé par l'interface web et les annonces DHCP/mDNS ; en DHCP, la valeur par défaut est annoncée sous la forme unique `FlowIO-XXXXXX`

## Commandes

- `system.ping`
  - réponse: `{"ok":true,"pong":true}`

- `system.reboot`
  - réponse ACK puis `esp_restart()`

- `system.factory_reset`
  - actuellement: réponse ACK puis reboot
  - la purge NVS est notée dans le code comme point d'évolution

## EventBus / DataStore / MQTT

- abonnement `ConfigChanged` pour normaliser la langue à chaud
