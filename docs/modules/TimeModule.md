# Heure et planification

## A quoi sert cette fonction

Flow.io utilise l'heure pour afficher la bonne date, piloter les programmes
horaires et declencher les actions planifiees, comme les fenetres de filtration
ou certains dosages.

Le systeme peut continuer a demarrer sans heure valide. Dans ce cas, les
fonctions physiques et les securites restent disponibles, mais les actions qui
dependent d'une heure fiable attendent qu'une source d'heure soit acceptee.

## Sources d'heure utilisees

Flow.io choisit automatiquement la meilleure source disponible:

1. **NTP**: l'heure recue par le reseau. C'est la source la plus fiable.
2. **RTC**: l'horloge interne de la carte. Elle sert de secours quand le reseau
   n'est pas disponible.
3. **Heure manuelle**: l'heure saisie par l'utilisateur pendant la session.
4. **Aucune heure valide**: Flow.io ne declenche pas les programmes horaires.

Si NTP devient disponible apres une heure RTC ou une heure manuelle, NTP reprend
la main et corrige l'heure.

## Heure locale et fuseau horaire

En interne, Flow.io garde l'heure en UTC. Le fuseau horaire configure dans
`time/tz` est applique uniquement pour l'affichage et pour les programmes
exprimes en heure locale.

Du point de vue utilisateur:
- l'heure affichee dans l'interface web est locale;
- l'heure envoyee a l'ecran est locale;
- une fenetre de filtration configuree de 8h a 18h est comparee a l'heure
  locale;
- les changements d'heure ete/hiver suivent le fuseau horaire configure.

## Synchronisation reseau

Quand le reseau est disponible et que NTP est active:
- Flow.io synchronise l'heure par NTP;
- le badge d'heure affiche `NTP`;
- les programmes horaires peuvent fonctionner;
- l'horloge interne de la carte est mise a jour pour les prochains redemarrages
  sans reseau.

Si NTP echoue, Flow.io garde la meilleure heure deja disponible. Une erreur NTP
ne rend pas invalide une heure RTC deja acceptee.

## Horloge RTC

Quand le reseau n'est pas disponible, Flow.io peut utiliser l'horloge interne de
la carte.

Cette horloge n'est acceptee que si sa date semble coherente. Si elle retourne
une date manifestement fausse, par exemple 1970, 2000 ou une date trop ancienne,
Flow.io l'ignore et attend une autre source.

Quand l'horloge RTC est acceptee:
- le badge d'heure affiche `RTC`;
- la page Informations affiche `Synchronisee (RTC)`;
- les programmes horaires peuvent fonctionner.

## Heure manuelle

L'utilisateur peut saisir une heure manuelle au format:

```text
YYYY-MM-DD HH:MM:SS
```

Exemple:

```text
2026-06-09 18:30:00
```

Cette heure est interpretee comme une heure locale, puis convertie par Flow.io
avant d'etre appliquee.

Quand une heure manuelle est acceptee:
- le badge d'heure affiche `manuel`;
- la page Informations affiche `Synchronisee (manuel)`;
- les programmes horaires peuvent fonctionner;
- Flow.io peut recopier cette heure vers l'horloge interne de la carte.

Une heure manuelle saisie dans la configuration ne doit pas etre comprise comme
une horloge permanente en elle-meme. Apres redemarrage, Flow.io ne fait pas
confiance a une ancienne valeur manuelle simplement parce qu'elle est encore
presente dans la configuration. Pour etre reutilisee sans reseau, elle doit
avoir ete conservee par l'horloge RTC de la carte et etre jugee coherente au
redemarrage.

Si NTP reussit plus tard, il remplace l'heure manuelle.

## Ce que montre l'interface web

Le badge en haut de l'interface indique la confiance actuelle dans l'heure:

- `Heure (NTP)`: heure synchronisee par le reseau.
- `Heure (RTC)`: heure reprise depuis l'horloge interne.
- `Heure (manuel)`: heure reglee manuellement pendant la session.
- `Heure`: aucune source fiable n'est encore disponible.

La page Informations affiche le meme etat dans la ligne `Heure`:

- `Synchronisee (NTP)`
- `Synchronisee (RTC)`
- `Synchronisee (manuel)`
- `Non synchronisee`

Le badge d'en-tete et la page Informations utilisent le meme niveau de confiance.
Ils doivent donc afficher le meme type de source.

## Effet sur la filtration et les programmes

Les programmes horaires sont compares a l'heure locale. Par exemple, une
filtration de 8h a 18h signifie 8h a 18h dans le fuseau horaire configure, pas
8h a 18h UTC.

Tant que l'heure n'est pas fiable:
- les declenchements horaires restent en attente;
- Flow.io evite de lancer une action planifiee sur une date inconnue;
- une filtration deja active peut etre conservee temporairement au demarrage,
  jusqu'a ce que Flow.io puisse prendre une decision horaire fiable.

Quand une source plus fiable arrive, Flow.io corrige l'heure. Si la correction
est importante, l'evenement est journalise afin d'expliquer un changement de
comportement horaire.

## Scenarios typiques

1. **Demarrage avec reseau**: Flow.io synchronise par NTP et affiche `NTP`.
2. **Demarrage sans reseau avec RTC correcte**: Flow.io utilise le RTC et affiche
   `RTC`.
3. **Demarrage sans reseau avec RTC incoherente**: Flow.io refuse l'heure et
   affiche une heure non synchronisee.
4. **Heure manuelle sans reseau**: Flow.io applique l'heure saisie et affiche
   `manuel`.
5. **NTP disponible apres une heure manuelle**: Flow.io remplace l'heure
   manuelle par NTP et affiche `NTP`.
6. **NTP echoue apres une heure RTC valide**: Flow.io conserve l'heure RTC et
   continue d'afficher `RTC`.

## Diagnostic utilisateur

Pour comprendre l'etat de l'heure:

- regarder le badge `Heure (...)` en haut de l'interface;
- ouvrir la page Informations et verifier la ligne `Heure`;
- verifier que le fuseau horaire configure correspond bien au lieu
  d'installation;
- si les programmes horaires ne demarrent pas, verifier d'abord que l'heure est
  indiquee comme synchronisee.
