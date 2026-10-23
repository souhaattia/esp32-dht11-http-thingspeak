# ESP32 - DHT11 vers ThingSpeak (ESP-IDF)

Projet ESP-IDF : un ESP32 se connecte en Wi-Fi (mode station), lit la temperature et l'humidite d'un capteur **DHT11**, puis les envoie periodiquement a **ThingSpeak** par requete **HTTP POST**.

> TP3 - Systemes embarques / IoT - ENICarthage
https://github.com/souhaattia/esp32-dht11-http-thingspeak/blob/main/Demo/video_x2.mp4
## Fonctionnalites

- Wi-Fi station avec reconnexion automatique (EventGroup + gestionnaire d'evenements)
- Lecture du DHT11 via le composant `esp-idf-lib/dht`
- Envoi HTTP POST (`application/x-www-form-urlencoded`) vers `api.thingspeak.com/update`
- Recuperation du corps de la reponse via un `event_handler` pour valider l'envoi (HTTP 200 et identifiant d'entree > 0)
- Configuration par `menuconfig` (aucun identifiant en dur dans le code)
- Journalisation avec `ESP_LOG`

## Materiel

| Element | Detail |
|---|---|
| Carte | ESP32-DevKitC (ESP32-WROOM) |
| Capteur | DHT11 (module 3 broches, ou capteur nu + resistance pull-up 10 kOhm) |
| Divers | Cables Dupont, cable USB (donnees) |

## Cablage

| DHT11 | ESP32 |
|---|---|
| VCC / + | 3V3 |
| GND / - | GND |
| DATA / S | GPIO4 (modifiable dans menuconfig) |

## Structure du projet

```
esp32-dht-thingspeak/
├─ CMakeLists.txt
├─ sdkconfig.defaults        # valeurs par defaut (sans secret)
├─ main/
│  ├─ CMakeLists.txt
│  ├─ idf_component.yml      # dependance esp-idf-lib/dht (Component Registry)
│  ├─ Kconfig.projbuild      # menu de configuration
│  └─ main.c
├─ .gitignore
├─ LICENSE
└─ README.md
```

## Prerequis

- ESP-IDF **5.x** (teste avec 5.5)
- VS Code + extension **Espressif IDF** (ou terminal ESP-IDF)
- Compte [ThingSpeak](https://thingspeak.mathworks.com) avec un channel :
  - `Field 1` = Temperature (degC)
  - `Field 2` = Humidity (%)

## Installation et configuration

```bash
git clone https://github.com/<votre-utilisateur>/esp32-dht-thingspeak.git
cd esp32-dht-thingspeak
idf.py set-target esp32
idf.py menuconfig
```

Dans le menu **TP: Configuration Wi-Fi, DHT et ThingSpeak** :

| Parametre | Valeur |
|---|---|
| Wi-Fi SSID | nom de votre reseau **2,4 GHz** |
| Wi-Fi Password | mot de passe du reseau |
| ThingSpeak WRITE API Key | cle *Write API Key* du channel |
| GPIO du DHT (DATA) | `4` par defaut |
| Periode d'envoi (s) | `15` (minimum ThingSpeak en compte gratuit) |

Le composant `dht` est telecharge automatiquement au premier build (dossier `managed_components/`).

## Compilation, flash et moniteur

```bash
idf.py build
idf.py -p COMx flash monitor      # remplacer COMx (ou /dev/ttyUSB0)
```

Dans VS Code : `Ctrl+E B` (build), `Ctrl+E F` (flash), `Ctrl+E M` (monitor), `Ctrl+E D` (tout enchainer).

Si le flash reste bloque sur `Connecting....`, maintenir le bouton **BOOT** de la carte jusqu'au debut du transfert.

## Resultat attendu

```
I (xxx) TP: Connexion Wi-Fi a <SSID>...
I (xxx) TP: IP: 192.168.x.x
I (xxx) TP: Temperature: 24.0 C, Humidite: 45.0 %
I (xxx) TP: HTTP 200, body='123'
I (xxx) TP: Publication ThingSpeak OK
```

Les graphiques *Field 1* et *Field 2* du channel ThingSpeak se mettent a jour toutes les 15 s.

## Fonctionnement du client HTTP

Cycle minimal de `esp_http_client` utilise dans `thingspeak_post()` :

1. **Configuration** : `esp_http_client_config_t` (URL, methode POST, timeout, event handler)
2. **Initialisation** : `esp_http_client_init()`
3. **Parametrage** : `esp_http_client_set_header()` et `esp_http_client_set_post_field()`
4. **Execution** : `esp_http_client_perform()` (appel bloquant)
5. **Inspection** : `esp_http_client_get_status_code()` et corps de reponse
6. **Nettoyage** : `esp_http_client_cleanup()`

## Depannage

| Symptome | Cause probable |
|---|---|
| Bloque sur "Connexion Wi-Fi" | SSID ou mot de passe errone, reseau 5 GHz |
| `DHT lecture echouee: ESP_ERR_TIMEOUT` | Cablage, mauvais GPIO, pull-up manquante |
| `HTTP erreur: ESP_ERR_HTTP_CONNECT` | Pas d'acces Internet sur le reseau |
| `HTTP 200, body='0'` | Mauvaise Write API Key ou envois < 15 s d'intervalle |
| `Wrong boot mode detected (0x17)` au flash | Maintenir BOOT pendant la connexion, changer de cable USB |
| Redemarrages en boucle | Augmenter *Main task stack size* (4096) |

## Securite

- Le fichier `sdkconfig` contient le mot de passe Wi-Fi et la cle API : il est **ignore par Git** (`.gitignore`). Ne le publiez jamais.
- Si une cle a ete exposee, regenerez-la dans ThingSpeak : *API Keys > Generate New Write API Key*.
- La communication utilise `http://` (comme dans l'enonce du TP). Pour du HTTPS, activer le bundle de certificats mbedTLS et utiliser `.crt_bundle_attach = esp_crt_bundle_attach`.

## Pistes d'amelioration

- Passage en HTTPS avec verification du certificat
- Ajout d'une tache FreeRTOS dediee a la lecture / l'envoi
- Mode deep sleep entre deux mesures
- Passage au DHT22 (`DHT_TYPE_AM2301`) pour plus de precision

## Auteur

**Souha Attia** - Etudiante ingenieur en systemes embarques, ENICarthage

## Licence

Distribue sous licence MIT. Voir le fichier [LICENSE](LICENSE).
