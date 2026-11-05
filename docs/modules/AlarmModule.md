# AlarmModule (`moduleId: alarms`)

## Rôle

Moteur d'alarmes central:
- enregistrement des définitions d'alarmes (`AlarmRegistration`)
- évaluation cyclique de conditions (`AlarmCondFn`)
- latching, reset manuel, délais ON/OFF, snapshots
- émission d'événements de cycle de vie d'alarme

Type: module actif.

## Dépendances

- `loghub`
- `eventbus`
- `cmd`

## Affinité / cadence

- core: 1
- task: `alarms`
- période d'évaluation: config `eval_period_ms` clampée [25..5000] ms

## Services exposés

- `alarms` -> `AlarmService`
  - `registerAlarm`, `reset`, `resetAll`
  - `isActive`, `isResettable`, `activeCount`, `highestSeverity`
  - `buildSnapshot`, `listIds`, `buildAlarmState`, `buildPacked`

## Services consommés

- `eventbus` (émission événements)
- `cmd` (commandes)
- `activitylog` optionnel (journal utilisateur des transitions d'alarme)

## Config / NVS

Module config `alarms` (`moduleId = ConfigModuleId::Alarms`, branche locale `1`):
- `enabled`
- `eval_period_ms`

## Commandes

- `alarms.list`
- `alarms.reset` (args `{id}`)
- `alarms.reset_slot` (args `{slot}` -> résolution `slot -> id`)
- `alarms.reset_all`

## Modèle d'état d'une alarme

Chaque slot garde notamment:
- `lastCond`: `False` / `True` / `Unknown`
- `active`: état d'alarme actif
- timers `onSinceMs` / `offSinceMs`

Transitions:
- `cond=True` pendant `onDelayMs` -> `active=true`
- `cond=False`:
  - si non latched -> clear après `offDelayMs`
  - si latched -> reste active jusqu'à commande `reset`
- `cond=Unknown` -> pas de transition (timers annulés, état conservé)

## Sémantique latch / reset

- `latched=false`: clear automatique quand la condition retombe
- `latched=true`: clear nécessite `condition=false` puis une commande `reset`
- `reset` n'est accepté que si l'alarme est encore active et que sa condition est déjà redevenue fausse
- `reset_all` ne touche que les alarmes latched actives actuellement réarmables

Conséquence opérationnelle importante:
- une alarme latched peut rester `active=true` même si la condition est redevenue fausse (tant qu'elle n'est pas reset)
- c'est voulu pour garder la trace d'un défaut de sécurité jusqu'à réarmement manuel

## EventBus

Émissions:
- `AlarmConditionChanged`
- `AlarmRaised`
- `AlarmCleared`
- `AlarmReset`

Abonnements:
- aucun

## DataStore / MQTT

Aucun accès direct au DataStore.
MQTT consomme les événements alarmes via `MQTTModule`.

## Notifications et anti-spam

Les transitions utiles sont publiées immédiatement sur l'EventBus:
- `AlarmRaised`
- `AlarmCleared`
- `AlarmReset`
- `AlarmConditionChanged` quand la condition change réellement

`minRepeatMs` ne sert plus à retarder ces transitions. Il ne sert qu'aux rappels
périodiques `AlarmConditionChanged` tant qu'une alarme reste active avec
`condition=true`.

## Journal d'activité

Pour les définitions dont `activityLogEnabled=true`, le module écrit directement
dans `ActivityLogService` afin de conserver la sémantique exacte des transitions:

- `AlarmRaised`: déclenchement effectif après le délai ON
- `AlarmConditionOk`: condition redevenue fausse
  - immédiatement pour une alarme latched devenue réarmable
  - à la levée effective après le délai OFF pour une alarme non latched
- `AlarmReset`: reset manuel accepté

Les passages initiaux `Unknown -> False`, les défauts qui disparaissent avant le
délai ON et les rappels `minRepeatMs` ne sont pas journalisés. Le `AlarmCleared`
émis après un reset ne crée pas une seconde entrée. Le champ persistant/API
`alarm_id` conserve l'identifiant stable de l'alarme.

## Alarmes définies actuellement

Le module est générique. Dans le projet actuel, `PoolLogicModule` enregistre:
- `AlarmId::PoolPsiLow`
- `AlarmId::PoolPsiHigh`
- `AlarmId::PoolPhTankLow`
- `AlarmId::PoolChlorineTankLow`

Pour `PoolLogic`, ces alarmes servent d'interlock sécurité:
- `PoolLogic` lit `isActive()` sur ces IDs
- si l'une est active, la filtration est forcée OFF (auto et manuel)
