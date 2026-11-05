# Services Core et architecture `ServiceId`

Toutes les interfaces de service vivent sous `src/Core/Services/` et sont agrégées par `src/Core/Services/Services.h`.

## Principe général

Le registre de services actuellement utilisé est indexé par `ServiceId`.

Références principales:

- `src/Core/ServiceId.h`
- `src/Core/ServiceRegistry.h`
- `src/Core/ServiceBinding.h`

Un service est généralement composé de:

- un `struct` de pointeurs de fonctions
- un champ `void* ctx`
- ou, pour certains services cœur, un pointeur direct vers l'objet porté

## Consommer un service

Exemple d'accès dans `init()`:

```cpp
#include "Core/ServiceId.h"
#include "Core/Services/Services.h"

void MyModule::init(ConfigStore&, ServiceRegistry& services)
{
    const IOServiceV2* io = services.get<IOServiceV2>(ServiceId::Io);
    const TimeService* time = services.get<TimeService>(ServiceId::Time);
    if (!io || !time) return;
}
```

`services.get<T>(id)` retourne `nullptr` si le service n'est pas enregistré.

## Exposer un service

Le pattern utilisé dans les modules actuels consiste à:

- conserver le service comme membre du module
- binder les méthodes d'instance avec `ServiceBinding::bind`
- enregistrer le service dans `init()`

Exemple:

```cpp
#include "Core/ServiceBinding.h"
#include "Core/ServiceId.h"

class MyModule : public Module {
private:
    bool doThing_(int value);

    MyService svc_{
        ServiceBinding::bind<&MyModule::doThing_>,
        this
    };
};

void MyModule::init(ConfigStore&, ServiceRegistry& services)
{
    (void)services.add(kMyServiceId, &svc_);
}
```

Contraintes de `ServiceBinding`:

- supporte les méthodes `const` et non `const`
- ne supporte pas les retours par référence
- `bind_or` ne s'applique qu'aux retours non `void`

## Comportement du `ServiceRegistry`

Le registre actuel:

- refuse les `ServiceId` invalides
- refuse les pointeurs nuls
- refuse les doublons
- retourne `nullptr` si un service est absent

Le remplacement dynamique d'un service pendant l'exécution n'est pas utilisé dans l'implémentation actuelle.

## Inventaire actuel des `ServiceId`

| `ServiceId` | Nom texte | Contrat |
| --- | --- | --- |
| `LogHub` | `loghub` | `LogHubService` |
| `LogSinks` | `logsinks` | `LogSinkRegistryService` |
| `EventBus` | `eventbus` | `EventBusService` |
| `ConfigStore` | `config` | `ConfigStoreService` |
| `DataStore` | `datastore` | `DataStoreService` |
| `Command` | `cmd` | `CommandService` |
| `Alarm` | `alarms` | `AlarmService` |
| `Hmi` | `hmi` | `HmiService` |
| `Wifi` | `wifi` | `WifiService` |
| `Time` | `time` | `TimeService` |
| `TimeScheduler` | `time.scheduler` | `TimeSchedulerService` |
| `Mqtt` | `mqtt` | `MqttService` |
| `Ha` | `ha` | `HAService` |
| `Io` | `io` | `IOServiceV2` |
| `PoolDevice` | `pooldev` | `PoolDeviceService` |
| `WebInterface` | `webinterface` | `WebInterfaceService` |
| `FirmwareUpdate` | `fwupdate` | `FirmwareUpdateService` |
| `NetworkAccess` | `network_access` | `NetworkAccessService` |
| `FlowCfg` | `flowcfg` | `FlowCfgRemoteService` |
| `I2cBus` | `i2c_bus` | `I2cBusService` |

La présence effective d'un service dépend du profil compilé et des modules enregistrés par ce profil.

`I2cBusService` fait exception à cette dernière règle: il est porté par
`AppContext` et enregistré par le bootstrap avant l'initialisation des modules.
Il expose l'instance unique du bus I2C principal afin que plusieurs modules,
notamment `IOModule` et `HMIModule`, partagent le même contrôleur et la même
synchronisation. À ce stade, `IOModule` reste responsable d'appliquer les
broches configurées lorsqu'un périphérique IO I2C est nécessaire. Si le bus
n'est pas encore initialisé, `HMIModule` peut l'initialiser avec les broches et
la fréquence déclarées par la carte avant d'accéder à son panneau LED dédié.

## Contrats principaux

### `LogHubService`

- enqueue non bloquant d'un `LogEntry`
- enregistrement des tags de modules de log
- filtrage par niveau
- lecture des statistiques du hub

### `LogSinkRegistryService`

- enregistrement des sinks
- itération sur les sinks déclarés

### `CommandService`

- enregistrement de handlers
- exécution d'une commande via `execute`

### `ConfigStoreService`

- import JSON
- export global
- export par module
- liste des modules
- effacement complet
- lecture de petits blobs runtime
- écriture de petits blobs runtime
- suppression ciblée d'une clé persistante

`ConfigStoreService` n'est donc pas limité au couple apply/export JSON: il sert aussi de façade pour certaines opérations de stockage runtime et d'effacement clé par clé.

La granularité de lecture exposée par le service est aujourd'hui l'export global ou l'export d'un module complet via `toJsonModule()`. Cette granularité est adaptée à l'inspection, aux endpoints de supervision/debug et aux snapshots de configuration; elle n'est pas présentée comme une API de lecture fine par variable.

Quand un module consomme le store, le service reste l'interface à privilégier. Dans l'état actuel du code, certains chemins gardent néanmoins aussi un accès direct à `ConfigStore` lorsque le contrat du service n'expose pas toute la nuance nécessaire, par exemple l'export `toJsonModule(..., maskSecrets)` avec contrôle explicite du masquage des secrets.

### `DataStoreService`

- expose directement `DataStore* store`

### `EventBusService`

- expose directement `EventBus* bus`

### `AlarmService`

- enregistrement d'alarmes
- acquittement
- lecture d'état
- génération de snapshots

### `HmiService`

- demande de refresh
- ouverture de vues de configuration
- génération d'un snapshot menu

### `WifiService`

- état du lien
- IP courante
- demande de reconnexion
- demande de scan

### `TimeService`

- état de synchronisation NTP
- lecture de l'epoch
- formatage de la date locale
- réglage depuis une RTC externe
- indication que l'heure courante provient d'une RTC externe

### `TimeSchedulerService`

- lecture et écriture des slots de planification
- effacement
- lecture des slots actifs

### `MqttService`

- enregistrement de producteurs
- enqueue des jobs MQTT
- formatage de topics
- état de la connexion MQTT

### `HAService`

- déclaration d'entités discovery
- demande de refresh discovery

### `IOServiceV2`

- inventaire des endpoints
- lecture des métadonnées
- lecture des valeurs
- écriture des sorties digitales
- lecture du dernier cycle IO

### `PoolDeviceService`

- inventaire des slots
- lecture de l'état réel
- écriture de l'état désiré
- remise à niveau d'une cuve

### `NetworkAccessService`

- état de joignabilité du web local
- mode réseau
- IP active
- notification de changement Wi-Fi

### `WebInterfaceService`

- pause et reprise de l'interface web
- lecture de l'état

### `FirmwareUpdateService`

- démarrage d'une mise à jour
- lecture de l'état JSON
- lecture de la configuration JSON
- mise à jour de la configuration source

### `FlowCfgRemoteService`

- readiness du lien vers `FlowIO`
- navigation de configuration distante
- lecture des snapshots runtime
- lecture Runtime UI binaire
- application de patch JSON distant
