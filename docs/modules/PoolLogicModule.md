# PoolLogicModule (`moduleId: poollogic`)

## Rôle

`PoolLogicModule` est l'orchestrateur métier principal de la piscine.  
Il:
- calcule et maintient la fenêtre quotidienne de filtration à partir de la température d'eau
- pilote les équipements via `PoolDeviceService` (filtration, pompe pH, pompe ORP/chlore liquide, robot, électrolyse, remplissage)
- applique les règles automatiques (modes, seuils, délais, sécurités)
- exécute la régulation pH/ORP en **PID temporel** (duty-cycle dans une fenêtre fixe)
- pilote un protocole de **chauffage assisté** (`heat_assist`) quand la température d'eau dépend de la filtration
- expose des snapshots runtime MQTT (`rt/poollogic/ph`, `rt/poollogic/orp`, `rt/poollogic/heat_assist`)
- enregistre des commandes métier et des entités Home Assistant
- surveille la pression via `AlarmService`

Type: module actif.

## Dépendances

- `loghub`
- `eventbus`
- `time` (`TimeSchedulerService`)
- `io` (`IOServiceV2`)
- `pooldev` (`PoolDeviceService`)
- `ha` (`HAService`)
- `cmd` (`CommandService`)
- `alarms` (`AlarmService`)

## Affinité / cadence

- core: `1`
- task: `poollogic`
- loop: `200 ms` (`vTaskDelay(pdMS_TO_TICKS(200))`)

## Interfaces exposées

Services Core exposés:
- aucun service public direct

Interfaces runtime exposées:
- `IRuntimeSnapshotProvider`
  - `rt/poollogic/ph`
  - `rt/poollogic/orp`
  - `rt/poollogic/heat_assist`

Ces snapshots sont routés vers MQTT via `MQTTModule::RuntimeProducer` (providers enregistrés dans le bootstrap de profil `FlowIO`), pas publiés directement par `PoolLogicModule`.

## Guide utilisateur: protocole chauffage assisté (`heat_assist`)

### Pourquoi ce protocole existe

Sur certaines installations, la température d'eau n'est fiable que si la pompe de filtration a tourné un peu.  
Le protocole `heat_assist` résout ce point: il fait d'abord un cycle court de filtration pour mesurer, puis décide si le chauffage doit réellement démarrer.

### Conditions d'activation

- `auto_mode` activé
- `heater_auto_mode` activé
- pas d'alarme pression PSI bloquante

Si une de ces conditions n'est pas remplie, `heat_assist` reste inactif.

### Fonctionnement pas à pas

1. Si la pompe est déjà en marche, le chauffage suit une hystérésis classique autour de la consigne.
2. Si la pompe est arrêtée en mode auto, le système lance une **filtration de sondage de 5 minutes**.
3. Cette phase de sondage est répétée toutes les **30 minutes** en régime normal, puis toutes les **20 minutes** après un cycle de chauffe terminé.
4. À la fin des 5 minutes, la température est lue.
5. Si l'eau est sous le seuil bas d'hystérésis, la pompe reste ON et le chauffage passe ON jusqu'au seuil haut d'hystérésis.
6. Quand le seuil haut est atteint, chauffage OFF, pompe OFF, puis reprise du cycle de sondage en cadence 5 minutes / 20 minutes.

### Exemples concrets

1. Eau froide le matin (pompe arrêtée): 08:00 sondage 5 min, 08:05 eau sous seuil bas donc chauffage ON + pompe ON, puis 10:10 seuil haut atteint donc chauffage OFF + pompe OFF. Ensuite le module passe en sondage périodique 5 min / 20 min.

2. Eau proche de la consigne (pompe arrêtée): sondage 5 min, mesure au-dessus du seuil bas, pas de chauffe, retour en attente jusqu'au prochain sondage.

3. Passage en manuel: si `auto_mode` est désactivé, `heat_assist` n'orchestre plus la chauffe automatique et la raison affichée passe à `MANUAL_MODE`.

### Lecture des motifs (`reason`) pour comprendre le comportement

- `DISABLED`: mode auto chauffage désactivé.
- `MANUAL_MODE`: mode auto global désactivé.
- `PSI_BLOCKED`: chauffage bloqué par la sécurité pression.
- `SETPOINT_INVALID`: consigne chauffage invalide.
- `TEMP_UNAVAILABLE`: température indisponible au moment de la décision.
- `PROBE_WAIT_30M`: attente avant le prochain sondage (cadence normale).
- `PROBE_WAIT_20M`: attente avant le prochain sondage (après cycle de chauffe).
- `PROBE_RUNNING`: cycle de sondage 5 min en cours.
- `HEATING`: chauffe active (pompe + chauffage).
- `IDLE_PUMP_ON`: pompe en marche mais pas de demande de chauffe.
- `SETPOINT_REACHED`: seuil haut atteint, arrêt chauffe/pompe effectué.

## Config / NVS

Module config: `poollogic`
Identité config: `moduleId = ConfigModuleId::PoolLogic`
Branches locales utilisées:
- `1`: `mode`
- `2`: `filtration`
- `3`: `sensors`
- `4`: `pid`
- `5`: `delay`
- `6`: `device`

Persistance: `ConfigStore` + `NvsKeys::PoolLogic::*`

### Paramètres modes et stratégie

- `enabled`
- `auto_mode`
- `winter_mode`
- `ph_auto_mode`
- `orp_auto_mode`
- `electrolys_mode`
- `electro_run_md`

### Fenêtre de filtration (calcul quotidien)

- `wat_temp_lo_th`
- `wat_temp_setpt`
- `filtr_start_min`
- `filtr_stop_max`
- `filtr_start_clc` (calculé)
- `filtr_stop_clc` (calculé)

### Hivernage actif

Activer `poollogic/mode/auto_mode` et `poollogic/mode/winter_mode`.
En hiver, les cycles périodiques remplacent la fenêtre quotidienne calculée :

- `poollogic/filtration/winter_duration_min` : durée de marche, **15 minutes** par défaut ; minimum effectif 1 minute, plafonnée à la période.
- `poollogic/filtration/winter_interval_h` : intervalle **entre deux débuts de cycle**, **1 heure** par défaut, borné de 1 à 24 heures.

Les réglages sont persistants et disponibles dans la configuration filtration.
Avec les valeurs par défaut : 15 minutes ON, puis 45 minutes OFF.
Le premier cycle commence immédiatement à l'activation en automatique ou après redémarrage.
L'intervalle utilise une horloge monotone et fonctionne sans synchronisation NTP.
Changer l'intervalle redémarre le cycle ; changer la durée s'applique au cycle en cours.
Le mode manuel conserve le contrôle manuel de la pompe.

Le hors gel reste prioritaire : sous `winter_start_t` (comparaison stricte, défaut −2 °C),
la pompe démarre même entre deux cycles ; si elle tourne, elle reste active tant que
la température d'air est inférieure ou égale à `freeze_hold_t` (défaut 2 °C).
La fin des 15 minutes ou d'une chauffe ne peut pas interrompre cette demande.
Sans mesure d'air valide, les cycles fonctionnent mais le hors gel ne peut pas être évalué.
La sécurité pression reste prioritaire et peut arrêter la pompe.

### Bindings capteurs IO

- `ph_io_id`
- `orp_io_id`
- `psi_io_id`
- `wat_temp_io_id`
- `air_temp_io_id`
- `pool_lvl_io_id`
- `ph_lvl_io_id`
- `chl_lvl_io_id`

### Seuils métier

- `psi_low_th`
- `psi_high_th`
- `winter_start_t`
- `freeze_hold_t`
- `secure_elec_t`
- `ph_setpoint`
- `orp_setpoint`

### PID pH / ORP (temporel)

- `ph_kp`, `ph_ki`, `ph_kd`
- `orp_kp`, `orp_ki`, `orp_kd`
- `ph_window_ms`
- `orp_window_ms`
- `pid_min_on_ms`
- `pid_sample_ms`

### Délais / temporisations

- `psi_start_dly_s`
- `delay_pids_min`
- `dly_electro_min`
- `robot_delay_min`
- `robot_dur_min`
- `fill_min_on_s`

### Slots équipements (PoolDevice)

- `filtration_slot`
- `swg_slot`
- `robot_slot`
- `filling_slot`

## Commandes

Enregistrées via `CommandService`:

Transport MQTT (`<base>/<device>/cmd`) :
- payload attendu: `{"cmd":"<nom_commande>","args":{...}}`

Commandes modes:
- `poollogic.auto_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `auto_mode`
- `poollogic.auto_mode.toggle`
  - inverse `auto_mode`
- `poollogic.ph_auto_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `ph_auto_mode`
- `poollogic.ph_auto_mode.toggle`
  - inverse `ph_auto_mode`
- `poollogic.orp_auto_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `orp_auto_mode`
- `poollogic.orp_auto_mode.toggle`
  - inverse `orp_auto_mode`
- `poollogic.winter_mode.set`
  - args: `{"value":true|false}`
  - persiste et applique `winter_mode`
- `poollogic.winter_mode.toggle`
  - inverse `winter_mode`

Commandes actionneurs:
- `poollogic.filtration.write`
  - args: `{"value":true|false}`
  - force `auto_mode=false`
  - écrit l'état désiré du slot filtration via `pooldev.writeDesired`
- `poollogic.filtration.toggle`
  - inverse l'état réel filtration
  - force `auto_mode=false`
- `poollogic.ph_pump.write`
  - args: `{"value":true|false}`
  - écrit la consigne de pompe pH
  - si `value=true`, force `ph_auto_mode=false` (aligné avec `pooldevice.write`)
- `poollogic.ph_pump.toggle`
  - inverse l'état réel pompe pH
  - si passage ON, force `ph_auto_mode=false`
- `poollogic.orp_pump.write`
  - args: `{"value":true|false}`
  - écrit la consigne de pompe ORP/chlore liquide
  - si `value=true`, force `orp_auto_mode=false` (aligné avec `pooldevice.write`)
- `poollogic.orp_pump.toggle`
  - inverse l'état réel pompe ORP/chlore liquide
  - si passage ON, force `orp_auto_mode=false`
- `poollogic.light.write` / `poollogic.lights.write`
  - args: `{"value":true|false}`
  - écrit la consigne éclairage
- `poollogic.light.toggle` / `poollogic.lights.toggle`
  - inverse l'état réel éclairage
- `poollogic.robot.write`
  - args: `{"value":true|false}`
  - écrit la consigne robot
- `poollogic.robot.toggle`
  - inverse l'état réel robot
- `poollogic.heater.write`
  - args: `{"value":true|false}`
  - écrit la consigne chauffage
- `poollogic.heater.toggle`
  - inverse l'état réel chauffage
- `poollogic.chlorine_generator.write` / `poollogic.swg.write`
  - args: `{"value":true|false}`
  - écrit la consigne électrolyseur (SWG)
- `poollogic.chlorine_generator.toggle` / `poollogic.swg.toggle`
  - inverse l'état réel électrolyseur (SWG)

Commande utilitaire:
- `poollogic.filtration.recalc`
  - met en file une recomputation de la fenêtre
  - traitement asynchrone dans la loop

Les réponses d'erreur suivent `ErrorCode` (`MissingArgs`, `MissingValue`, `NotReady`, `Disabled`, `InterlockBlocked`, etc.).

## EventBus et Scheduler

### Abonnements EventBus

- `EventId::SchedulerEventTriggered`
  - `POOLLOGIC_EVENT_DAILY_RECALC` + `Trigger` -> queue recalc de filtration
  - `TIME_EVENT_SYS_DAY_START` + `Trigger` -> reset quotidien interne (`cleaningDone_ = false`)
  - `POOLLOGIC_EVENT_FILTRATION_WINDOW` + `Start/Stop` -> mise à jour `filtrationWindowActive_`

### Slots scheduler gérés

- slot `3` (`POOLLOGIC_EVENT_DAILY_RECALC`)
  - rappel quotidien à 15:00
- slot `4` (`POOLLOGIC_EVENT_FILTRATION_WINDOW`)
  - fenêtre start/stop calculée dynamiquement
  - `replayStartOnBoot=true` pour reconstruire l'état après reboot

## Algorithme de contrôle

### Entrées runtime

- capteurs analogiques: pH, ORP, PSI, température eau, température air
- capteur digital: niveau bassin
- états actionneurs: lecture `pooldev.readActualOn(...)`
- états alarmes PSI: via `alarmSvc->isActive(...)`

Convention logique des capteurs digitaux de niveau (règle harmonisée):
- `true` (`ON`) = problème détecté (niveau bas / défaut)
- `false` (`OFF`) = état normal (pas de problème)
- cette convention est appliquée à `pool_lvl_io_id`, `ph_lvl_io_id`, `chl_lvl_io_id`

### Priorité des règles (filtration)

Ordre de décision appliqué à chaque cycle (`200 ms`):
1. état réel et capteurs relus (`syncDeviceState_`, IO analog/digital)
2. statut sécurité PSI recalculé (`psiError_`)
3. si `psiError_==true` -> **filtration forcée OFF**, même en manuel
4. sinon:
   - mode manuel (`auto_mode=false`): consigne manuelle conservée
   - mode auto: décision fenêtre scheduler / hiver / freeze-hold
5. actionnement via `PoolDeviceService::writeDesired` (avec interlocks `PoolDeviceModule`)

### Pilotage filtration / robot / SWG / remplissage

Logique principale:
- filtration:
  - sécurité PSI prioritaire: coupe sur erreur PSI (auto **et** manuel)
  - en auto: suit fenêtre scheduler, mode hiver et freeze-hold
  - en manuel (`auto_mode=false`): `PoolLogic` n'impose pas de demande auto hors sécurité PSI
- robot:
  - démarre après `robot_delay_min` de filtration
  - s'arrête après `robot_dur_min`
  - un cycle/jour (`cleaningDone_`)
- électrolyse:
  - nécessite filtration active, température mini (`secure_elec_t`) et délai (`dly_electro_min`)
  - en mode `electro_run_md`, asservissement ORP avec hystérésis implicite (`<= 100%` pour maintenir, `<= 90%` pour démarrer)
- remplissage:
  - démarre si `Pool Level` est actif (`pool_lvl_io_id == true`)
  - respecte un minimum de marche `fill_min_on_s`

### Alarmes pression PSI

- `AlarmId::PoolPsiLow`
  - latched
  - délai ON `2000 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition active seulement si filtration ON et `runSec > psi_start_dly_s`
- `AlarmId::PoolPsiHigh`
  - latched
  - sévérité critique
  - délai ON `0 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition active seulement si filtration ON

### Alarmes niveau cuves dosage

- `AlarmId::PoolWaterLevelLow`
  - non-latched (auto-clear quand entrée inactive)
  - délai ON `500 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition: entrée digitale `pool_lvl_io_id == true`
- `AlarmId::PoolPhTankLow`
  - non-latched (auto-clear quand entrée inactive)
  - délai ON `500 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition: entrée digitale `ph_lvl_io_id == true`
- `AlarmId::PoolChlorineTankLow`
  - non-latched (auto-clear quand entrée inactive)
  - délai ON `500 ms`, OFF `1000 ms`, répétition `60000 ms`
  - condition: entrée digitale `chl_lvl_io_id == true`

### Réarmement PSI

- source de vérité en nominal: `alarmSvc->isActive(PoolPsiLow|PoolPsiHigh)`
- tant qu'une alarme PSI latched reste `active`, `psiError_` reste vrai et la filtration est bloquée
- si la condition est redevenue fausse, un `reset` manuel est alors autorisé pour clear l'alarme
- si la filtration est redémarrée alors que la pression reste anormale:
  - `psi_high` peut reraiser immédiatement
  - `psi_low` reraisera après `psi_start_dly_s` (délai de démarrage)

### Mode dégradé sans `AlarmService`

Si le service alarmes est indisponible, `PoolLogic` applique un latch local PSI minimal:
- détection locale `psi < low` (après délai de démarrage) ou `psi > high`
- `psiError_` passe à vrai
- pas de clear automatique local (mode dégradé conservatif)

## Régulation PID temporelle (pH / ORP)

La régulation est implémentée dans `PoolLogicModule` avec deux états internes (`TemporalPidState`), un pour pH, un pour ORP.

### Activation

Le mode PID est autorisé seulement si:
- filtration en marche
- pas de mode hiver
- délai de stabilisation atteint (`delay_pids_min`)
- mode auto de la boucle activé (`ph_auto_mode` / `orp_auto_mode`)

Le calcul et la commande sont ensuite conditionnés par:
- capteur disponible (`ph_io_id` / `orp_io_id`)
- pas de défaut PSI latched (`psiError_==false`)
- pas de niveau bas cuve actif:
  - pH: `phTankLowError_==false`
  - ORP/chlore: `chlorineTankLowError_==false`
- pour ORP péristaltique: `electrolys_mode == false` (en mode électrolyse, la pompe ORP liquide est inhibée)

### Convention d'erreur

- pH: `error = ph_input - ph_setpoint`
  - injection acide seulement si `error > 0`
- ORP: `error = orp_setpoint - orp_input`
  - injection chlore liquide seulement si `error > 0`

### Calcul périodique

À chaque `pid_sample_ms` (par défaut `30000 ms`):
- intégrale:
  - accumulée si `Ki != 0`
  - remise à zéro si `Ki == 0` ou `error <= 0`
- dérivée:
  - `dE/dt` entre l'échantillon précédent et courant
- sortie:
  - `u = Kp*e + Ki*I + Kd*D`
  - clampée sur `[0, window_ms]`
  - filtrée par seuil minimal: si `u < pid_min_on_ms`, alors `0`

### Fenêtre temporelle (PWM temporel)

Chaque boucle a une fenêtre cyclique fixe (`ph_window_ms` / `orp_window_ms`):
- la fenêtre est avancée par pas de `window_ms`
- `output_on_ms` représente la durée ON au début de la fenêtre
- demande pompe:
  - ON si `elapsed_in_window < output_on_ms`
  - OFF sinon

### Reset état PID

Si les conditions d'autorisation ne sont plus remplies:
- sortie forcée à `0`
- demande OFF
- reset de l'état interne (fenêtre, intégrale, erreur, échantillons)

### Actionnement

Le module n'écrit pas directement les GPIO.  
Il passe par `PoolDeviceService::writeDesired` sur:
- slot pH (`POOL_IO_SLOT_PH_PUMP`)
- slot ORP/chlore liquide (`POOL_IO_SLOT_CHLORINE_PUMP`)

Les interlocks `PoolDeviceModule` restent appliqués.

Codes de blocage renvoyés par `PoolDevice` (`blockReason`):
- `0` = `none`
- `1` = `disabled`
- `2` = `interlock`
- `3` = `io_error`
- `4` = `max_uptime` (limite journalière `max_uptime_day_s` atteinte)

## Runtime MQTT (`rt/poollogic/*`)

Snapshots publiés (via `RuntimeProducer` du `MQTTModule`):
- `rt/poollogic/ph`
- `rt/poollogic/orp`
- `rt/poollogic/heat_assist`

Payload (champs principaux):
- `id`: `ph` ou `orp`
- `input`: valeur **échantillonnée lors du dernier compute PID** (pas la mesure live instantanée)
- `setpoint`: setpoint utilisé lors du dernier compute
- `error`: erreur utilisée lors du dernier compute
- `compute_ts`: timestamp du dernier compute PID
- `enabled`: boucle autorisée côté logique
- `demand`: demande ON/OFF calculée dans la fenêtre
- `actual`: état ON/OFF réel lu sur le device
- `kp`, `ki`, `kd`
- `window_ms`, `sample_ms`, `min_on_ms`
- `output_on_ms`
- `window_elapsed_ms`
- `electrolyse_mode`
- `ts`: timestamp snapshot

Sémantique importante:
- `input/setpoint/error` sont des valeurs latched au compute PID
- `rt/io/input/*` reste la source des mesures live brutes

### Snapshot `rt/poollogic/heat_assist` (lecture orientée usage)

- `en`: protocole autorisé (`auto_mode` + `heater_auto_mode`)
- `pr`: sondage 5 min en cours
- `ha`: chauffe active
- `fc`: cadence rapide active (5 min / 20 min)
- `ri`: motif courant brut (code interne)
- `ivm`: intervalle d'attente courant en minutes (20 ou 30)
- `prm`: temps restant du sondage courant (ms)
- `irm`: temps restant avant prochain sondage (ms)

## Config MQTT (`cfg/poollogic*`)

Publication autoportée via `MqttConfigRouteProducer` local:
- `cfg/poollogic` (agrégat base)
- `cfg/poollogic/mode`
- `cfg/poollogic/filtration`
- `cfg/poollogic/sensors`
- `cfg/poollogic/pid`
- `cfg/poollogic/delay`
- `cfg/poollogic/device`

Le mapping `ConfigChanged -> messageId -> topic` est local au module.

## Home Assistant

Entités enregistrées par `PoolLogicModule`:
- switches:
  - `pool_auto_mode` (`Pool Auto-regulation`)
  - `pool_winter_mode`
  - `pool_ph_auto_mode` (`pH Auto-regulation`)
  - `pool_orp_auto_mode` (`Orp Auto-regulation`)
- sensors:
  - `calculated_filtration_start`
  - `calculated_filtration_stop`
  - `heat_assist_status`
- numbers (section configuration):
  - `delay_pids_min`
  - `ph_setpoint`
  - `orp_setpoint`
  - `ph_pid_window_min` (conversion vers `ph_window_ms`)
  - `orp_pid_window_min` (conversion vers `orp_window_ms`)
  - `psi_low_threshold`
  - `psi_high_threshold`
- button:
  - `filtration_recalc` -> `{"cmd":"poollogic.filtration.recalc"}`

## DataStore / EventStore

- pas d'écriture directe `DataStore` par `PoolLogicModule`
- interactions runtime via services (`IOServiceV2`, `PoolDeviceService`, `AlarmService`)
- pas d'`EventStore` persistant côté `PoolLogic` (événements runtime via `EventBus`, config persistante via `ConfigStore`)
