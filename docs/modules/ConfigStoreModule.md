# ConfigStoreModule (`moduleId: config`)

## Rôle

Expose l'instance globale de `ConfigStore` dans le `ServiceRegistry` sous `ServiceId::ConfigStore`.

Le contrat exposé couvre les opérations JSON (`applyJson`, exports, inventaire des modules) ainsi que certaines opérations de stockage runtime (`readRuntimeBlob`, `writeRuntimeBlob`, `eraseKey`).

Type: module passif.

## Dépendances

- `loghub`

## Services exposés

- `config` (`ServiceId::ConfigStore`) -> `ConfigStoreService`
  - `applyJson`
  - `toJson`
  - `toJsonModule`
  - `listModules`
  - `erase`
  - `readRuntimeBlob`
  - `writeRuntimeBlob`
  - `eraseKey`

## Services consommés

- `loghub` (log d'initialisation)

## Config / NVS

Ne déclare pas ses propres variables.

Il enregistre l'instance globale `ConfigStore` comme service partagé pour les autres modules.

La lecture offerte par le service reste aujourd'hui orientée export global ou export de module complet. Cette forme est pertinente pour l'inspection, les endpoints de supervision et les snapshots de configuration, sans être une API dédiée à la lecture unitaire fine.

Dans l'état actuel du code, les consommateurs devraient privilégier `ConfigStoreService`. Certains chemins gardent toutefois un accès direct à `ConfigStore` quand ils ont besoin d'un comportement non porté par le service, par exemple `toJsonModule(..., maskSecrets)` avec contrôle explicite du masquage des secrets.

## EventBus / DataStore / MQTT

Directement: aucun.

Indirectement via `ConfigStore`:
- un `applyJson` qui modifie une variable persistante publie `EventId::ConfigChanged`
- utilisé par `MQTTModule` pour `cfg/set`

## Utilisation dans le firmware

Exemple d'application d'un patch:

```cpp
#include "Core/ServiceId.h"

const ConfigStoreService* cfg = services.get<ConfigStoreService>(ServiceId::ConfigStore);
if (cfg) cfg->applyJson(cfg->ctx, "{\"poollogic/modes\":{\"auto_mode\":true}}");
```
