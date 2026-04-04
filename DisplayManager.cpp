/**
 * @file    DisplayManager.cpp
 * @brief   Implementation of DisplayManager — Core 1 render loop, touch handling, and all UI screens.
 * @details Contains the complete rendering engine: Core 1 entry point, snapshot-
 *          based dirty rendering, dashboard with ambient/slot panels, graph
 *          plotting with dual Y-axis, settings menus (themes, alarms, sounds,
 *          language, password, calibration, license), authentication keypad with
 *          scrambled layout and lockout, alarm flash animation with per-slot
 *          masking, and the i18n dictionary for 8 languages.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "DisplayManager.h"
#include "LogManager.h"
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "pico/multicore.h"
}


static const int TOTAL_LANGS = 8;

static const char* const LANG_NAMES[TOTAL_LANGS] = {
    "English", "Portugues", "Espanol", "Francais",
    "Deutsch", "Italiano", "Russkiy", "Zhongwen"
};
static const char* const LANG_FLAGS[TOTAL_LANGS] = {
    "EN", "PT", "ES", "FR", "DE", "IT", "RU", "ZH"
};

const char* const DICTIONARY[TOTAL_LANGS][TR_KEYS_COUNT] = {

    {
        "AMBIENT", "Settings > Main", "Settings > Themes", "Settings > Language",
        "EXIT", "APPLY", "CANCEL", "Security Authentication", "ACCESS BLOCKED",
        "Reboot required", "Attempts Exceeded", "Wait %ld seconds...",
        "Invalid Password!", "Loading...", "Reading History...", "No Data",
        "MAXIMUM", "MINIMUM", "Temperature", "Humidity", "PLOT CHART",
        "1. Visual Themes", "2. Alarm Limits", "3. Alarm Sounds", "4. System Language",
        "Applying Theme...", "SAVE", "Alarm Limits", "Temp Min", "Temp Max", "Hum Min", "Hum Max", "ENTER", "SKIP", "5. Change Password", "New Password",
        "6. Touch Calibration", "Touch Calibration", "Touch the crosshair", "Calibration Done!",
        "Imprecise touches! Try again.",
        "Confirm Password", "Password too short! (min 4)", "Passwords don't match!", "Password saved!", "UNDERSTOOD",
        "Sound Settings", "Touch Click", "Confirmation", "Error Sound", "Alarm Sound",
        "Mute All", "Sys Volume", "Alarm Vol", "ON", "OFF",
        "Web Access", "Melody",
        "7. License", "MIT License",
        "ACTIVE",
        "Silence 120s", "Deactivate", "Min/Max", "Silenced",
        "%RH"
    },

    {
        "AMBIENTE", "Configuracoes > Principal", "Configuracoes > Temas", "Configuracoes > Idioma",
        "SAIR", "APLICAR", "CANCELAR", "Autenticacao de Seguranca", "ACESSO BLOQUEADO",
        "Reinicializacao requerida", "Tentativas Excedidas", "Aguarde %ld segundos...",
        "Senha Invalida!", "Carregando...", "Lendo Historico...", "Sem Dados",
        "MAXIMO", "MINIMO", "Temperatura", "Umidade", "GERAR GRAFICO",
        "1. Temas Visuais", "2. Limites de Alarme", "3. Sons de Alarme", "4. Idioma do Sistema",
        "Aplicando Tema...", "SALVAR", "Limites de Alarme", "Temp Min", "Temp Max", "Umid Min", "Umid Max", "ENTRAR", "PULAR", "5. Alterar Senha", "Nova Senha",
        "6. Calibrar Touch", "Calibracao do Touch", "Toque na mira", "Calibracao Concluida!",
        "Toques imprecisos! Tente novamente.",
        "Confirmar Senha", "Senha muito curta! (min 4)", "Senhas nao coincidem!", "Senha salva!", "ENTENDI",
        "Config. de Sons", "Toque na Tela", "Confirmacao", "Som de Erro", "Som de Alarme",
        "Silenciar Tudo", "Vol. Sistema", "Vol. Alarme", "SIM", "NAO",
        "Acesso Web", "Melodia",
        "7. Licenca", "Licenca MIT",
        "ATIVO",
        "Silenciar 120s", "Desativar", "Min/Max", "Silenciado",
        "%UR"
    },

    {
        "AMBIENTE", "Ajustes > Principal", "Ajustes > Temas", "Ajustes > Idioma",
        "SALIR", "APLICAR", "CANCELAR", "Autenticacion de Seguridad", "ACCESO BLOQUEADO",
        "Reinicio requerido", "Intentos Excedidos", "Espere %ld segundos...",
        "Clave Invalida!", "Cargando...", "Leyendo Historial...", "Sin Datos",
        "MAXIMO", "MINIMO", "Temperatura", "Humedad", "GENERAR GRAFICO",
        "1. Temas Visuales", "2. Limites de Alarma", "3. Sonidos de Alarma", "4. Idioma del Sistema",
        "Aplicando Tema...", "GUARDAR", "Limites de Alarma", "Temp Min", "Temp Max", "Humed Min", "Humed Max", "ENTRAR", "OMITIR", "5. Cambiar Clave", "Nueva Clave",
        "6. Calibrar Touch", "Calibracion del Touch", "Toque la mira", "Calibracion Completa!",
        "Toques imprecisos! Intente de nuevo.",
        "Confirmar Clave", "Clave muy corta! (min 4)", "Claves no coinciden!", "Clave guardada!", "ENTENDIDO",
        "Config. de Sonidos", "Toque en Pantalla", "Confirmacion", "Sonido de Error", "Sonido de Alarma",
        "Silenciar Todo", "Vol. Sistema", "Vol. Alarma", "SI", "NO",
        "Acceso Web", "Melodia",
        "7. Licencia", "Licencia MIT",
        "ACTIVO",
        "Silenciar 120s", "Desactivar", "Min/Max", "Silenciado",
        "%HR"
    },

    {
        "AMBIANCE", "Reglages > Principal", "Reglages > Themes", "Reglages > Langue",
        "QUITTER", "APPLIQUER", "ANNULER", "Authentification de Securite", "ACCES BLOQUE",
        "Redemarrage requis", "Tentatives Depassees", "Patientez %ld secondes...",
        "Mot de passe invalide!", "Chargement...", "Lecture Historique...", "Aucune Donnee",
        "MAXIMUM", "MINIMUM", "Temperature", "Humidite", "TRACER GRAPHIQUE",
        "1. Themes Visuels", "2. Limites d'Alarme", "3. Sons d'Alarme", "4. Langue du Systeme",
        "Application du Theme...", "ENREGISTRER", "Limites d'Alarme", "Temp Min", "Temp Max", "Hum Min", "Hum Max", "ENTRER", "PASSER", "5. Changer Mot de Passe", "Nouveau Mot de Passe",
        "6. Calibrer le Tactile", "Calibration Tactile", "Touchez la cible", "Calibration Terminee!",
        "Touches imprecis! Reessayez.",
        "Confirmer Mot de Passe", "Mot de passe trop court! (min 4)", "Mots de passe differents!", "Mot de passe enregistre!", "COMPRIS",
        "Reglages des Sons", "Toucher Ecran", "Confirmation", "Son d'Erreur", "Son d'Alarme",
        "Tout Couper", "Vol. Systeme", "Vol. Alarme", "OUI", "NON",
        "Acces Web", "Melodie",
        "7. Licence", "Licence MIT",
        "ACTIF",
        "Silencer 120s", "Desactiver", "Min/Max", "En Silence",
        "%HR"
    },

    {
        "UMGEBUNG", "Einstellungen > Haupt", "Einstellungen > Themen", "Einstellungen > Sprache",
        "BEENDEN", "ANWENDEN", "ABBRECHEN", "Sicherheitsauthentifizierung", "ZUGANG GESPERRT",
        "Neustart erforderlich", "Versuche Ueberschritten", "Warten Sie %ld Sekunden...",
        "Ungultiges Passwort!", "Laden...", "Verlauf Lesen...", "Keine Daten",
        "MAXIMUM", "MINIMUM", "Temperatur", "Feuchtigkeit", "DIAGRAMM ERSTELLEN",
        "1. Visuelle Themen", "2. Alarmgrenzen", "3. Alarmtone", "4. Systemsprache",
        "Thema Anwenden...", "SPEICHERN", "Alarmgrenzen", "Temp Min", "Temp Max", "Feuch Min", "Feuch Max", "EINGABE", "WEITER", "5. Passwort Aendern", "Neues Passwort",
        "6. Touch Kalibrieren", "Touch-Kalibrierung", "Fadenkreuz beruehren", "Kalibrierung Fertig!",
        "Ungenaue Beruehrungen! Erneut versuchen.",
        "Passwort Bestaetigen", "Passwort zu kurz! (min 4)", "Passwoerter stimmen nicht!", "Passwort gespeichert!", "VERSTANDEN",
        "Toneinstellungen", "Bildschirmberuehrung", "Bestaetigung", "Fehlerton", "Alarmton",
        "Alles Stumm", "Sys-Lautst.", "Alarm-Lautst.", "EIN", "AUS",
        "Web-Zugriff", "Melodie",
        "7. Lizenz", "MIT-Lizenz",
        "AKTIV",
        "Stumm 120s", "Deaktivieren", "Min/Max", "Stummgeschaltet",
        "%RH"
    },

    {
        "AMBIENTE", "Impostazioni > Principale", "Impostazioni > Temi", "Impostazioni > Lingua",
        "ESCI", "APPLICA", "ANNULLA", "Autenticazione di Sicurezza", "ACCESSO BLOCCATO",
        "Riavvio necessario", "Tentativi Superati", "Attendere %ld secondi...",
        "Password non valida!", "Caricamento...", "Lettura Cronologia...", "Nessun Dato",
        "MASSIMO", "MINIMO", "Temperatura", "Umidita", "GENERA GRAFICO",
        "1. Temi Visivi", "2. Limiti di Allarme", "3. Suoni di Allarme", "4. Lingua del Sistema",
        "Applicazione Tema...", "SALVA", "Limiti di Allarme", "Temp Min", "Temp Max", "Umid Min", "Umid Max", "INVIO", "SALTA", "5. Cambia Password", "Nuova Password",
        "6. Calibra Touch", "Calibrazione Touch", "Tocca il mirino", "Calibrazione Completata!",
        "Tocchi imprecisi! Riprova.",
        "Conferma Password", "Password troppo corta! (min 4)", "Password non corrispondono!", "Password salvata!", "CAPITO",
        "Impostaz. Suoni", "Tocco Schermo", "Conferma", "Suono Errore", "Suono Allarme",
        "Silenzia Tutto", "Vol. Sistema", "Vol. Allarme", "SI", "NO",
        "Accesso Web", "Melodia",
        "7. Licenza", "Licenza MIT",
        "ATTIVO",
        "Silenzia 120s", "Disattiva", "Min/Max", "Silenziato",
        "%UR"
    },

    {
        "OKRUZHENIE", "Nastroyki > Glavnaya", "Nastroyki > Temy", "Nastroyki > Yazyk",
        "VYKHOD", "PRIMENIT", "OTMENA", "Autentifikaciya Bezopasnosti", "DOSTUP ZABLOKIROVAN",
        "Trebuetsya perezagruzka", "Popytki Prevysheny", "Zhdite %ld sekund...",
        "Nevernyy parol!", "Zagruzka...", "Chtenie Istorii...", "Net Dannykh",
        "MAKSIMUM", "MINIMUM", "Temperatura", "Vlazhnost", "POSTROIT GRAFIK",
        "1. Vizualnye Temy", "2. Predely Signalizacii", "3. Zvuki Signalizacii", "4. Yazyk Sistemy",
        "Primenenie Temy...", "SOKHRANIT", "Predely Signalizacii", "Temp Min", "Temp Maks", "Vlazh Min", "Vlazh Maks", "VVOD", "PROPUSTIT", "5. Smena Parolya", "Novyy Parol",
        "6. Kalibrovka Tachskrina", "Kalibrovka Tachskrina", "Kosnityes perekrestiya", "Kalibrovka Zavershena!",
        "Netochnyye kasaniya! Povtorite.",
        "Podtverdite Parol", "Parol slishkom korotkiy! (min 4)", "Paroli ne sovpadayut!", "Parol sokhranyon!", "PONYATNO",
        "Nastroyki Zvukov", "Kasanie Ekrana", "Podtverzhdenie", "Zvuk Oshibki", "Zvuk Signalizacii",
        "Vykl. Vse Zvuki", "Sis. Gromk.", "Alarm Gromk.", "VKL", "VYKL",
        "Veb Dostup", "Melodiya",
        "7. Licenziya", "Licenziya MIT",
        "AKTIVNO",
        "Tishina 120s", "Otklyuchit", "Min/Maks", "Otklyucheno",
        "%RH"
    },

    {
        "HUANJING", "Shezhi > Zhuyao", "Shezhi > Zhuti", "Shezhi > Yuyan",
        "TUICHU", "YINGYONG", "QUXIAO", "Anquan Yanzheng", "FANGWEN BEISUODING",
        "Xuyao Chongqi", "Changshi Chaoguo", "Qing Dengdai %ld Miao...",
        "Mima Wuxiao!", "Jiazai Zhong...", "Duqu Lishi...", "Wu Shuju",
        "ZUIDA", "ZUIXIAO", "Wendu", "Shidu", "SHENGCHENG TUBIAO",
        "1. Shijue Zhuti", "2. Baojing Xianzhi", "3. Baojing Shengyin", "4. Xitong Yuyan",
        "Yingyong Zhuti...", "BAOCUN", "Baojing Xianzhi", "Wen Min", "Wen Zui", "Shi Min", "Shi Zui", "QUEREN", "TIAOGUO", "5. Xiugai Mima", "Xin Mima",
        "6. Chuping Jiaozhun", "Chuping Jiaozhun", "Qing Chumu Shizi", "Jiaozhun Wancheng!",
        "Chumu Bu Jingque! Qing Chongshi.",
        "Queren Mima", "Mima Tai Duan! (min 4)", "Mima Bu Yizhi!", "Mima Yi Baocun!", "MINGBAI",
        "Shengyin Shezhi", "Chuping Chumu", "Queren Shengyin", "Cuowu Shengyin", "Baojing Shengyin",
        "Jingyin Quanbu", "Xit. Yinliang", "Baoj. Yinliang", "KAI", "GUAN",
        "Web Fangwen", "Xuanlv",
        "7. Xuke Zheng", "MIT Xukezheng",
        "QIYONG",
        "Jingyin 120m", "Tingzhi", "Min/Max", "Yi Jingyin",
        "%RH"
    }
};


static const char LICENSE_EN[] =
    "MIT License\n\n"
    "Copyright (c) 2026 Angelo Moises Alves\n\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy "
    "of this software and associated documentation files (the \"Software\"), to deal "
    "in the Software without restriction, including without limitation the rights "
    "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
    "copies of the Software, and to permit persons to whom the Software is "
    "furnished to do so, subject to the following conditions:\n\n"
    "The above copyright notice and this permission notice shall be included in all "
    "copies or substantial portions of the Software.\n\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
    "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
    "SOFTWARE.\n\n"
    "--- Acknowledgments ---\n\n"
    "Arduino-Pico Core\n"
    "  Earle F. Philhower III - LGPL-2.1\n\n"
    "Raspberry Pi Pico SDK\n"
    "  Raspberry Pi Ltd - BSD-3\n\n"
    "Adafruit GFX Library\n"
    "  Adafruit Industries - BSD-2\n\n"
    "Adafruit ILI9341\n"
    "  Adafruit Industries - BSD-2\n\n"
    "XPT2046 Touchscreen\n"
    "  Paul Stoffregen - MIT\n\n"
    "LittleFS\n"
    "  ARM Ltd / C. Haster - BSD-3\n\n"
    "PubSubClient (MQTT)\n"
    "  Nick O'Leary - MIT\n\n"
    "BearSSL\n"
    "  Thomas Pornin - MIT\n\n"
    "GNU FreeFont (FreeSans)\n"
    "  GNU Project - GPL-3 + Font Exception\n\n"
    "OneWirePIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "DHT22PIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "BuzzerPIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "SIMUT v3 - Made in Brazil";

static const char LICENSE_PT[] =
    "Licenca MIT\n\n"
    "Direitos Autorais (c) 2026 Angelo Moises Alves\n\n"
    "E concedida permissao, gratuitamente, a qualquer pessoa que obtenha uma copia "
    "deste software e dos arquivos de documentacao associados (o \"Software\"), para "
    "lidar com o Software sem restricoes, incluindo, sem limitacao, os direitos de "
    "usar, copiar, modificar, fundir, publicar, distribuir, sublicenciar e/ou vender "
    "copias do Software, e permitir que as pessoas a quem o Software e fornecido o "
    "facam, sujeito as seguintes condicoes:\n\n"
    "O aviso de direitos autorais acima e este aviso de permissao devem ser incluidos "
    "em todas as copias ou partes substanciais do Software.\n\n"
    "O SOFTWARE E FORNECIDO \"COMO ESTA\", SEM GARANTIA DE QUALQUER TIPO, EXPRESSA OU "
    "IMPLICITA, INCLUINDO, MAS NAO SE LIMITANDO AS GARANTIAS DE COMERCIALIZACAO, "
    "ADEQUACAO A UM PROPOSITO ESPECIFICO E NAO VIOLACAO. EM NENHUM CASO OS AUTORES OU "
    "DETENTORES DE DIREITOS AUTORAIS SERAO RESPONSAVEIS POR QUALQUER RECLAMACAO, DANO "
    "OU OUTRA RESPONSABILIDADE, SEJA EM UMA ACAO DE CONTRATO, ATO ILICITO OU DE OUTRA "
    "FORMA, DECORRENTE DE, OU EM CONEXAO COM O SOFTWARE OU O USO OU OUTRAS NEGOCIACOES "
    "NO SOFTWARE.\n\n"
    "--- Agradecimentos ---\n\n"
    "Arduino-Pico Core\n"
    "  Earle F. Philhower III - LGPL-2.1\n\n"
    "Raspberry Pi Pico SDK\n"
    "  Raspberry Pi Ltd - BSD-3\n\n"
    "Adafruit GFX Library\n"
    "  Adafruit Industries - BSD-2\n\n"
    "Adafruit ILI9341\n"
    "  Adafruit Industries - BSD-2\n\n"
    "XPT2046 Touchscreen\n"
    "  Paul Stoffregen - MIT\n\n"
    "LittleFS\n"
    "  ARM Ltd / C. Haster - BSD-3\n\n"
    "PubSubClient (MQTT)\n"
    "  Nick O'Leary - MIT\n\n"
    "BearSSL\n"
    "  Thomas Pornin - MIT\n\n"
    "GNU FreeFont (FreeSans)\n"
    "  GNU Project - GPL-3 + Font Exception\n\n"
    "OneWirePIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "DHT22PIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "BuzzerPIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "SIMUT v3 - Feito no Brasil";

static const char LICENSE_ES[] =
    "Licencia MIT\n\n"
    "Copyright (c) 2026 Angelo Moises Alves\n\n"
    "Se concede permiso, de forma gratuita, a cualquier persona que obtenga una copia "
    "de este software y de los archivos de documentacion asociados (el \"Software\"), "
    "para tratar el Software sin restriccion, incluyendo sin limitacion los derechos "
    "de usar, copiar, modificar, fusionar, publicar, distribuir, sublicenciar y/o "
    "vender copias del Software, y permitir a las personas a quienes se proporcione "
    "el Software hacerlo, sujeto a las siguientes condiciones:\n\n"
    "El aviso de copyright anterior y este aviso de permiso se incluiran en todas las "
    "copias o partes sustanciales del Software.\n\n"
    "EL SOFTWARE SE PROPORCIONA \"TAL CUAL\", SIN GARANTIA DE NINGUN TIPO, EXPRESA O "
    "IMPLICITA, INCLUYENDO PERO NO LIMITANDOSE A LAS GARANTIAS DE COMERCIALIZACION, "
    "IDONEIDAD PARA UN PROPOSITO PARTICULAR Y NO INFRACCION. EN NINGUN CASO LOS AUTORES "
    "O TITULARES DEL COPYRIGHT SERAN RESPONSABLES DE NINGUNA RECLAMACION, DANO U OTRA "
    "RESPONSABILIDAD, YA SEA EN UNA ACCION DE CONTRATO, AGRAVIO O DE OTRO MODO, "
    "DERIVADA DE, O EN CONEXION CON EL SOFTWARE O EL USO U OTRAS NEGOCIACIONES EN "
    "EL SOFTWARE.\n\n"
    "--- Reconocimientos ---\n\n"
    "Arduino-Pico Core\n"
    "  Earle F. Philhower III - LGPL-2.1\n\n"
    "Raspberry Pi Pico SDK\n"
    "  Raspberry Pi Ltd - BSD-3\n\n"
    "Adafruit GFX Library\n"
    "  Adafruit Industries - BSD-2\n\n"
    "Adafruit ILI9341\n"
    "  Adafruit Industries - BSD-2\n\n"
    "XPT2046 Touchscreen\n"
    "  Paul Stoffregen - MIT\n\n"
    "LittleFS\n"
    "  ARM Ltd / C. Haster - BSD-3\n\n"
    "PubSubClient (MQTT)\n"
    "  Nick O'Leary - MIT\n\n"
    "BearSSL\n"
    "  Thomas Pornin - MIT\n\n"
    "GNU FreeFont (FreeSans)\n"
    "  GNU Project - GPL-3 + Font Exception\n\n"
    "OneWirePIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "DHT22PIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "BuzzerPIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "SIMUT v3 - Hecho en Brasil";

static const char LICENSE_FR[] =
    "Licence MIT\n\n"
    "Copyright (c) 2026 Angelo Moises Alves\n\n"
    "La permission est accordee, gratuitement, a toute personne obtenant une copie de "
    "ce logiciel et des fichiers de documentation associes (le \"Logiciel\"), de traiter "
    "le Logiciel sans restriction, y compris sans limitation les droits d'utiliser, "
    "copier, modifier, fusionner, publier, distribuer, sous-licencier et/ou vendre des "
    "copies du Logiciel, et de permettre aux personnes auxquelles le Logiciel est fourni "
    "de le faire, sous reserve des conditions suivantes:\n\n"
    "L'avis de copyright ci-dessus et cet avis de permission doivent etre inclus dans "
    "toutes les copies ou parties substantielles du Logiciel.\n\n"
    "LE LOGICIEL EST FOURNI \"TEL QUEL\", SANS GARANTIE D'AUCUNE SORTE, EXPRESSE OU "
    "IMPLICITE, Y COMPRIS MAIS SANS S'Y LIMITER LES GARANTIES DE QUALITE MARCHANDE, "
    "D'ADEQUATION A UN USAGE PARTICULIER ET DE NON-CONTREFACON. EN AUCUN CAS LES "
    "AUTEURS OU LES TITULAIRES DU COPYRIGHT NE SERONT RESPONSABLES DE TOUTE RECLAMATION, "
    "DOMMAGE OU AUTRE RESPONSABILITE, QUE CE SOIT DANS UNE ACTION CONTRACTUELLE, "
    "DELICTUELLE OU AUTRE, DECOULANT DE, OU EN LIEN AVEC LE LOGICIEL OU L'UTILISATION "
    "OU AUTRES TRANSACTIONS DANS LE LOGICIEL.\n\n"
    "--- Remerciements ---\n\n"
    "Arduino-Pico Core\n"
    "  Earle F. Philhower III - LGPL-2.1\n\n"
    "Raspberry Pi Pico SDK\n"
    "  Raspberry Pi Ltd - BSD-3\n\n"
    "Adafruit GFX Library\n"
    "  Adafruit Industries - BSD-2\n\n"
    "Adafruit ILI9341\n"
    "  Adafruit Industries - BSD-2\n\n"
    "XPT2046 Touchscreen\n"
    "  Paul Stoffregen - MIT\n\n"
    "LittleFS\n"
    "  ARM Ltd / C. Haster - BSD-3\n\n"
    "PubSubClient (MQTT)\n"
    "  Nick O'Leary - MIT\n\n"
    "BearSSL\n"
    "  Thomas Pornin - MIT\n\n"
    "GNU FreeFont (FreeSans)\n"
    "  GNU Project - GPL-3 + Font Exception\n\n"
    "OneWirePIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "DHT22PIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "BuzzerPIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "SIMUT v3 - Fabrique au Bresil";

static const char LICENSE_DE[] =
    "MIT-Lizenz\n\n"
    "Copyright (c) 2026 Angelo Moises Alves\n\n"
    "Hiermit wird jeder Person, die eine Kopie dieser Software und der zugehoerigen "
    "Dokumentationsdateien (die \"Software\") erhaelt, kostenlos die Erlaubnis erteilt, "
    "die Software ohne Einschraenkung zu nutzen, einschliesslich und ohne Einschraenkung "
    "des Rechts, die Software zu verwenden, zu kopieren, zu aendern, zusammenzufuehren, "
    "zu veroeffentlichen, zu verteilen, unterzulizenzieren und/oder Kopien der Software "
    "zu verkaufen, und Personen, denen die Software zur Verfuegung gestellt wird, dies "
    "zu gestatten, unter den folgenden Bedingungen:\n\n"
    "Der obige Copyright-Hinweis und dieser Genehmigungshinweis muessen in allen Kopien "
    "oder wesentlichen Teilen der Software enthalten sein.\n\n"
    "DIE SOFTWARE WIRD \"WIE BESEHEN\" BEREITGESTELLT, OHNE JEGLICHE GEWAEHRLEISTUNG, "
    "WEDER AUSDRUECKLICH NOCH STILLSCHWEIGEND, EINSCHLIESSLICH, ABER NICHT BESCHRAENKT "
    "AUF DIE GEWAEHRLEISTUNG DER MARKTGAENGIGKEIT, DER EIGNUNG FUER EINEN BESTIMMTEN "
    "ZWECK UND DER NICHTVERLETZUNG. IN KEINEM FALL HAFTEN DIE AUTOREN ODER COPYRIGHT-"
    "INHABER FUER ANSPRUECHE, SCHAEDEN ODER ANDERE HAFTUNG, OB IN EINER VERTRAGSKLAGE, "
    "UNERLAUBTER HANDLUNG ODER ANDERWEITIG, DIE SICH AUS ODER IN VERBINDUNG MIT DER "
    "SOFTWARE ODER DER NUTZUNG ODER ANDEREN GESCHAEFTEN IN DER SOFTWARE ERGEBEN.\n\n"
    "--- Danksagungen ---\n\n"
    "Arduino-Pico Core\n"
    "  Earle F. Philhower III - LGPL-2.1\n\n"
    "Raspberry Pi Pico SDK\n"
    "  Raspberry Pi Ltd - BSD-3\n\n"
    "Adafruit GFX Library\n"
    "  Adafruit Industries - BSD-2\n\n"
    "Adafruit ILI9341\n"
    "  Adafruit Industries - BSD-2\n\n"
    "XPT2046 Touchscreen\n"
    "  Paul Stoffregen - MIT\n\n"
    "LittleFS\n"
    "  ARM Ltd / C. Haster - BSD-3\n\n"
    "PubSubClient (MQTT)\n"
    "  Nick O'Leary - MIT\n\n"
    "BearSSL\n"
    "  Thomas Pornin - MIT\n\n"
    "GNU FreeFont (FreeSans)\n"
    "  GNU Project - GPL-3 + Font Exception\n\n"
    "OneWirePIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "DHT22PIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "BuzzerPIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "SIMUT v3 - Hergestellt in Brasilien";

static const char LICENSE_IT[] =
    "Licenza MIT\n\n"
    "Copyright (c) 2026 Angelo Moises Alves\n\n"
    "Con la presente si concede il permesso, gratuitamente, a chiunque ottenga una "
    "copia di questo software e dei file di documentazione associati (il \"Software\"), "
    "di trattare il Software senza restrizioni, inclusi senza limitazione i diritti di "
    "utilizzare, copiare, modificare, unire, pubblicare, distribuire, sublicenziare "
    "e/o vendere copie del Software, e di consentire alle persone a cui il Software e "
    "fornito di farlo, alle seguenti condizioni:\n\n"
    "L'avviso di copyright sopra riportato e questo avviso di permesso devono essere "
    "inclusi in tutte le copie o parti sostanziali del Software.\n\n"
    "IL SOFTWARE E FORNITO \"COSI COM'E\", SENZA GARANZIA DI ALCUN TIPO, ESPRESSA O "
    "IMPLICITA, INCLUSE MA NON LIMITATE ALLE GARANZIE DI COMMERCIABILITA, IDONEITA PER "
    "UNO SCOPO PARTICOLARE E NON VIOLAZIONE. IN NESSUN CASO GLI AUTORI O I DETENTORI "
    "DEL COPYRIGHT SARANNO RESPONSABILI PER QUALSIASI RECLAMO, DANNO O ALTRA "
    "RESPONSABILITA, SIA IN UN'AZIONE CONTRATTUALE, ILLECITO O ALTRO, DERIVANTE DA, O "
    "IN CONNESSIONE CON IL SOFTWARE O L'USO O ALTRE OPERAZIONI NEL SOFTWARE.\n\n"
    "--- Ringraziamenti ---\n\n"
    "Arduino-Pico Core\n"
    "  Earle F. Philhower III - LGPL-2.1\n\n"
    "Raspberry Pi Pico SDK\n"
    "  Raspberry Pi Ltd - BSD-3\n\n"
    "Adafruit GFX Library\n"
    "  Adafruit Industries - BSD-2\n\n"
    "Adafruit ILI9341\n"
    "  Adafruit Industries - BSD-2\n\n"
    "XPT2046 Touchscreen\n"
    "  Paul Stoffregen - MIT\n\n"
    "LittleFS\n"
    "  ARM Ltd / C. Haster - BSD-3\n\n"
    "PubSubClient (MQTT)\n"
    "  Nick O'Leary - MIT\n\n"
    "BearSSL\n"
    "  Thomas Pornin - MIT\n\n"
    "GNU FreeFont (FreeSans)\n"
    "  GNU Project - GPL-3 + Font Exception\n\n"
    "OneWirePIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "DHT22PIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "BuzzerPIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "SIMUT v3 - Fatto in Brasile";

static const char LICENSE_RU[] =
    "Licenziya MIT\n\n"
    "Avtorskiye prava (c) 2026 Angelo Moises Alves\n\n"
    "Nastoyashchim predostavlyayetsya razresheniye, besplatno, lyubomu litsu, "
    "poluchivshemu kopiyu dannogo programmnogo obespecheniya i svyazannykh s nim "
    "faylov dokumentatsii (\"Programmnoe Obespechenie\"), ispol'zovat' Programmnoe "
    "Obespechenie bez ogranicheniy, vklyuchaya, no ne ogranichivayas' pravami na "
    "ispol'zovaniye, kopirovaniye, izmeneniye, ob\"yedineniye, publikatsiyu, "
    "rasprostranenie, sublitsenzirovaniye i/ili prodazhu kopiy Programmnogo "
    "Obespecheniya, a takzhe razreshat' litsam, kotorym predostavlyayetsya "
    "Programmnoe Obespechenie, delat' eto pri soblyudenii sleduyushchikh usloviy:\n\n"
    "Ukazannoye vyshe uvedomleniye ob avtorskikh pravakh i dannoye uvedomleniye o "
    "razreshenii dolzhny byt' vklyucheny vo vse kopii ili sushchestvennyye chasti "
    "Programmnogo Obespecheniya.\n\n"
    "PROGRAMMNOYE OBESPECHENIYE PREDOSTAVLYAYETSYA \"KAK YEST'\", BEZ KAKIKH-LIBO "
    "GARANTIY, YAVNYKH ILI PODRAZUMEVAYEMYKH, VKLYUCHAYA, NO NE OGRANICHIVAYAS' "
    "GARANTIYAMI TOVARNOY PRIGODNOSTI, PRIGODNOSTI DLYA OPREDELYONNOY TSELI I "
    "NENARUSHENIYA. NI V KOYEM SLUCHAYE AVTORY ILI PRAVOOOBLADATELI NE NESUT "
    "OTVETSTVENNOSTI ZA LYUBYYE PRETENZII, USHCHERB ILI INUYU OTVETSTVENNOST', BUDTO "
    "V DEYSTVII KONTRAKTA, DELIKTA ILI INOGO, VOZNIKAYUSHCHUYU IZ ILI V SVYAZI S "
    "PROGRAMMNYM OBESPECHENIYEM ILI ISPOL'ZOVANIYEM ILI INYMI DEYSTVIYAMI S "
    "PROGRAMMNYM OBESPECHENIYEM.\n\n"
    "--- Blagodarnosti ---\n\n"
    "Arduino-Pico Core\n"
    "  Earle F. Philhower III - LGPL-2.1\n\n"
    "Raspberry Pi Pico SDK\n"
    "  Raspberry Pi Ltd - BSD-3\n\n"
    "Adafruit GFX Library\n"
    "  Adafruit Industries - BSD-2\n\n"
    "Adafruit ILI9341\n"
    "  Adafruit Industries - BSD-2\n\n"
    "XPT2046 Touchscreen\n"
    "  Paul Stoffregen - MIT\n\n"
    "LittleFS\n"
    "  ARM Ltd / C. Haster - BSD-3\n\n"
    "PubSubClient (MQTT)\n"
    "  Nick O'Leary - MIT\n\n"
    "BearSSL\n"
    "  Thomas Pornin - MIT\n\n"
    "GNU FreeFont (FreeSans)\n"
    "  GNU Project - GPL-3 + Font Exception\n\n"
    "OneWirePIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "DHT22PIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "BuzzerPIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "SIMUT v3 - Sdelano v Brazilii";

static const char LICENSE_ZH[] =
    "MIT Xukezheng\n\n"
    "Banquan Suoyou (c) 2026 Angelo Moises Alves\n\n"
    "Tezi mianfei shouquan renhe huode ben ruanjian ji xiangguan wendang wenjian "
    "(\"Ruanjian\") fuben de ren, zai wu renhe xianzhi de qingkuang xia chuli gai "
    "Ruanjian, baokuo dan bu xianzhi yu shiyong, fuben, xiugai, hebing, chuban, "
    "fenfa, zai xukezheng he/huo xiaoshou gai Ruanjian de fuben de quanli, yi ji "
    "yunxu xiang qi tigong gai Ruanjian de ren zheyang zuo, xu fuhe yixia tiaojian:\n\n"
    "Shangshu banquan shengming he ben xukezheng shengming ying baohanzai gai "
    "Ruanjian de suoyou fuben huo zhuyao bufen zhong.\n\n"
    "GAI RUANJIAN AN \"YUANYANG\" TIGONG, BU FU RENHE MINGSHI HUO ANSHI DE BAOZHENG, "
    "BAOKUO DAN BU XIANZHI YU DUI SHIXIAOXING, TEDING YONGTU DE SHIYONGXING HE BU "
    "QINQUAN DE BAOZHENG. ZUOZHE HUO BANQUAN CHIYOU REN ZAI RENHE QINGKUANG XIA "
    "JUN BU DUI RENHE SUOPEI, SUNHAI HUO QITA ZEREN FUZE, WULUN SHI HETONG XINGWEI, "
    "QINQUAN XINGWEI HAISHI QITA, YIN GAI RUANJIAN HUO GAI RUANJIAN DE SHIYONG HUO "
    "QITA JIAOYÌ ER CHANSHENG.\n\n"
    "--- Zhixie ---\n\n"
    "Arduino-Pico Core\n"
    "  Earle F. Philhower III - LGPL-2.1\n\n"
    "Raspberry Pi Pico SDK\n"
    "  Raspberry Pi Ltd - BSD-3\n\n"
    "Adafruit GFX Library\n"
    "  Adafruit Industries - BSD-2\n\n"
    "Adafruit ILI9341\n"
    "  Adafruit Industries - BSD-2\n\n"
    "XPT2046 Touchscreen\n"
    "  Paul Stoffregen - MIT\n\n"
    "LittleFS\n"
    "  ARM Ltd / C. Haster - BSD-3\n\n"
    "PubSubClient (MQTT)\n"
    "  Nick O'Leary - MIT\n\n"
    "BearSSL\n"
    "  Thomas Pornin - MIT\n\n"
    "GNU FreeFont (FreeSans)\n"
    "  GNU Project - GPL-3 + Font Exception\n\n"
    "OneWirePIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "DHT22PIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "BuzzerPIO RP2040\n"
    "  Angelo M. Alves - MIT\n\n"
    "SIMUT v3 - Zhizao yu Baxi";

static const char* const LICENSE_TEXT[TOTAL_LANGS] = {
    LICENSE_EN, LICENSE_PT, LICENSE_ES, LICENSE_FR,
    LICENSE_DE, LICENSE_IT, LICENSE_RU, LICENSE_ZH
};

static int wrapLineCount(const char* text, int maxCols) {
    int lines = 1;
    int col = 0;
    while (*text) {
        if (*text == '\n') { lines++; col = 0; text++; continue; }
        if (*text == ' ')  { if (col > 0 && col < maxCols) col++; text++; continue; }

        int wlen = 0;
        const char* w = text;
        while (*w && *w != ' ' && *w != '\n') { wlen++; w++; }

        if (col > 0 && col + wlen > maxCols) { lines++; col = 0; }
        col += wlen;
        text += wlen;
    }
    return lines;
}


static void renderWrapped(Adafruit_ILI9341* tft, const char* text,
                          int x0, int y0, int maxCols, int lineH,
                          int skip, int maxVis) {
    int curLine = 0;
    int col = 0;
    while (*text) {
        if (curLine >= skip + maxVis) break;
        if (*text == '\n') { curLine++; col = 0; text++; continue; }
        if (*text == ' ')  { if (col > 0 && col < maxCols) col++; text++; continue; }

        char word[52];
        int wlen = 0;
        while (*text && *text != ' ' && *text != '\n' && wlen < 50) {
            word[wlen++] = *text++;
        }
        word[wlen] = '\0';

        if (col > 0 && col + wlen > maxCols) { curLine++; col = 0; }
        if (curLine >= skip + maxVis) break;

        if (curLine >= skip) {
            int sy = y0 + (curLine - skip) * lineH;
            tft->setCursor(x0 + col * 6, sy);
            tft->print(word);
        }
        col += wlen;
    }
}

static DisplayManager* _instance = nullptr;


constexpr int16_t DisplayManager::CAL_SCR_X[4];
constexpr int16_t DisplayManager::CAL_SCR_Y[4];

DisplayManager::DisplayManager() {
    _instance = this;
    mutex_init(&_stateMutex);
    queue_init(&_eventQueue, sizeof(UiEvent), 10);
    _sharedState.ambientTemp = NAN;
    _sharedState.ambientHum = NAN;
    _sharedState.ambientValid = true;
    _sharedState.slotTemp = NAN;
    _sharedState.slotValid = false;
    _sharedState.selectedSlotIdx = 0;
    _sharedState.wifiRssi = -100;
    _sharedState.btActive = false;
    _sharedState.isBooting = true;
    _sharedState.showSkipButton = false;
    _sharedState.apProgressPct = -1;
    for(int i = 0; i < 5; i++) strcpy(_sharedState.bootLogs[i], "");
    strcpy(_sharedState.timeString, "--/-- --:--");
    strcpy(_sharedState.slotName, "Sensor 1");
    _lastRenderedState.isBooting = false;
    _lastRenderedState.apProgressPct = -2;
    _lastRenderedState.selectedSlotIdx = -1;
    _isDirty = true;
    _currentPage = 0;
    _lastTouchTime = 0;
    _btnHoldStartTime = 0;
    _lastPressedBtn = -1;
    _menuSelection = 0;
    _isPausedForFlash = false;
    _lastHeartbeat = millis();
    _uiMode = MODE_DASHBOARD;
    _webBusyUser[0] = '\0';
    _repaintGraph = false;
    _repaintLoading = false;
    _loadingDrawn = false;
    _themeChanged = false;
    _forceFullRedraw = false;
    _rawTouchState = false;
    _skipPressed = false;
}

void DisplayManager::begin() {}

void DisplayManager::startCore1() { multicore_launch_core1(core1Entry); }

void DisplayManager::restartCore1() {
    multicore_reset_core1();
    delay(50);
    mutex_init(&_stateMutex);
    _isPausedForFlash = false;
    _lastHeartbeat = millis();
    multicore_launch_core1(core1Entry);
}

void DisplayManager::setLanguage(int langId) {
    if (langId >= 0 && langId < TOTAL_LANGS) _currentLangIdx = langId;
    else _currentLangIdx = 1;
}

const char* DisplayManager::tr(LangKey key) { return DICTIONARY[_currentLangIdx][key]; }


/**
 * @brief Trunca um texto para caber em maxPixelW pixels na fonte atual do GFX.
 *
 * If the original text fits, it is copied entirely to out.
 * Otherwise, removes trailing characters and appends "..." de modo
 * the result fits within the maximum width. The font must already be set
 * no contexto GFX antes da chamada.
 */
void DisplayManager::truncateText(Adafruit_GFX* gfx, const char* src,
                                  char* out, size_t outSize, int16_t maxPixelW) {
    if (!gfx || !src || !out || outSize < 4) {
        if (out && outSize > 0) out[0] = '\0';
        return;
    }

    /* Medir largura do texto original */
    int16_t bx, by;
    uint16_t tw, th;
    gfx->getTextBounds(src, 0, 0, &bx, &by, &tw, &th);

    /* Se cabe inteiro, copia e retorna */
    if ((int16_t)tw <= maxPixelW) {
        strncpy(out, src, outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }

    /* Measure ellipsis width */
    uint16_t ellW, ellH;
    gfx->getTextBounds("...", 0, 0, &bx, &by, &ellW, &ellH);
    int16_t targetW = maxPixelW - (int16_t)ellW;
    if (targetW < 0) targetW = 0;

    /* Binary search for maximum fitting length */
    int srcLen = (int)strlen(src);
    int lo = 0, hi = srcLen;
    int best = 0;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;

        /* Monta string candidata no buffer */
        int copyLen = mid;
        if (copyLen > (int)(outSize - 4)) copyLen = (int)(outSize - 4);
        memcpy(out, src, copyLen);
        out[copyLen] = '\0';

        gfx->getTextBounds(out, 0, 0, &bx, &by, &tw, &th);

        if ((int16_t)tw <= targetW) {
            best = copyLen;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    /* Remove trailing spaces before ellipsis */
    while (best > 0 && out[best - 1] == ' ') best--;

    /* Monta resultado final */
    memcpy(out, src, best);
    out[best]     = '.';
    out[best + 1] = '.';
    out[best + 2] = '.';
    out[best + 3] = '\0';
}

bool DisplayManager::isMenuActive() {
    mutex_enter_blocking(&_stateMutex);
    bool active = (_uiMode >= MODE_AUTH);
    mutex_exit(&_stateMutex);
    return active;
}


bool DisplayManager::isDisplayBusy() {
    mutex_enter_blocking(&_stateMutex);
    bool busy = (_uiMode != MODE_DASHBOARD);
    mutex_exit(&_stateMutex);
    return busy;
}


bool DisplayManager::isHeavyRendering() {
    mutex_enter_blocking(&_stateMutex);
    bool heavy = (_uiMode == MODE_GRAPH_LOADING || _uiMode == MODE_GRAPH_VIEW);
    mutex_exit(&_stateMutex);
    return heavy;
}

void DisplayManager::pauseRendering(bool pause) {

    if (!_core1Ready) return;
    if (pause) {

        int32_t prev = __atomic_fetch_add(&_pauseRefCount, 1, __ATOMIC_ACQ_REL);
        if (prev == 0) {
            _pauseStartTime = millis();
            LogManager::instance().setCorePaused(1, true);
            multicore_lockout_start_blocking();
        }
    } else {
        int32_t prev = __atomic_fetch_sub(&_pauseRefCount, 1, __ATOMIC_ACQ_REL);
        if (prev <= 1) {

            __atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
            _pauseStartTime = 0;
            multicore_lockout_end_blocking();
            LogManager::instance().setCorePaused(1, false);
        }
    }
}


void DisplayManager::forceUnpause() {
    int32_t prev = __atomic_load_n(&_pauseRefCount, __ATOMIC_ACQUIRE);
    if (prev > 0) {
        LOG_ERR("DSP", "forceUnpause: refCount was " + String(prev));
        __atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
        _pauseStartTime = 0;
        multicore_lockout_end_blocking();
        LogManager::instance().setCorePaused(1, false);
    }
}

uint32_t DisplayManager::getHeartbeat() {
    if (_isPausedForFlash) return millis();
    return _lastHeartbeat;
}

void DisplayManager::refreshTheme() { _themeChanged = true; }


void DisplayManager::showStats(const GraphDataPackage& data, float minHum, float maxHum) {
    mutex_enter_blocking(&_stateMutex);
    _graphData = data; _currentMinHum = minHum; _currentMaxHum = maxHum;
    _uiMode = MODE_STATS_VIEW;
    __dmb();
    _repaintGraph = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::showGraphPlot(const GraphDataPackage& data, float minHum, float maxHum) {
    mutex_enter_blocking(&_stateMutex);
    _graphData = data; _currentMinHum = minHum; _currentMaxHum = maxHum;
    _uiMode = MODE_GRAPH_VIEW;
    __dmb();
    _repaintGraph = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setBootStatus(String msg, bool showSkip) {
    mutex_enter_blocking(&_stateMutex);
    if (msg.length() > 0) {
        for (int i = 0; i < 4; i++) strcpy(_sharedState.bootLogs[i], _sharedState.bootLogs[i+1]);
        strncpy(_sharedState.bootLogs[4], msg.c_str(), 39); _sharedState.bootLogs[4][39] = '\0';
    }
    _sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::replaceBootStatus(String msg, bool showSkip) {
    mutex_enter_blocking(&_stateMutex);
    if (msg.length() > 0) {
        strncpy(_sharedState.bootLogs[4], msg.c_str(), 39); _sharedState.bootLogs[4][39] = '\0';
    }
    _sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setApProgress(int pct) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.apProgressPct = pct; _sharedState.isBooting = true; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::endBoot() {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.isBooting = false; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::forceDashboard() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true;
    mutex_exit(&_stateMutex);
}

bool DisplayManager::isSkipPressed() {
    if (_skipPressed) { _skipPressed = false; return true; }
    return false;
}

bool DisplayManager::isScreenTouched() { return _rawTouchState; }


void DisplayManager::setWebBusy(bool busy, const char* username) {
    mutex_enter_blocking(&_stateMutex);
    if (busy) {
        if (username) strncpy(_webBusyUser, username, 23);
        else strncpy(_webBusyUser, "web", 23);
        _webBusyUser[23] = '\0';
        _webBusy = true;
    } else {
        _webBusy = false;
    }
    mutex_exit(&_stateMutex);
}

void DisplayManager::setAmbientData(float t, float h, bool isValid) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.ambientTemp = t; _sharedState.ambientHum = h; _sharedState.ambientValid = isValid; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setAmbientMinMax(float minT, float maxT, float minH, float maxH) {
    _ambMinTemp = minT;
    _ambMaxTemp = maxT;
    _ambMinHum  = minH;
    _ambMaxHum  = maxH;
}

void DisplayManager::setSlotData(float t, bool isValid, int slotIdx, String name) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.slotTemp = t; _sharedState.slotValid = isValid; _sharedState.selectedSlotIdx = slotIdx;
    strncpy(_sharedState.slotName, name.c_str(), 31); _sharedState.slotName[31] = '\0'; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setSlotMinMax(float minT, float maxT) {
    _slotMinTemp = minT;
    _slotMaxTemp = maxT;
}

void DisplayManager::setSystemStatus(int rssi, bool bt, String timeStr) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.wifiRssi = rssi; _sharedState.btActive = bt;
    strncpy(_sharedState.timeString, timeStr.c_str(), 23); _sharedState.timeString[23] = '\0'; _isDirty = true;
    mutex_exit(&_stateMutex);
}


void DisplayManager::setAlarmState(uint16_t slotMask, int8_t navSlot,
                                   bool ambTemp, bool ambHum) {
    _alarmSlotMask    = slotMask;
    _alarmAmbientTemp = ambTemp;
    _alarmAmbientHum  = ambHum;
    if (navSlot >= 0) _alarmNavPending = navSlot;
}


void DisplayManager::setAlarmSilenced(bool silenced, uint32_t endTime) {
    _alarmSilenced   = silenced;
    _alarmSilenceEnd = endTime;
}


void DisplayManager::setAlarmDeactivated(bool deactivated) {
    _alarmDeactivated = deactivated;
}


void DisplayManager::showAlarmAction(int8_t slotIdx) {
    mutex_enter_blocking(&_stateMutex);
    _alarmActionSlot = slotIdx;
    _uiMode = MODE_ALARM_ACTION;
    _forceSettingsRedraw = true;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}


void DisplayManager::drawAlarmAction() {
    if (!_canvasWide) return;

    if (!_forceSettingsRedraw) return;
    _forceSettingsRedraw = false;

    _tft->fillScreen(C_BG_MAIN);


    _tft->fillRect(0, 0, 320, 48, RGB565(180, 30, 30));
    _tft->setFont(&FreeSansBold12pt7b);
    _tft->setTextColor(RGB565(255, 255, 255));

    char headerBuf[40];
    if (_alarmActionSlot < 0) {
        snprintf(headerBuf, sizeof(headerBuf), "! %s", tr(TR_AMBIENT));
    } else {
        /* Use sensor friendly name (from sharedState) */
        mutex_enter_blocking(&_stateMutex);
        char friendlyName[32];
        strncpy(friendlyName, _sharedState.slotName, 31);
        friendlyName[31] = '\0';
        mutex_exit(&_stateMutex);
        if (strlen(friendlyName) > 0) {
            snprintf(headerBuf, sizeof(headerBuf), "! %s", friendlyName);
        } else {
            snprintf(headerBuf, sizeof(headerBuf), "! Sensor %d", _alarmActionSlot);
        }
    }
    int16_t bx, by; uint16_t bw, bh;
    _tft->getTextBounds(headerBuf, 0, 0, &bx, &by, &bw, &bh);
    _tft->setCursor((320 - bw) / 2, 32);
    _tft->print(headerBuf);


    int btnX = 20, btnW = 280, btnH = 45, btnR = 10;
    _tft->fillRoundRect(btnX, 60, btnW, btnH, btnR, RGB565(200, 100, 0));
    _tft->setFont(&FreeSansBold12pt7b);
    _tft->setTextColor(RGB565(255, 255, 255));
    String silTxt = tr(TR_SILENCE_120S);
    _tft->getTextBounds(silTxt, 0, 0, &bx, &by, &bw, &bh);
    _tft->setCursor(btnX + (btnW - bw) / 2, 60 + 30);
    _tft->print(silTxt);


    _tft->fillRoundRect(btnX, 115, btnW, btnH, btnR, RGB565(180, 30, 30));
    String deactTxt = tr(TR_DEACTIVATE);
    _tft->getTextBounds(deactTxt, 0, 0, &bx, &by, &bw, &bh);
    _tft->setCursor(btnX + (btnW - bw) / 2, 115 + 30);
    _tft->print(deactTxt);


    _tft->fillRoundRect(btnX, 170, btnW, btnH, btnR, C_ACCENT);
    _tft->setTextColor(C_BG_MAIN);
    String mmTxt = tr(TR_MINMAX);
    _tft->getTextBounds(mmTxt, 0, 0, &bx, &by, &bw, &bh);
    _tft->setCursor(btnX + (btnW - bw) / 2, 170 + 30);
    _tft->print(mmTxt);
}

bool DisplayManager::isSlotAlarming(int slotIdx) const {
    return (slotIdx >= 0 && slotIdx < 16) && (_alarmSlotMask & (1 << slotIdx));
}

uint16_t DisplayManager::slotAlarmBg(int slotIdx) const {
    if (!isSlotAlarming(slotIdx)) return C_CARD_BG;

    if (_alarmSilenced) return C_CARD_BG;
    return _alarmFlashPhase ? RGB565(180, 30, 30) : C_CARD_BG;
}

bool DisplayManager::isAnyAlarmActive() const {
    return (_alarmSlotMask != 0) || _alarmAmbientTemp || _alarmAmbientHum;
}


void DisplayManager::fixCardCorners(int16_t x, int16_t y, int16_t w,
                                    int16_t h, int16_t r,
                                    uint16_t borderColor) {
    if (!_tft) return;
    for (int16_t i = 0; i < r; i++) {
        int16_t span = (int16_t)(sqrtf(2.0f * r * i - (float)(i * i)) + 0.5f);
        int16_t gap  = r - span;
        if (gap <= 0) continue;
        _tft->drawFastHLine(x,           y + i,         gap, C_BG_MAIN);
        _tft->drawFastHLine(x + w - gap, y + i,         gap, C_BG_MAIN);
        _tft->drawFastHLine(x,           y + h - 1 - i, gap, C_BG_MAIN);
        _tft->drawFastHLine(x + w - gap, y + h - 1 - i, gap, C_BG_MAIN);
    }
    _tft->drawRoundRect(x, y, w, h, r, borderColor);
}


void DisplayManager::maskStripCorners(GFXcanvas16* canvas,
                                      int16_t stripRow, int16_t stripH,
                                      int16_t cardW, int16_t cardH,
                                      int16_t r, uint16_t bgColor,
                                      uint16_t borderColor) {
    if (!canvas || r <= 0) return;
    uint16_t* buf    = canvas->getBuffer();
    int16_t   stride = canvas->width();


    constexpr int16_t MAX_R = 24;
    int16_t borderMin[MAX_R], borderMax[MAX_R];
    int16_t rr = (r > MAX_R) ? MAX_R : r;

    for (int16_t i = 0; i < rr; i++) { borderMin[i] = rr; borderMax[i] = -1; }

    {

        int16_t f     = 1 - rr;
        int16_t ddF_x = 1;
        int16_t ddF_y = -2 * rr;
        int16_t cx    = 0;
        int16_t cy    = rr;

        while (cx < cy) {
            if (f >= 0) { cy--; ddF_y += 2; f += ddF_y; }
            cx++; ddF_x += 2; f += ddF_x;


            int16_t row1 = rr - cx, col1 = rr - cy;
            int16_t row2 = rr - cy, col2 = rr - cx;

            if (row1 >= 0 && row1 < rr) {
                if (col1 < borderMin[row1]) borderMin[row1] = col1;
                if (col1 > borderMax[row1]) borderMax[row1] = col1;
            }
            if (row2 >= 0 && row2 < rr) {
                if (col2 < borderMin[row2]) borderMin[row2] = col2;
                if (col2 > borderMax[row2]) borderMax[row2] = col2;
            }
        }
    }


    for (int16_t row = 0; row < stripH; row++) {
        int16_t   cardY  = stripRow + row;
        uint16_t* rowPtr = buf + (row * stride);


        int16_t bMin = -1, bMax = -1;

        if (cardY < rr) {
            bMin = borderMin[cardY];
            bMax = borderMax[cardY];
        } else if (cardY >= cardH - rr) {
            int16_t mirror = cardH - 1 - cardY;
            bMin = borderMin[mirror];
            bMax = borderMax[mirror];
        }

        if (cardY == 0 || cardY == cardH - 1) {


            for (int16_t x = 0; x < bMin; x++)
                rowPtr[x] = bgColor;
            for (int16_t x = bMin; x < cardW - bMin; x++)
                rowPtr[x] = borderColor;
            for (int16_t x = cardW - bMin; x < cardW; x++)
                rowPtr[x] = bgColor;

        } else if (bMin >= 0) {


            for (int16_t x = 0; x < bMin; x++)
                rowPtr[x] = bgColor;
            for (int16_t x = bMin; x <= bMax; x++)
                rowPtr[x] = borderColor;

            int16_t rBMax = cardW - 1 - bMin;
            int16_t rBMin = cardW - 1 - bMax;
            for (int16_t x = rBMin; x <= rBMax; x++)
                rowPtr[x] = borderColor;
            for (int16_t x = cardW - bMin; x < cardW; x++)
                rowPtr[x] = bgColor;

        } else {

            rowPtr[0]          = borderColor;
            rowPtr[cardW - 1]  = borderColor;
        }
    }
}


void DisplayManager::redrawAlarmFlash() {
    if (!_tft || !_canvasSmall || !_canvasWide) return;

    if (_alarmAmbientTemp || _alarmAmbientHum) {
        drawAmbientPanel(_lastRenderedState.ambientTemp,
                         _lastRenderedState.ambientHum,
                         _lastRenderedState.ambientValid);
    }

    int sel = _lastRenderedState.selectedSlotIdx;
    if (isSlotAlarming(sel)) {
        drawSlotPanel(_lastRenderedState.slotTemp, _lastRenderedState.slotValid,
                      sel, _lastRenderedState.slotName, true);
    }


    bool pageHasAlarm = false;
    bool otherPageHasAlarm = false;
    for (int i = 0; i < 10; i++) {
        if (!isSlotAlarming(i)) continue;
        int slotPage = (i < 4) ? 0 : (i < 8) ? 1 : 2;
        if (slotPage == _currentPage) pageHasAlarm = true;
        else                          otherPageHasAlarm = true;
    }
    if (pageHasAlarm || otherPageHasAlarm) {
        drawBottomButtons(sel, true);
    }
}


void DisplayManager::restoreNormalDashboard() {
    if (!_tft || !_canvasSmall || !_canvasWide) return;
    drawAmbientPanel(_lastRenderedState.ambientTemp,
                     _lastRenderedState.ambientHum,
                     _lastRenderedState.ambientValid);
    drawSlotPanel(_lastRenderedState.slotTemp, _lastRenderedState.slotValid,
                  _lastRenderedState.selectedSlotIdx,
                  _lastRenderedState.slotName, true);
    drawBottomButtons(_lastRenderedState.selectedSlotIdx, true);
}

bool DisplayManager::getUiEvent(UiEvent& ev) { return queue_try_remove(&_eventQueue, &ev); }
void DisplayManager::core1Entry() { if (_instance) _instance->loopCore1(); }

void DisplayManager::loopCore1() {

    multicore_lockout_victim_init();
    _core1Ready = true;

    if (!_tft) _tft = new Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
    if (!_ts) _ts = new XPT2046_Touchscreen(TOUCH_CS, TOUCH_IRQ);
    _tft->begin(); _tft->setRotation(3); _tft->fillScreen(C_BG_MAIN);
    _ts->begin(); _ts->setRotation(3);

    if (!_canvasWide) _canvasWide = new GFXcanvas16(320, 45);
    if (!_canvasSmall) _canvasSmall = new GFXcanvas16(140, 40);

    SystemState currentSnapshot;
    if (!_sharedState.isBooting) drawInterfaceFixed();
    _lastRenderedState.selectedSlotIdx = -1;

    while (true) {
        TRACE_MOD(1, MOD_DISPLAY);
        TRACE_BEAT(1);

        _lastHeartbeat = millis();
        _rawTouchState = _ts->touched();

        if (_themeChanged) {
            SystemState snap;
            mutex_enter_blocking(&_stateMutex);
            snap = _sharedState;
            mutex_exit(&_stateMutex);

            if (!snap.isBooting) {
                _tft->fillScreen(C_BG_MAIN);
                _tft->setFont(&FreeSansBold12pt7b);
                _tft->setTextColor(C_TEXT_MAIN);
                int16_t x1, y1; uint16_t w, h;
                String msg = tr(TR_APPLYING_THEME);
                _tft->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
                _tft->setCursor(160 - (w/2), 127);
                _tft->print(msg);
                delay(400);

                mutex_enter_blocking(&_stateMutex);
                snap = _sharedState;
                _isDirty = false;
                mutex_exit(&_stateMutex);

                drawInterfaceFixed();
                drawTopBar(snap);
                drawAmbientPanel(snap.ambientTemp, snap.ambientHum, snap.ambientValid);
                drawSlotPanel(snap.slotTemp, snap.slotValid, snap.selectedSlotIdx, snap.slotName, true);
                drawBottomButtons(snap.selectedSlotIdx, true);
                _lastRenderedState = snap;
                _uiMode = MODE_DASHBOARD;
            } else {
                _tft->fillScreen(C_BG_MAIN);
                _lastRenderedState.isBooting = false;

                mutex_enter_blocking(&_stateMutex);
                _isDirty = true;
                mutex_exit(&_stateMutex);
            }
            _themeChanged = false;
        }

        if (_uiMode == MODE_DASHBOARD) {

            bool webBusyNow = false;
            if (mutex_try_enter(&_stateMutex, NULL)) {
                webBusyNow = _webBusy;
                mutex_exit(&_stateMutex);
            }


            if (_alarmNavPending >= 0) {
                int8_t navTarget = _alarmNavPending;
                _alarmNavPending = -1;
                if      (navTarget < 4) _currentPage = 0;
                else if (navTarget < 8) _currentPage = 1;
                else                    _currentPage = 2;
                _alarmRotateTimer = millis();
                mutex_enter_blocking(&_stateMutex);
                _isDirty = true;
                mutex_exit(&_stateMutex);
            }


            if (_alarmSlotMask != 0 && !_alarmSilenced) {
                uint16_t m = _alarmSlotMask;
                int alarmCount = 0;
                while (m) { alarmCount += (m & 1); m >>= 1; }

                if (alarmCount >= 2 && (millis() - _alarmRotateTimer >= 3000)) {
                    _alarmRotateTimer = millis();
                    int current = _lastRenderedState.selectedSlotIdx;
                    for (int i = 1; i <= 10; i++) {
                        int idx = (current + i) % 10;
                        if (_alarmSlotMask & (1 << idx)) {
                            if      (idx < 4) _currentPage = 0;
                            else if (idx < 8) _currentPage = 1;
                            else              _currentPage = 2;
                            UiEvent ev;
                            ev.type = UiEvent::EVT_SLOT_SELECT;
                            ev.id   = idx;
                            queue_try_add(&_eventQueue, &ev);
                            break;
                        }
                    }
                }
            }


            if (isAnyAlarmActive()) {
                uint32_t now = millis();
                if (now - _alarmFlashTimer >= 600) {
                    _alarmFlashTimer = now;
                    _alarmFlashPhase = !_alarmFlashPhase;
                    if (!_webOverlayShown) {
                        redrawAlarmFlash();
                    }
                }
            } else if (_alarmFlashPhase) {

                _alarmFlashPhase  = false;
                _alarmFlashTimer  = 0;
                _alarmRotateTimer = 0;
                if (!_webOverlayShown) {
                    restoreNormalDashboard();
                }
            }


            if (_webOverlayShown) {
                if (!webBusyNow) {
                    _webOverlayShown = false;
                    _forceFullRedraw = true;
                    _isDirty = true;

                    if (pullSnapshot(currentSnapshot)) render(currentSnapshot);
                }

            } else {
                if (pullSnapshot(currentSnapshot)) render(currentSnapshot);
            }
        }
        else if (_uiMode == MODE_GRAPH_LOADING) {
            if (_repaintLoading) { drawLoadingScreen(); _repaintLoading = false; }
        }
        else if (_uiMode == MODE_STATS_VIEW) {
            if (_repaintGraph) { drawStatsScreen(); _repaintGraph = false; }
        }
        else if (_uiMode == MODE_GRAPH_VIEW) {
            if (_repaintGraph) { drawGraphScreen(); _repaintGraph = false; }
        }
        else if (_uiMode == MODE_SETTINGS_THEMES) {
            if (_repaintSettings) { drawSettingsThemes(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_ALARMS) {
            if (_repaintSettings) { drawSettingsAlarms(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_ALARM_EDIT) {
            if (_repaintSettings) { drawAlarmEdit(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_AUTH) {
            if (_permanentLockout) {
                if (millis() > _lockoutUntil) forceDashboard();
            } else if (_lockoutUntil > 0) {
                if (millis() < _lockoutUntil) _repaintSettings = true;
                else { _lockoutUntil = 0; _forceSettingsRedraw = true; _repaintSettings = true; }
            }
            if (_repaintSettings) { drawAuthScreen(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_MAIN) {
            if (_repaintSettings) { drawSettingsMain(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_LANG) {
            if (_repaintSettings) { drawSettingsLang(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_PASSWORD) {
            if (_repaintSettings) { drawSettingsPassword(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_TOUCH_CAL) {
            if (_repaintSettings) { drawTouchCalibration(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_SOUNDS) {

            if (_repaintSettings) {
                if (_inMelodySelect) drawMelodySelect();
                else                 drawSettingsSounds();
                _repaintSettings = false;
            }
        }
        else if (_uiMode == MODE_SETTINGS_LICENSE) {
            if (_repaintSettings) { drawSettingsLicense(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_ALARM_ACTION) {

            if (_repaintSettings) { drawAlarmAction(); _repaintSettings = false; }
        }
        handleTouch();
        delay(5);
    }
}

bool DisplayManager::pullSnapshot(SystemState& localSnapshot) {
    bool updated = false;


    if (mutex_enter_timeout_us(&_stateMutex, 1000)) {
        if (_isDirty) {
            localSnapshot = _sharedState;
            _isDirty = false;
            updated = true;
        }
        mutex_exit(&_stateMutex);
    }
    return updated;
}

void DisplayManager::render(const SystemState& state) {
    if (state.isBooting) {
        bool fullRedraw = (_lastRenderedState.isBooting == false) || (_lastRenderedState.apProgressPct != state.apProgressPct);
        if (state.apProgressPct >= 0) {
            if (fullRedraw) _tft->fillScreen(C_BG_MAIN);
            _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
            _tft->setCursor(55, 120); _tft->print("Configuration Mode");
            _tft->drawRoundRect(40, 140, 240, 20, 6, C_TEXT_SUB);
            int wBar = map(state.apProgressPct, 0, 100, 0, 236);
            if (wBar > 0) {
                _tft->fillRoundRect(42, 142, wBar, 16, 4, C_ACCENT);
            }
            _lastRenderedState = state;
            return;
        }

        int boxY = 105;

        if (fullRedraw) {
            _tft->fillScreen(C_BG_MAIN);
            _tft->setFont(&FreeSansBold24pt7b); _tft->setTextColor(C_TEXT_MAIN);
            int16_t x1, y1; uint16_t w, h;
            _tft->getTextBounds("SIMUT", 0, 0, &x1, &y1, &w, &h);
            _tft->setCursor((320 - w) / 2, 60); _tft->print("SIMUT");
            _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_ACCENT);
            _tft->getTextBounds(SIMUT_VERSION, 0, 0, &x1, &y1, &w, &h);
            _tft->setCursor((320 - w) / 2, 85); _tft->print(SIMUT_VERSION);

            _tft->fillRoundRect(10, boxY, 300, 80, 8, C_CARD_BG);
            _tft->drawRoundRect(10, boxY, 300, 80, 8, C_TEXT_OFF);
            _tft->setFont(NULL);
            _tft->setTextSize(1);
            _tft->setTextColor(C_ACCENT_HIGH, C_CARD_BG);
            _tft->setCursor(20, boxY + 8);
            _tft->print("> system_init()                       ");
        }
        _tft->setFont(NULL);
        _tft->setTextSize(1);
        _tft->setTextColor(C_TEXT_SUB, C_CARD_BG);
        for(int i=0; i<5; i++) {
            _tft->setCursor(20, boxY + 22 + (i*10));
            String logLine = state.bootLogs[i];
            while(logLine.length() < 46) logLine += " ";
            _tft->print(logLine);
        }

        if (state.showSkipButton) {
            _tft->fillRoundRect(80, 195, 160, 35, 8, C_ACCENT_HIGH);
            _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_BG_MAIN);
            int16_t x1, y1; uint16_t w, h;
            const char* skipLabel = tr(TR_SKIP);
            _tft->getTextBounds(skipLabel, 0, 0, &x1, &y1, &w, &h);
            _tft->setCursor(80 + (160 - w)/2, 218); _tft->print(skipLabel);
        } else if (fullRedraw) {
            _tft->fillRect(80, 195, 160, 35, C_BG_MAIN);
        }

        _lastRenderedState = state;
        return;
    }
    if (_lastRenderedState.isBooting && !state.isBooting) {


        _forceFullRedraw = true;
    }

    bool full = _forceFullRedraw;
    if (full) {
        drawInterfaceFixed();
        drawTopBar(state);


        drawAmbientPanel(state.ambientTemp, state.ambientHum, state.ambientValid);
        drawSlotPanel(state.slotTemp, state.slotValid, state.selectedSlotIdx, state.slotName, true);
        drawBottomButtons(state.selectedSlotIdx, true);
        _forceFullRedraw = false;
        _lastRenderedState = state;
        return;
    }

    if (state.wifiRssi != _lastRenderedState.wifiRssi ||
        state.btActive != _lastRenderedState.btActive ||
        strcmp(state.timeString, _lastRenderedState.timeString) != 0 ||
        _webNotifyStartMs > 0 ||
        _alarmSilenced ||
        _pktArrowState == 3) {
        drawTopBar(state);
    }

    if (!_ambientShowMinMax) {
        if (abs(state.ambientTemp - _lastRenderedState.ambientTemp) > 0.01 ||
            abs(state.ambientHum - _lastRenderedState.ambientHum) > 0.01 ||
            state.ambientValid != _lastRenderedState.ambientValid) {
            drawAmbientPanel(state.ambientTemp, state.ambientHum, state.ambientValid);
        }
    }

    /* Return panels to normal mode after 30s without touch */
    if ((_ambientShowMinMax || _slotShowMinMax) &&
        (millis() - _lastTouchTime > 30000)) {
        if (_ambientShowMinMax) {
            _ambientShowMinMax = false;
            drawAmbientPanel(state.ambientTemp, state.ambientHum, state.ambientValid);
        }
        if (_slotShowMinMax) {
            _slotShowMinMax = false;
            drawSlotPanel(state.slotTemp, state.slotValid,
                          state.selectedSlotIdx, state.slotName, true);
        }
    }

    bool slotChanged = (state.selectedSlotIdx != _lastRenderedState.selectedSlotIdx);
    bool nameChanged = (strcmp(state.slotName, _lastRenderedState.slotName) != 0);
    bool tempChanged = (abs(state.slotTemp - _lastRenderedState.slotTemp) > 0.01) || (state.slotValid != _lastRenderedState.slotValid);

    if (slotChanged || nameChanged || (!_slotShowMinMax && tempChanged)) {
        if (slotChanged) {
             drawBottomButtons(state.selectedSlotIdx, false);
        }

        drawSlotPanel(state.slotTemp, state.slotValid, state.selectedSlotIdx, state.slotName, (slotChanged || nameChanged));
    }

    /* Detect alarm state change and redraw buttons + panels */
    if (_alarmSlotMask != _prevAlarmSlotMask ||
        _alarmAmbientTemp != _prevAlarmAmbTemp ||
        _alarmAmbientHum  != _prevAlarmAmbHum) {
        drawBottomButtons(state.selectedSlotIdx, true);
        if (!_ambientShowMinMax) {
            drawAmbientPanel(state.ambientTemp, state.ambientHum, state.ambientValid);
        }
        if (!_slotShowMinMax) {
            drawSlotPanel(state.slotTemp, state.slotValid,
                          state.selectedSlotIdx, state.slotName, true);
        }
        _prevAlarmSlotMask = _alarmSlotMask;
        _prevAlarmAmbTemp  = _alarmAmbientTemp;
        _prevAlarmAmbHum   = _alarmAmbientHum;
    }

    _lastRenderedState = state;
}

void DisplayManager::drawInterfaceFixed() {


    _tft->fillScreen(C_BG_MAIN);
}

void DisplayManager::blitCanvas(GFXcanvas16* canvas, int16_t dstX, int16_t dstY, int16_t w, int16_t h) {
    if (!canvas || !_tft) return;
    int16_t cw = canvas->width();
    if (w == cw) {
        _tft->drawRGBBitmap(dstX, dstY, canvas->getBuffer(), w, h);
    } else {
        uint16_t* buf = canvas->getBuffer();
        for (int16_t row = 0; row < h; row++) {
            _tft->drawRGBBitmap(dstX, dstY + row, buf + (row * cw), w, 1);
        }
    }
}

void DisplayManager::drawTopBar(const SystemState& state) {
    if(!_canvasWide) return;
    const int W = 320, H = 29;
    _canvasWide->fillScreen(C_BG_MAIN);


    _canvasWide->setFont(&FreeSansBold9pt7b);
    _canvasWide->setTextSize(1);
    _canvasWide->setTextColor(C_ACCENT);
    _canvasWide->setCursor(3, 20);
    _canvasWide->print("SIMUT");


    bool showingSilence = false;
    if (_alarmSilenced && _alarmSilenceEnd > 0) {
        uint32_t now = millis();
        if (now < _alarmSilenceEnd) {
            showingSilence = true;
            uint32_t remaining = (_alarmSilenceEnd - now) / 1000;
            char silBuf[32];
            snprintf(silBuf, sizeof(silBuf), "%s: %lus", tr(TR_SILENCED), (unsigned long)remaining);
            _canvasWide->setFont(&FreeSansBold9pt7b);
            _canvasWide->setTextColor(RGB565(200, 100, 0));
            _canvasWide->setCursor(75, 20);
            _canvasWide->print(silBuf);
        }
    }


    bool showingNotify = false;
    if (!showingSilence && _webNotifyStartMs > 0) {
        uint32_t elapsed = millis() - _webNotifyStartMs;
        if (elapsed < 5000) {
            showingNotify = true;
            _canvasWide->setFont(&FreeSansBold9pt7b);
            _canvasWide->setTextColor(C_ACCENT_HIGH);
            _canvasWide->setCursor(75, 20);
            char notifyBuf[32];
            snprintf(notifyBuf, sizeof(notifyBuf), "Web: %s", _webNotifyUser);
            _canvasWide->print(notifyBuf);
        } else {

            _webNotifyStartMs = 0;
            _webNotifyUser[0] = '\0';
        }
    }


    if (!showingSilence && !showingNotify) {
        /*
         * Date and time centered in the available area.
         * Formato: "dd/mm/yy - HH:MM"
         * O separador " - " fica fixo no centro; a data cresce para
         * a esquerda e a hora cresce para a direita, garantindo que
         * prevents text from jumping when digits change.
         */
        _canvasWide->setTextSize(1);
        _canvasWide->setFont(&FreeSansBold9pt7b);
        _canvasWide->setTextColor(C_TEXT_MAIN);

        /* Separar data e hora pelo " - " */
        String fullTime = String(state.timeString);
        int sepIdx = fullTime.indexOf(" - ");
        String datePart = (sepIdx >= 0) ? fullTime.substring(0, sepIdx) : fullTime;
        String timePart = (sepIdx >= 0) ? fullTime.substring(sepIdx + 3) : "";

        /* Medir as 3 partes */
        int16_t bx, by; uint16_t bw, bh;
        uint16_t sepW, dateW, timeW;

        _canvasWide->getTextBounds(" - ", 0, 0, &bx, &by, &bw, &bh);
        sepW = bw;
        _canvasWide->getTextBounds(datePart, 0, 0, &bx, &by, &bw, &bh);
        dateW = bw;
        _canvasWide->getTextBounds(timePart, 0, 0, &bx, &by, &bw, &bh);
        timeW = bw;

        /*
         * Centro do separador fixo no meio do display (x=160).
         * Data cresce para a esquerda, hora para a direita.
         */
        const int centerX = 160;

        int sepX  = centerX - (int)sepW / 2;
        int dateX = sepX - (int)dateW;
        int timeX = sepX + (int)sepW;

        _canvasWide->setCursor(dateX, 20);
        _canvasWide->print(datePart);
        _canvasWide->setTextColor(C_TEXT_SUB);
        _canvasWide->setCursor(sepX, 20);
        _canvasWide->print(" - ");
        _canvasWide->setTextColor(C_TEXT_MAIN);
        _canvasWide->setCursor(timeX, 20);
        _canvasWide->print(timePart);
    }


    int xIcon = 305;

    if (state.pendingPkts > 0 || _pktArrowState > 0) {
        /*
         * NUMBER color: based on last send result.
         *   estado 1 ou 3 → azul (sucesso / flash de sucesso)
         *   estado 2       → vermelho (falha)
         *   estado 0       → azul (idle, nunca enviou)
         */
        uint16_t numColor = (_pktArrowState == 2) ? C_TEMP_HOT : C_ACCENT_HIGH;

        /*
         * ARROW color: same as number, except during flash (state 3)
         * onde alterna azul/branco a cada 300ms por 1 segundo.
         */
        uint16_t arrowColor = numColor;

        if (_pktArrowState == 3) {
            uint32_t now = millis();
            if (now >= _pktArrowFlashEnd) {
                _pktArrowState = 1;
                arrowColor = C_ACCENT_HIGH;
            } else {
                if (now - _pktArrowFlashTime >= 300) {
                    _pktArrowFlashOn   = !_pktArrowFlashOn;
                    _pktArrowFlashTime = now;
                }
                arrowColor = _pktArrowFlashOn ? RGB565(255, 255, 255) : C_ACCENT_HIGH;
            }
        }

        if (state.pendingPkts > 0) {
            char pktBuf[10];
            snprintf(pktBuf, sizeof(pktBuf), "%u", state.pendingPkts);

            _canvasWide->setFont(&FreeSansBold9pt7b);

            int16_t tx1, ty1; uint16_t tw, th;
            _canvasWide->getTextBounds(pktBuf, 0, 0, &tx1, &ty1, &tw, &th);

            /*
             * Layout: [number][gapNum][arrow][gapWifi][wifi]
             * Arrow: 12px. Gap between number and arrow: 4px.
             * Gap entre seta e wifi: 3px.
             * When the number is wide (>=3 digits), xIcon shifts back 1 character.
             */
            const int arrowTotalW = 12;
            const int gapToWifi   = 3;
            const int gapNumArrow = 4;
            int effectiveXIcon = xIcon;
            if ((int)tw > 24) effectiveXIcon -= 8;  /* shift back for large numbers */

            int arrowRight = effectiveXIcon - gapToWifi;
            int arrowLeft  = arrowRight - arrowTotalW;
            int textX      = arrowLeft - gapNumArrow - (int)tw;

            /* Number — fixed color based on status */
            _canvasWide->setTextColor(numColor);
            _canvasWide->setCursor(textX, 20);
            _canvasWide->print(pktBuf);

            /*
             * Seta para a direita:
             *   - Rectangular stem (6×3 px) at vertical center
             *   - Triangular tip (6×8 px) on the right
             */
            int ay     = 13;
            int shaftX = arrowLeft;
            int shaftW = 6;
            int tipX   = shaftX + shaftW;
            int tipW   = arrowTotalW - shaftW;

            _canvasWide->fillRect(shaftX, ay - 1, shaftW, 3, arrowColor);
            _canvasWide->fillTriangle(tipX,        ay - 4,
                                      tipX,        ay + 4,
                                      tipX + tipW, ay,
                                      arrowColor);

            /* Reposition wifi icon if necessary */
            if ((int)tw > 24) xIcon = effectiveXIcon;
        }
    }


    int barras = 0;
    if (state.wifiRssi > -100) {
        if      (state.wifiRssi > -55) barras = 4;
        else if (state.wifiRssi > -65) barras = 3;
        else if (state.wifiRssi > -75) barras = 2;
        else                           barras = 1;
    }
    for (int i = 0; i < 4; i++) {
        _canvasWide->fillRect(xIcon + (i * 3), 20 - (4 + (i * 2)), 2, 4 + (i * 2),
                              (i < barras) ? C_TEMP_OK : C_BAR_BG);
    }

    blitCanvas(_canvasWide, 0, 0, W, H);
}

void DisplayManager::drawAmbientPanel(float t, float h, bool isValid) {
    if (!_canvasWide) return;
    int16_t x1, y1; uint16_t w, h_bound;

    bool leftRed  = _alarmAmbientTemp && _alarmFlashPhase && !_alarmSilenced;
    bool rightRed = _alarmAmbientHum  && _alarmFlashPhase && !_alarmSilenced;
    bool isRed    = leftRed || rightRed;

    static constexpr int16_t CARD_X = 0, CARD_Y = 35;
    static constexpr int16_t CARD_W = 320, CARD_H = 75, CARD_R = 12;

    bool ambAlarm = (_alarmAmbientTemp || _alarmAmbientHum) && _alarmFlashPhase;
    uint16_t borderColor = ambAlarm ? RGB565(255, 60, 60) : C_ACCENT_HIGH;
    uint16_t cardBg = isRed ? RGB565(180, 30, 30) : C_CARD_BG;

    if (_ambientShowMinMax) {
        /* Track mode transition (no prior clear — blits cover 100%) */
        _ambientLastMinMax = true;

        /* =============================================================
         * MODO MIN/MAX — 3 blits com moldura incorporada
         * Sem maskStripCorners nas strips individuais.
         * ============================================================= */

        uint16_t txtSub  = isRed ? RGB565(220, 200, 200) : C_TEXT_MAIN;
        uint16_t icCol   = isRed ? RGB565(220, 200, 200) : C_TEXT_SUB;
        uint16_t mercCol = isRed ? RGB565(255, 255, 255) : C_TEMP_HOT;
        uint16_t dropCol = isRed ? RGB565(220, 200, 200) : C_HUMIDITY;
        uint16_t humCol  = isRed ? RGB565(255, 255, 255) : C_HUMIDITY;

        /* Dynamically calculated positions */
        _canvasWide->setFont(&FreeSansBold9pt7b);
        uint16_t minLblW, maxLblW;
        _canvasWide->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &minLblW, &h_bound);
        _canvasWide->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &maxLblW, &h_bound);
        int biggestLbl = ((int)minLblW > (int)maxLblW) ? (int)minLblW : (int)maxLblW;

        const int THERM_X = 8 + biggestLbl + 8;
        const int DOT_X   = THERM_X + 36;
        const int BTN_X   = 268;
        const int BTN_W   = 44;

        uint16_t sufW;
        _canvasWide->getTextBounds(tr(TR_HUM_SUFFIX), 0, 0, &x1, &y1, &sufW, &h_bound);

        /* Fixed drop position: worst case "100" + 3px gap + sufixo, at 8px from button */
        uint16_t numMaxW;
        _canvasWide->getTextBounds("100", 0, 0, &x1, &y1, &numMaxW, &h_bound);
        int worstNumX = BTN_X - 8 - (int)sufW - 3 - (int)numMaxW;
        const int DROP_FIX = worstNumX - 6;

        /* Blit 1: Title (20px) — with top corners + borders */
        {
            _canvasWide->fillScreen(cardBg);
            _canvasWide->setFont(&FreeSansBold9pt7b);
            _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(txtSub);
            _canvasWide->getTextBounds(tr(TR_AMBIENT), 0, 0, &x1, &y1, &w, &h_bound);
            _canvasWide->setCursor((CARD_W - (int)w) / 2, 15);
            _canvasWide->print(tr(TR_AMBIENT));
            maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
        }

        /* Blit 2: Min + Max together (43px) */
        {
            _canvasWide->fillScreen(cardBg);

            /* ---- Min line (y=0..20) ---- */
            {
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 15);
                _canvasWide->print(tr(TR_MIN_LBL));

                /* Improved mini thermometer (proportional to normal scale) */
                int tx = THERM_X, ty = 0;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);       /* base (contorno) */
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);  /* haste (contorno) */
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, cardBg); /* haste (vazio) */
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, cardBg);      /* base (vazio) */
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);     /* mercury (column) */
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);     /* mercury (bulb) */
                _canvasWide->fillCircle(tx + 4, ty + 2, 2, icCol);        /* topo arredondado */

                uint16_t tCol = isRed ? RGB565(255, 255, 255) : C_TEMP_OK;
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(tCol);
                if (isnan(_ambMinTemp)) {
                    uint16_t dw;
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)dw + 10, 15);
                    _canvasWide->print("--.-");
                } else {
                    char iP[8], dP[4];
                    snprintf(iP, sizeof(iP), "%d", (int)_ambMinTemp);
                    snprintf(dP, sizeof(dP), ".%d", abs((int)(_ambMinTemp * 10) % 10));
                    uint16_t iPw;
                    _canvasWide->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)iPw, 15); _canvasWide->print(iP);
                    _canvasWide->setCursor(DOT_X, 15); _canvasWide->print(dP);
                }
                uint16_t dpW;
                _canvasWide->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &h_bound);
                int endT = DOT_X + (int)dpW + 3;
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(endT, 2); _canvasWide->print("o");
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setCursor(endT + 6, 15); _canvasWide->print("C");

                _canvasWide->setFont(&FreeSansBold9pt7b);
                char hnum[8];
                if (isnan(_ambMinHum)) snprintf(hnum, sizeof(hnum), "--");
                else                   snprintf(hnum, sizeof(hnum), "%d", (int)_ambMinHum);
                uint16_t hnW;
                _canvasWide->getTextBounds(hnum, 0, 0, &x1, &y1, &hnW, &h_bound);
                /* Position back-to-front: suffix ends at 8px from button */
                int sufX = BTN_X - 8 - (int)sufW;
                int numX = sufX - 3 - (int)hnW;
                _canvasWide->setTextColor(humCol);
                _canvasWide->setCursor(numX, 15);
                _canvasWide->print(hnum);
                _canvasWide->setTextColor(isRed ? RGB565(255, 255, 255) : C_TEXT_MAIN);
                _canvasWide->setCursor(sufX, 15);
                _canvasWide->print(tr(TR_HUM_SUFFIX));
                /* Gota fixa alinhada verticalmente */
                uint16_t shine = isRed ? RGB565(255, 255, 255) : RGB565(200, 230, 255);
                _canvasWide->fillCircle(DROP_FIX + 5, 13, 6, dropCol);
                _canvasWide->fillTriangle(DROP_FIX + 5, 1, DROP_FIX, 11, DROP_FIX + 10, 11, dropCol);
                _canvasWide->fillCircle(DROP_FIX + 3, 11, 2, shine);
                _canvasWide->drawPixel(DROP_FIX + 3, 8, shine);
            }

            /* ---- Max line (y=22..42) ---- */
            {
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 37);
                _canvasWide->print(tr(TR_MAX_LBL));

                /* Improved mini thermometer */
                int tx = THERM_X, ty = 22;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, cardBg);
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, cardBg);
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 2, 2, icCol);

                uint16_t tCol = isRed ? RGB565(255, 255, 255) : C_TEMP_OK;
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(tCol);
                if (isnan(_ambMaxTemp)) {
                    uint16_t dw;
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)dw + 10, 37);
                    _canvasWide->print("--.-");
                } else {
                    char iP[8], dP[4];
                    snprintf(iP, sizeof(iP), "%d", (int)_ambMaxTemp);
                    snprintf(dP, sizeof(dP), ".%d", abs((int)(_ambMaxTemp * 10) % 10));
                    uint16_t iPw;
                    _canvasWide->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)iPw, 37); _canvasWide->print(iP);
                    _canvasWide->setCursor(DOT_X, 37); _canvasWide->print(dP);
                }
                uint16_t dpW;
                _canvasWide->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &h_bound);
                int endT = DOT_X + (int)dpW + 3;
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(endT, 24); _canvasWide->print("o");
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setCursor(endT + 6, 37); _canvasWide->print("C");

                _canvasWide->setFont(&FreeSansBold9pt7b);
                char hnum[8];
                if (isnan(_ambMaxHum)) snprintf(hnum, sizeof(hnum), "--");
                else                   snprintf(hnum, sizeof(hnum), "%d", (int)_ambMaxHum);
                uint16_t hnW;
                _canvasWide->getTextBounds(hnum, 0, 0, &x1, &y1, &hnW, &h_bound);
                int sufX = BTN_X - 8 - (int)sufW;
                int numX = sufX - 3 - (int)hnW;
                _canvasWide->setTextColor(humCol);
                _canvasWide->setCursor(numX, 37);
                _canvasWide->print(hnum);
                _canvasWide->setTextColor(isRed ? RGB565(255, 255, 255) : C_TEXT_MAIN);
                _canvasWide->setCursor(sufX, 37);
                _canvasWide->print(tr(TR_HUM_SUFFIX));
                /* Gota fixa alinhada verticalmente */
                uint16_t shine = isRed ? RGB565(255, 255, 255) : RGB565(200, 230, 255);
                _canvasWide->fillCircle(DROP_FIX + 5, 35, 6, dropCol);
                _canvasWide->fillTriangle(DROP_FIX + 5, 23, DROP_FIX, 33, DROP_FIX + 10, 33, dropCol);
                _canvasWide->fillCircle(DROP_FIX + 3, 33, 2, shine);
                _canvasWide->drawPixel(DROP_FIX + 3, 30, shine);
            }

            /* Graph button — full height of both lines */
            _canvasWide->fillRoundRect(BTN_X, 1, BTN_W, 42, 8, C_ACCENT);
            {
                int cx = BTN_X + BTN_W / 2;
                int cy = 22;
                /* Rounded graph bars */
                _canvasWide->fillRoundRect(cx - 11, cy,     4, 8, 1, C_BG_MAIN);
                _canvasWide->fillRoundRect(cx - 5,  cy - 6, 4, 14, 1, C_BG_MAIN);
                _canvasWide->fillRoundRect(cx + 1,  cy - 3, 4, 11, 1, C_BG_MAIN);
                /* Eixo horizontal */
                _canvasWide->drawFastHLine(cx - 12, cy + 9, 19, C_BG_MAIN);
                /* Seta play */
                _canvasWide->fillTriangle(cx + 10, cy - 5,
                                          cx + 10, cy + 5,
                                          cx + 17, cy, C_BG_MAIN);
            }

            /* Side borders (middle strip, no corners) */
            {
                uint16_t* buf = _canvasWide->getBuffer();
                int stride = _canvasWide->width();
                for (int row = 0; row < 43; row++) {
                    buf[row * stride] = borderColor;
                    buf[row * stride + CARD_W - 1] = borderColor;
                }
            }

            blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 43);
        }

        /* Blit 3: Fundo inferior (12px) — com cantos inferiores + bordas */
        {
            _canvasWide->fillScreen(cardBg);
            maskStripCorners(_canvasWide, 63, 12, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 63, CARD_W, 12);
        }

    } else {
        /* Track mode transition */
        _ambientLastMinMax = false;

        /* =============================================================
         * NORMAL MODE — layout with large icons
         * ============================================================= */
        uint16_t leftBg  = leftRed  ? RGB565(180, 30, 30) : C_CARD_BG;
        uint16_t rightBg = rightRed ? RGB565(180, 30, 30) : C_CARD_BG;

        /* Strip 1: Centered name (20px) — same position as min/max */
        {
            _canvasWide->fillRect(0, 0, 160, 20, leftBg);
            _canvasWide->fillRect(160, 0, 160, 20, rightBg);
            _canvasWide->setFont(&FreeSansBold9pt7b);
            _canvasWide->setTextSize(1);
            uint16_t nameColor = isRed ? RGB565(255, 255, 255) : C_TEXT_MAIN;
            _canvasWide->setTextColor(nameColor);
            _canvasWide->getTextBounds(tr(TR_AMBIENT), 0, 0, &x1, &y1, &w, &h_bound);
            _canvasWide->setCursor((CARD_W - (int)w) / 2, 15);
            _canvasWide->print(tr(TR_AMBIENT));
            maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
        }

        /* Strip 2: Gap to center content (8px) */
        {
            _canvasWide->fillRect(0, 0, 160, 8, leftBg);
            _canvasWide->fillRect(160, 0, 160, 8, rightBg);
            /* Bordas laterais */
            uint16_t* buf = _canvasWide->getBuffer();
            int stride = _canvasWide->width();
            for (int row = 0; row < 8; row++) {
                buf[row * stride] = borderColor;
                buf[row * stride + CARD_W - 1] = borderColor;
            }
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 8);
        }

        /* Strip 3: Temperatura + Umidade (40px) */
        {
            _canvasWide->fillRect(0, 0, 160, 40, leftBg);
            _canvasWide->fillRect(160, 0, 160, 40, rightBg);

            /* --- Temperatura --- */
            if (!isValid) {
                _canvasWide->setFont(&FreeSansBold12pt7b);
                _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(leftRed ? RGB565(255,255,255) : C_TEMP_HOT);
                _canvasWide->setCursor(25, 28);
                _canvasWide->print("Error");
            } else {
                _canvasWide->setFont(&FreeSansBold24pt7b);
                _canvasWide->setTextSize(1);
                uint16_t corT = C_TEMP_OK;
                if (isnan(t)) corT = C_TEXT_OFF;
                if (leftRed) corT = RGB565(255, 255, 255);
                _canvasWide->setTextColor(corT);

                int textAnchor = 92;
                int unitX = 0;
                if (isnan(t)) {
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &w, &h_bound);
                    _canvasWide->setCursor(textAnchor - w, 35);
                    _canvasWide->print("--.-");
                    unitX = textAnchor + 3;
                } else {
                    char intPart[10]; char decPart[5];
                    int fractional = abs((int)(t * 10) % 10);
                    snprintf(intPart, sizeof(intPart), "%d", (int)t);
                    snprintf(decPart, sizeof(decPart), ".%d", fractional);
                    _canvasWide->getTextBounds(intPart, 0, 0, &x1, &y1, &w, &h_bound);
                    int numCursorX = textAnchor - w - 4;
                    _canvasWide->setCursor(numCursorX, 35);
                    _canvasWide->print(intPart);
                    if (t < 0) {
                        int16_t mx1, my1; uint16_t mw, mh;
                        _canvasWide->getTextBounds("-", 0, 0, &mx1, &my1, &mw, &mh);
                        int eraseW = (int)mw / 3;
                        if (eraseW < 2) eraseW = 2;
                        _canvasWide->fillRect(numCursorX, 0, eraseW, 40, leftBg);
                    }
                    _canvasWide->setFont(&FreeSansBold24pt7b);
                    _canvasWide->setCursor(textAnchor, 35);
                    _canvasWide->print(decPart);
                    uint16_t decW;
                    _canvasWide->getTextBounds(decPart, 0, 0, &x1, &y1, &decW, &h_bound);
                    unitX = textAnchor + (int)decW + 3;
                }
                uint16_t unitCol = leftRed ? RGB565(220, 200, 200) : C_TEXT_MAIN;
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(unitCol);
                _canvasWide->setCursor(unitX, 17);
                _canvasWide->print("o");
                _canvasWide->setFont(&FreeSansBold12pt7b);
                _canvasWide->setCursor(unitX + 8, 35);
                _canvasWide->print("C");
                /* Thermometer icon — rendered last */
                {
                    uint16_t ic   = leftRed ? RGB565(220, 200, 200) : C_TEXT_SUB;
                    uint16_t merc = leftRed ? RGB565(255, 255, 255) : C_TEMP_HOT;
                    int ix = 14, iy = 4;
                    _canvasWide->fillCircle(ix + 5, iy + 26, 7, ic);
                    _canvasWide->fillRoundRect(ix + 1, iy, 8, 24, 4, ic);
                    _canvasWide->fillRoundRect(ix + 3, iy + 2, 4, 20, 2, leftBg);
                    _canvasWide->fillCircle(ix + 5, iy + 26, 5, leftBg);
                    _canvasWide->fillRect(ix + 4, iy + 10, 2, 14, merc);
                    _canvasWide->fillCircle(ix + 5, iy + 26, 4, merc);
                    _canvasWide->fillCircle(ix + 5, iy + 2, 2, ic);
                }
            }

            /* --- Umidade --- */
            if (!isValid) {
                _canvasWide->setFont(&FreeSansBold12pt7b);
                _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(rightRed ? RGB565(255,255,255) : C_TEMP_HOT);
                _canvasWide->setCursor(185, 28);
                _canvasWide->print("Error");
            } else {
                _canvasWide->setFont(&FreeSansBold12pt7b);
                int16_t px1, py1; uint16_t pctW, pctH;
                _canvasWide->getTextBounds(tr(TR_HUM_SUFFIX), 0, 0, &px1, &py1, &pctW, &pctH);
                const int rightMargin = 15;
                int pctX = CARD_W - rightMargin - (int)pctW;
                int humAnchor = pctX - 3;
                _canvasWide->setFont(&FreeSansBold24pt7b);
                _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(rightRed ? RGB565(255,255,255) : C_HUMIDITY);
                char humBuffer[6];
                if (isnan(h)) snprintf(humBuffer, sizeof(humBuffer), "--");
                else snprintf(humBuffer, sizeof(humBuffer), "%d", (int)h);
                _canvasWide->getTextBounds(humBuffer, 0, 0, &x1, &y1, &w, &h_bound);
                _canvasWide->setCursor(humAnchor - w, 35);
                _canvasWide->print(humBuffer);
                uint16_t pctCol = rightRed ? RGB565(220, 200, 200) : C_TEXT_MAIN;
                _canvasWide->setFont(&FreeSansBold12pt7b);
                _canvasWide->setTextColor(pctCol);
                _canvasWide->setCursor(pctX, 34);
                _canvasWide->print(tr(TR_HUM_SUFFIX));
                /* Drop icon */
                {
                    int dropRight = humAnchor - (int)w - 6;
                    int ix = dropRight - 14;
                    int iy = 4;
                    uint16_t ic    = rightRed ? RGB565(220, 200, 200) : C_HUMIDITY;
                    uint16_t shine = rightRed ? RGB565(255,255,255) : RGB565(200, 230, 255);
                    _canvasWide->fillCircle(ix + 6, iy + 20, 8, ic);
                    _canvasWide->fillTriangle(ix + 6, iy,
                                              ix - 1, iy + 18,
                                              ix + 13, iy + 18, ic);
                    _canvasWide->fillCircle(ix + 4, iy + 17, 3, shine);
                    _canvasWide->fillCircle(ix + 3, iy + 14, 1, shine);
                }
            }
            maskStripCorners(_canvasWide, 28, 40, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 28, CARD_W, 40);
        }

        /* Strip 4: Fundo inferior (7px) */
        {
            uint16_t lBg = leftRed  ? RGB565(180, 30, 30) : C_CARD_BG;
            uint16_t rBg = rightRed ? RGB565(180, 30, 30) : C_CARD_BG;
            _canvasWide->fillRect(0, 0, 160, 7, lBg);
            _canvasWide->fillRect(160, 0, 160, 7, rBg);
            maskStripCorners(_canvasWide, 68, 7, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 68, CARD_W, 7);
        }
    }
}

void DisplayManager::drawSlotPanel(float t, bool isValid, int slotIdx, const char* name, bool forceNameRedraw) {
    if(!_canvasWide) return;
    int16_t x1, y1; uint16_t w, h_bound;


    uint16_t panelBg   = slotAlarmBg(slotIdx);
    bool isRedPhase    = _alarmFlashPhase && isSlotAlarming(slotIdx) && !_alarmSilenced;
    uint16_t nameColor = isRedPhase ? RGB565(255, 255, 255) : C_TEXT_MAIN;
    uint16_t unitColor = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_MAIN;
    if (isSlotAlarming(slotIdx)) forceNameRedraw = true;


    static constexpr int16_t CARD_X = 0, CARD_Y = 115;
    static constexpr int16_t CARD_W = 320, CARD_H = 75, CARD_R = 12;


    bool slotAlarm = isSlotAlarming(slotIdx) && _alarmFlashPhase;
    uint16_t borderColor = slotAlarm ? RGB565(255, 60, 60) : C_ACCENT_HIGH;

    if (_slotShowMinMax) {
        /* Track mode transition */
        _slotLastMinMax = true;

        /* =============================================================
         * MODO MIN/MAX — 3 blits com moldura incorporada
         * Slot has no humidity.
         * ============================================================= */

        uint16_t txtSub  = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_MAIN;
        uint16_t icCol   = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_SUB;
        uint16_t mercCol = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_HOT;

        _canvasWide->setFont(&FreeSansBold9pt7b);
        uint16_t minLblW, maxLblW;
        _canvasWide->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &minLblW, &h_bound);
        _canvasWide->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &maxLblW, &h_bound);
        int biggestLbl = ((int)minLblW > (int)maxLblW) ? (int)minLblW : (int)maxLblW;

        const int THERM_X = 8 + biggestLbl + 8;
        const int DOT_X   = THERM_X + 36;
        const int BTN_X   = 268;
        const int BTN_W   = 44;

        /* Blit 1: Nome (20px) */
        {
            _canvasWide->fillScreen(panelBg);
            _canvasWide->setFont(&FreeSansBold9pt7b);
            _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(txtSub);
            const char* displayName = name;
            char buf[16];
            if (strlen(name) == 0) {
                snprintf(buf, 16, "Sensor %d", slotIdx);
                displayName = buf;
            }
            int16_t nx1, ny1; uint16_t nw, nh;
            _canvasWide->getTextBounds(displayName, 0, 0, &nx1, &ny1, &nw, &nh);
            _canvasWide->setCursor((CARD_W - (int)nw) / 2, 15);
            _canvasWide->print(displayName);
            maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
        }

        /* Blit 2: Min + Max together (43px) */
        {
            _canvasWide->fillScreen(panelBg);

            /* ---- Min line (y=0..20) ---- */
            {
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 15);
                _canvasWide->print(tr(TR_MIN_LBL));

                /* Improved mini thermometer */
                int tx = THERM_X, ty = 0;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, panelBg);
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, panelBg);
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 2, 2, icCol);

                uint16_t tCol = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_OK;
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(tCol);
                if (isnan(_slotMinTemp)) {
                    uint16_t dw;
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)dw + 10, 15);
                    _canvasWide->print("--.-");
                } else {
                    char iP[8], dP[4];
                    snprintf(iP, sizeof(iP), "%d", (int)_slotMinTemp);
                    snprintf(dP, sizeof(dP), ".%d", abs((int)(_slotMinTemp * 10) % 10));
                    uint16_t iPw;
                    _canvasWide->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)iPw, 15); _canvasWide->print(iP);
                    _canvasWide->setCursor(DOT_X, 15); _canvasWide->print(dP);
                }
                uint16_t dpW;
                _canvasWide->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &h_bound);
                int endT = DOT_X + (int)dpW + 3;
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(endT, 2); _canvasWide->print("o");
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setCursor(endT + 6, 15); _canvasWide->print("C");
            }

            /* ---- Max line (y=22..42) ---- */
            {
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 37);
                _canvasWide->print(tr(TR_MAX_LBL));

                /* Improved mini thermometer */
                int tx = THERM_X, ty = 22;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, panelBg);
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, panelBg);
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 2, 2, icCol);

                uint16_t tCol = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_OK;
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(tCol);
                if (isnan(_slotMaxTemp)) {
                    uint16_t dw;
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)dw + 10, 37);
                    _canvasWide->print("--.-");
                } else {
                    char iP[8], dP[4];
                    snprintf(iP, sizeof(iP), "%d", (int)_slotMaxTemp);
                    snprintf(dP, sizeof(dP), ".%d", abs((int)(_slotMaxTemp * 10) % 10));
                    uint16_t iPw;
                    _canvasWide->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)iPw, 37); _canvasWide->print(iP);
                    _canvasWide->setCursor(DOT_X, 37); _canvasWide->print(dP);
                }
                uint16_t dpW;
                _canvasWide->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &h_bound);
                int endT = DOT_X + (int)dpW + 3;
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(endT, 24); _canvasWide->print("o");
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setCursor(endT + 6, 37); _canvasWide->print("C");
            }

            /* Graph button */
            _canvasWide->fillRoundRect(BTN_X, 1, BTN_W, 42, 8, C_ACCENT);
            {
                int cx = BTN_X + BTN_W / 2;
                int cy = 22;
                /* Rounded graph bars */
                _canvasWide->fillRoundRect(cx - 11, cy,     4, 8, 1, C_BG_MAIN);
                _canvasWide->fillRoundRect(cx - 5,  cy - 6, 4, 14, 1, C_BG_MAIN);
                _canvasWide->fillRoundRect(cx + 1,  cy - 3, 4, 11, 1, C_BG_MAIN);
                /* Eixo horizontal */
                _canvasWide->drawFastHLine(cx - 12, cy + 9, 19, C_BG_MAIN);
                /* Seta play */
                _canvasWide->fillTriangle(cx + 10, cy - 5,
                                          cx + 10, cy + 5,
                                          cx + 17, cy, C_BG_MAIN);
            }

            /* Side borders (middle strip, no corners) */
            {
                uint16_t* buf = _canvasWide->getBuffer();
                int stride = _canvasWide->width();
                for (int row = 0; row < 43; row++) {
                    buf[row * stride] = borderColor;
                    buf[row * stride + CARD_W - 1] = borderColor;
                }
            }

            blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 43);
        }

        /* Blit 3: Fundo inferior (12px) — com cantos inferiores + bordas */
        {
            _canvasWide->fillScreen(panelBg);
            maskStripCorners(_canvasWide, 63, 12, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 63, CARD_W, 12);
        }

    } else {
        /* Force name redraw on min/max → normal transition */
        if (_slotLastMinMax) forceNameRedraw = true;
        _slotLastMinMax = false;

        /* =============================================================
         * NORMAL MODE — centered temperature with large icon
         * ============================================================= */

        if (forceNameRedraw) {
            _canvasWide->fillScreen(panelBg);
            _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(nameColor);
            const char* displayName = name;
            char buf[16];
            if (strlen(name) == 0) {
                snprintf(buf, 16, "Sensor %d", slotIdx);
                displayName = buf;
            }
            int16_t nx1, ny1; uint16_t nw, nh;
            _canvasWide->getTextBounds(displayName, 0, 0, &nx1, &ny1, &nw, &nh);
            _canvasWide->setCursor((CARD_W - (int)nw) / 2, 15);
            _canvasWide->print(displayName);
            maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
        }

        /* Gap strip to center content (8px) */
        {
            _canvasWide->fillScreen(panelBg);
            uint16_t* buf = _canvasWide->getBuffer();
            int stride = _canvasWide->width();
            for (int row = 0; row < 8; row++) {
                buf[row * stride] = borderColor;
                buf[row * stride + CARD_W - 1] = borderColor;
            }
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 8);
        }

        _canvasWide->fillScreen(panelBg);

        if (!isValid) {
            _canvasWide->setFont(&FreeSansBold12pt7b); _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(isRedPhase ? RGB565(255,255,255) : C_TEMP_HOT);
            int16_t ex1, ey1; uint16_t ew, eh;
            _canvasWide->getTextBounds("Error", 0, 0, &ex1, &ey1, &ew, &eh);
            _canvasWide->setCursor((CARD_W - (int)ew) / 2, 28);
            _canvasWide->print("Error");
        } else {
            const int iconW     = 20;
            const int iconGap   = 8;
            const int unitGap   = 3;
            const int dotGap    = 4;

            _canvasWide->setFont(&FreeSansBold24pt7b); _canvasWide->setTextSize(1);

            char intPart[10]; char decPart[5];
            bool isNan = isnan(t);
            uint16_t intW = 0, decW = 0;

            if (isNan) {
                _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &intW, &h_bound);
                decW = 0;
            } else {
                int fractional = abs((int)(t * 10) % 10);
                snprintf(intPart, sizeof(intPart), "%d", (int)t);
                snprintf(decPart, sizeof(decPart), ".%d", fractional);
                _canvasWide->getTextBounds(intPart, 0, 0, &x1, &y1, &intW, &h_bound);
                _canvasWide->getTextBounds(decPart, 0, 0, &x1, &y1, &decW, &h_bound);
            }

            _canvasWide->setFont(&FreeSansBold9pt7b);
            uint16_t degW;
            _canvasWide->getTextBounds("o", 0, 0, &x1, &y1, &degW, &h_bound);
            _canvasWide->setFont(&FreeSansBold12pt7b);
            uint16_t cW;
            _canvasWide->getTextBounds("C", 0, 0, &x1, &y1, &cW, &h_bound);
            int unitTotalW = (int)degW + 8 + (int)cW;

            int numW = (int)intW + (isNan ? 0 : dotGap + (int)decW);
            int totalW = iconW + iconGap + numW + unitGap + unitTotalW;
            int offsetX = (CARD_W - totalW) / 2;

            int iconX      = offsetX;
            int numAnchorX = iconX + iconW + iconGap + (int)intW;
            int unitX;

            _canvasWide->setFont(&FreeSansBold24pt7b);
            if (isNan) {
                _canvasWide->setTextColor(isRedPhase ? RGB565(200,180,180) : C_TEXT_OFF);
                _canvasWide->setCursor(iconX + iconW + iconGap, 35);
                _canvasWide->print("--.-");
                unitX = iconX + iconW + iconGap + (int)intW + unitGap;
            } else {
                _canvasWide->setTextColor(isRedPhase ? RGB565(255,255,255) : C_TEMP_OK);
                int numCursorX = numAnchorX - (int)intW;
                _canvasWide->setCursor(numCursorX, 35);
                _canvasWide->print(intPart);
                if (t < 0) {
                    int16_t mx1, my1; uint16_t mw, mh;
                    _canvasWide->getTextBounds("-", 0, 0, &mx1, &my1, &mw, &mh);
                    int eraseW = (int)mw / 3;
                    if (eraseW < 2) eraseW = 2;
                    _canvasWide->fillRect(numCursorX, 0, eraseW, 45, panelBg);
                }
                _canvasWide->setFont(&FreeSansBold24pt7b);
                _canvasWide->setCursor(numAnchorX + dotGap, 35);
                _canvasWide->print(decPart);
                unitX = numAnchorX + dotGap + (int)decW + unitGap;
            }

            _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(unitColor);
            _canvasWide->setCursor(unitX, 17); _canvasWide->print("o");
            _canvasWide->setFont(&FreeSansBold12pt7b);
            _canvasWide->setCursor(unitX + 8, 35); _canvasWide->print("C");

            /* Thermometer icon — rendered last */
            {
                uint16_t ic   = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_SUB;
                uint16_t merc = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_HOT;
                int ix = iconX, iy = 4;
                _canvasWide->fillCircle(ix + 10, iy + 26, 7, ic);
                _canvasWide->fillRoundRect(ix + 6, iy, 8, 24, 4, ic);
                _canvasWide->fillRoundRect(ix + 8, iy + 2, 4, 20, 2, panelBg);
                _canvasWide->fillCircle(ix + 10, iy + 26, 5, panelBg);
                _canvasWide->fillRect(ix + 9, iy + 10, 2, 14, merc);
                _canvasWide->fillCircle(ix + 10, iy + 26, 4, merc);
                _canvasWide->fillCircle(ix + 10, iy + 2, 2, ic);
            }
        }

        maskStripCorners(_canvasWide, 28, 40, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
        blitCanvas(_canvasWide, CARD_X, CARD_Y + 28, CARD_W, 40);

        /* Strip 4: Fundo inferior (7px) */
        {
            _canvasWide->fillScreen(panelBg);
            maskStripCorners(_canvasWide, 68, 7, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 68, CARD_W, 7);
        }
    }
}

void DisplayManager::drawBottomButtons(int selectedIdx, bool forceRedraw) {
    if(!_canvasWide) return;
    _canvasWide->fillScreen(C_BG_MAIN);
    int btnW = 58; int gap = 5; int xStart = 5;
    char labels[4][8]; int slotsMap[4];
    if (_currentPage == 0) { for(int i=0; i<4; i++) { slotsMap[i] = i; snprintf(labels[i], 8, "S%d", i); } }
    else if (_currentPage == 1) { for(int i=0; i<4; i++) { slotsMap[i] = i+4; snprintf(labels[i], 8, "S%d", i+4); } }
    else { slotsMap[0] = 8; strcpy(labels[0], "S8"); slotsMap[1] = 9; strcpy(labels[1], "S9"); slotsMap[2] = 10; strcpy(labels[2], "CFG"); slotsMap[3] = -1; strcpy(labels[3], ""); }

    for (int i = 0; i < 4; i++) {
        int realIdx = slotsMap[i]; if (realIdx == -1) continue;
        int x = xStart + (i * (btnW + gap));
        bool isActive = (realIdx == selectedIdx);


        bool btnAlarm = _alarmFlashPhase && isSlotAlarming(realIdx);
        uint16_t bgColor, txtColor;
        if (btnAlarm) {
            bgColor  = RGB565(180, 30, 30);
            txtColor = RGB565(255, 255, 255);
        } else if (isActive) {
            bgColor  = C_ACCENT_HIGH;
            txtColor = C_TEXT_MAIN;
        } else {
            bgColor  = C_CARD_BG;
            txtColor = isSlotAlarming(realIdx) ? C_TEMP_HOT : C_TEXT_SUB;
        }

        _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, bgColor);
        _canvasWide->setFont(&FreeSansBold12pt7b); _canvasWide->setTextSize(1); _canvasWide->setTextColor(txtColor);
        int16_t x1, y1; uint16_t w, h;
        _canvasWide->getTextBounds(labels[i], 0, 0, &x1, &y1, &w, &h);
        _canvasWide->setCursor(x + (btnW - w)/2, 28);
        _canvasWide->print(labels[i]);
    }
    int xPag = xStart + (4 * (btnW + gap));


    bool hasAlarmsOnOtherPages = false;
    if (_alarmSlotMask != 0) {
        for (int p = 0; p < 3; p++) {
            if (p == _currentPage) continue;
            int rs = p * 4;
            int re = (p < 2) ? rs + 4 : 10;
            for (int s = rs; s < re; s++) {
                if (isSlotAlarming(s)) { hasAlarmsOnOtherPages = true; break; }
            }
            if (hasAlarmsOnOtherPages) break;
        }
    }


    uint16_t pagTxtCol = C_TEXT_SUB;
    if (hasAlarmsOnOtherPages && _alarmFlashPhase) {

        _canvasWide->fillRoundRect(xPag, 0, btnW, 40, 12, RGB565(180, 30, 30));
        pagTxtCol = RGB565(255, 255, 255);
    } else if (hasAlarmsOnOtherPages) {

        _canvasWide->fillRoundRect(xPag, 0, btnW, 40, 12, C_CARD_BG);
        _canvasWide->drawRoundRect(xPag, 0, btnW, 40, 12, RGB565(255, 60, 60));
    } else {

        _canvasWide->drawRoundRect(xPag, 0, btnW, 40, 12, C_TEXT_SUB);
    }

    char pageStr[4];
    snprintf(pageStr, sizeof(pageStr), "%d", _currentPage + 1);
    _canvasWide->setFont(&FreeSansBold12pt7b); _canvasWide->setTextColor(pagTxtCol);
    _canvasWide->setCursor(xPag + 15, 28); _canvasWide->print(pageStr);
    _canvasWide->setFont(NULL); _canvasWide->setCursor(xPag + 35, 8); _canvasWide->print("/3");
    blitCanvas(_canvasWide, 0, 195, 320, 45);
}

void DisplayManager::drawLoadingScreen() {

    _tft->fillScreen(C_BG_MAIN);
    _tft->setFont(&FreeSansBold12pt7b);
    _tft->setTextColor(C_TEXT_MAIN);
    int16_t x1, y1; uint16_t w, h;
    String t1 = tr(TR_LOADING);
    _tft->getTextBounds(t1, 0, 0, &x1, &y1, &w, &h);
    _tft->setCursor(160 - (w/2), 127);
    _tft->print(t1);
    _loadingDrawn = true;
}


void __not_in_flash_func(DisplayManager::drawWebBusyOverlay)() {


    _tft->fillScreen(C_BG_MAIN);
    _tft->setFont(&FreeSansBold12pt7b);
    char userLine[32];
    mutex_enter_blocking(&_stateMutex);
    snprintf(userLine, sizeof(userLine), "'%s'", _webBusyUser);
    mutex_exit(&_stateMutex);
    int16_t x1, y1; uint16_t w, h;

    _tft->setTextColor(C_TEXT_MAIN);
    _tft->getTextBounds(userLine, 0, 0, &x1, &y1, &w, &h);
    _tft->setCursor(160 - (w / 2), 100);
    _tft->print(userLine);

    const char* line2 = "Acessando via Web.";
    _tft->setTextColor(C_TEXT_SUB);
    _tft->getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
    _tft->setCursor(160 - (w / 2), 130);
    _tft->print(line2);

    const char* line3 = "Aguarde...";
    _tft->getTextBounds(line3, 0, 0, &x1, &y1, &w, &h);
    _tft->setCursor(160 - (w / 2), 160);
    _tft->print(line3);
    _webOverlayShown = true;
}

void DisplayManager::requestLoadingScreen() {
    _loadingDrawn = false;
    _repaintLoading = true;
    _uiMode = MODE_GRAPH_LOADING;
}

void DisplayManager::drawPeriodButtons() {
    if (!_canvasWide) return;
    const char* ranges[] = {"1H", "6H", "12H", "24H", "7D"};
    int btnW = 58, btnH = 40, gap = 5, startX = 5;

    _canvasWide->fillScreen(C_BG_MAIN);

    for (int i = 0; i < 5; i++) {
        int x = startX + (i * (btnW + gap));
        bool active = (i == _graphData.timeRange);
        uint16_t bg  = active ? C_ACCENT : C_CARD_BG;
        uint16_t txt = active ? C_BG_MAIN : C_TEXT_SUB;

        _canvasWide->fillRoundRect(x, 0, btnW, btnH, 12, bg);
        if (!active) _canvasWide->drawRoundRect(x, 0, btnW, btnH, 12, C_TEXT_SUB);

        _canvasWide->setFont(&FreeSansBold12pt7b);
        _canvasWide->setTextSize(1);
        _canvasWide->setTextColor(txt);
        int16_t tx1, ty1; uint16_t tw, th;
        _canvasWide->getTextBounds(ranges[i], 0, 0, &tx1, &ty1, &tw, &th);
        /* Precise horizontal and vertical centering */
        int curX = x + (btnW - tw) / 2 - tx1;
        int curY = (btnH - th) / 2 - ty1;
        _canvasWide->setCursor(curX, curY);
        _canvasWide->print(ranges[i]);
    }

    blitCanvas(_canvasWide, 0, 195, 320, btnH);
}

void DisplayManager::drawGraphIcon(int16_t x, int16_t y, uint16_t color) {
    _tft->fillRect(x,      y + 12, 6, 10, color);
    _tft->fillRect(x + 8,  y + 4,  6, 18, color);
    _tft->fillRect(x + 16, y + 8,  6, 14, color);
    _tft->drawLine(x, y+2, x+22, y+2, color);
}

void DisplayManager::drawStatsScreen() {
    _tft->fillScreen(C_BG_MAIN);
    int16_t x1, y1; uint16_t w, h_bound;


    _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
    _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
    _tft->setCursor(14, 23); _tft->print(_graphData.title);


    _tft->fillRoundRect(280, 4, 36, 24, 6, C_TEMP_WARM);
    _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_BG_MAIN);
    _tft->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor(298 - w / 2, 23); _tft->print("X");


    _tft->setFont(NULL); _tft->setTextSize(1); _tft->setTextColor(C_TEXT_SUB);
    _tft->setCursor(14, 38); _tft->print("ID: "); _tft->print(_graphData.hwId);
    _tft->setCursor(14, 49); _tft->print("SN: "); _tft->print(_graphData.rom);


    auto drawTemp = [&](float val, int anchorX, int y, uint16_t color, bool large) {
        int16_t bx1, by1; uint16_t bw, bh;
        int symbolX = anchorX + (large ? 38 : 28);
        _tft->setTextColor(color);

        if (large) _tft->setFont(&FreeSansBold24pt7b);
        else       _tft->setFont(&FreeSansBold12pt7b);

        if (isnan(val)) {
            _tft->getTextBounds("--.-", 0, 0, &bx1, &by1, &bw, &bh);
            _tft->setCursor(anchorX - bw, y); _tft->print("--.-");
        } else {
            char iPart[8], dPart[4];
            snprintf(iPart, sizeof(iPart), "%d", (int)val);
            snprintf(dPart, sizeof(dPart), ".%d", abs((int)(val * 10) % 10));
            _tft->getTextBounds(iPart, 0, 0, &bx1, &by1, &bw, &bh);
            _tft->setCursor(anchorX - bw - 2, y); _tft->print(iPart);
            _tft->setCursor(anchorX, y);           _tft->print(dPart);
        }


        if (large) {
            _tft->setFont(&FreeSansBold9pt7b);  _tft->setCursor(symbolX, y - 18); _tft->print("o");
            _tft->setFont(&FreeSansBold12pt7b); _tft->setCursor(symbolX + 8, y);  _tft->print("C");
        } else {
            _tft->setFont(NULL);                _tft->setCursor(symbolX, y - 12); _tft->print("o");
            _tft->setFont(&FreeSansBold9pt7b);  _tft->setCursor(symbolX + 7, y);  _tft->print("C");
        }
    };


    auto drawHum = [&](float val, int anchorX, int y, uint16_t color) {
        int16_t bx1, by1; uint16_t bw, bh;
        char buf[6];
        if (isnan(val)) snprintf(buf, sizeof(buf), "--");
        else            snprintf(buf, sizeof(buf), "%d", (int)val);

        _tft->setFont(&FreeSansBold12pt7b); _tft->setTextColor(color);
        _tft->getTextBounds(buf, 0, 0, &bx1, &by1, &bw, &bh);
        _tft->setCursor(anchorX - bw, y); _tft->print(buf);
        _tft->setTextColor(C_TEXT_SUB); _tft->setCursor(anchorX + 4, y); _tft->print("%");
    };


    if (_graphData.hasHumidity && !isnan(_currentMinHum)) {
        const int cardW = 148, cardH = 96, cardR = 12;
        const int cardY = 62;
        const int leftX = 5, rightX = 167;


        _tft->fillRoundRect(leftX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(leftX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEMP_HOT);
        _tft->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(leftX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MAX_LBL));


        drawTemp(_graphData.maxVal, leftX + 68, cardY + 52, C_TEMP_HOT, false);


        _tft->fillCircle(leftX + 25, cardY + 74, 3, C_HUMIDITY);
        drawHum(_currentMaxHum, leftX + 80, cardY + 80, C_HUMIDITY);


        _tft->fillRoundRect(rightX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(rightX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEMP_OK);
        _tft->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(rightX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MIN_LBL));

        drawTemp(_graphData.minVal, rightX + 68, cardY + 52, C_TEMP_OK, false);

        _tft->fillCircle(rightX + 25, cardY + 74, 3, C_HUMIDITY);
        drawHum(_currentMinHum, rightX + 80, cardY + 80, C_HUMIDITY);
    }


    else {
        const int cardW = 148, cardH = 96, cardR = 12;
        const int cardY = 62;
        const int leftX = 5, rightX = 167;


        _tft->fillRoundRect(leftX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(leftX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEMP_HOT);
        _tft->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(leftX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MAX_LBL));

        drawTemp(_graphData.maxVal, leftX + 55, cardY + 68, C_TEMP_HOT, true);


        _tft->fillRoundRect(rightX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(rightX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEMP_OK);
        _tft->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(rightX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MIN_LBL));

        drawTemp(_graphData.minVal, rightX + 55, cardY + 68, C_TEMP_OK, true);
    }


    {
        const char* rangeLabels[] = {"1h", "6h", "24h", "3d", "7d"};
        const char* rangeText = ((_graphData.timeRange >= 0) && (_graphData.timeRange < 5))
                                ? rangeLabels[_graphData.timeRange] : "?";
        char periodBuf[16];
        snprintf(periodBuf, sizeof(periodBuf), "[ %s ]", rangeText);
        _tft->setFont(NULL); _tft->setTextSize(1); _tft->setTextColor(C_TEXT_OFF);
        _tft->getTextBounds(periodBuf, 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(160 - w / 2, 168); _tft->print(periodBuf);
    }


    _tft->fillRoundRect(10, 180, 300, 40, 12, C_ACCENT);


    int icX = 50, icY = 188;
    _tft->fillRect(icX,      icY + 8, 4, 12, C_BG_MAIN);
    _tft->fillRect(icX + 6,  icY + 2, 4, 18, C_BG_MAIN);
    _tft->fillRect(icX + 12, icY + 6, 4, 14, C_BG_MAIN);
    _tft->drawFastHLine(icX - 2, icY + 20, 20, C_BG_MAIN);


    _tft->setFont(&FreeSansBold12pt7b); _tft->setTextColor(C_BG_MAIN);
    String btnTxt = tr(TR_PLOT_CHART);
    _tft->getTextBounds(btnTxt, 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor(160 - (w / 2) + 15, 207);
    _tft->print(btnTxt);
}

void DisplayManager::drawGraphScreen() {
    _tft->fillScreen(C_BG_MAIN);
    _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
    _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
    _tft->setCursor(10, 22); _tft->print(_graphData.title);
    _tft->fillRoundRect(280, 4, 36, 24, 6, C_TEMP_WARM);
    _tft->setCursor(291, 21); _tft->setTextColor(C_BG_MAIN); _tft->print("X");

    _tft->setFont(NULL); _tft->setTextSize(1);

    if (_graphData.hasHumidity && !isnan(_currentMinHum)) {
        _tft->fillCircle(14, 39, 3, C_TEMP_HOT);
        _tft->setTextColor(C_TEMP_HOT); _tft->setCursor(20, 36);
        _tft->print("Temp ");
        _tft->setTextColor(C_TEXT_SUB); _tft->print(_graphData.minVal, 1);
        _tft->print("~"); _tft->print(_graphData.maxVal, 1);
        _tft->setTextColor(C_TEMP_HOT); _tft->print(" C");

        _tft->fillCircle(180, 39, 3, C_HUMIDITY);
        _tft->setTextColor(C_HUMIDITY); _tft->setCursor(186, 36);
        _tft->print("Umid ");
        _tft->setTextColor(C_TEXT_SUB); _tft->print((int)_currentMinHum);
        _tft->print("~"); _tft->print((int)_currentMaxHum);
        _tft->setTextColor(C_HUMIDITY); _tft->print(" %");
    } else {
        _tft->fillCircle(54, 39, 3, C_TEMP_HOT);
        _tft->setTextColor(C_TEXT_SUB); _tft->setCursor(60, 36);
        _tft->print("Max: ");
        _tft->setTextColor(C_TEMP_HOT); _tft->print(_graphData.maxVal, 1);
        _tft->print(" C");
        _tft->fillCircle(174, 39, 3, C_TEMP_OK);
        _tft->setTextColor(C_TEXT_SUB); _tft->setCursor(180, 36);
        _tft->print("Min: ");
        _tft->setTextColor(C_TEMP_OK); _tft->print(_graphData.minVal, 1);
        _tft->print(" C");
    }
    _tft->drawFastHLine(0, 48, 320, C_CARD_BG);

    int gx = 35, gy = 55, gw = 275, gh = 135;

    if (_graphData.hasHumidity && !isnan(_currentMinHum)) {
        gw = 245;
    }

    _tft->drawFastVLine(gx, gy, gh, C_AXIS);
    _tft->drawFastHLine(gx, gy + gh, gw, C_AXIS);
    for (int y = gy; y < gy + gh; y += (gh / 4)) {
        for (int x = gx + 2; x < gx + gw; x += 6) {
            _tft->drawPixel(x, y, C_GRID);
            _tft->drawPixel(x + 1, y, C_GRID);
        }
    }
    for (int x = gx; x < gx + gw; x += 40) _tft->drawFastVLine(x, gy, gh, C_GRID);

    if (_graphData.count >= 2) {
        float tempRange = _graphData.maxVal - _graphData.minVal;
        if (tempRange < 2.0f) tempRange = 2.0f;
        _tft->setFont(NULL);
        _tft->setTextColor(C_TEMP_HOT);
        _tft->setCursor(2, gy); _tft->print((int)_graphData.maxVal);
        _tft->setTextColor(C_TEMP_OK);
        _tft->setCursor(2, gy + gh - 8); _tft->print((int)_graphData.minVal);
        float tempMid = (_graphData.maxVal + _graphData.minVal) / 2.0f;
        _tft->setTextColor(C_TEXT_SUB);
        _tft->setCursor(2, gy + gh / 2 - 4); _tft->print((int)tempMid);

        for (int i = 0; i < _graphData.count - 1; i++) {
            int x1 = gx + map(i, 0, _graphData.count - 1, 0, gw);
            int x2 = gx + map(i + 1, 0, _graphData.count - 1, 0, gw);
            int y1 = gy + gh - (int)((_graphData.pointsV1[i] - _graphData.minVal) / tempRange * gh);
            int y2 = gy + gh - (int)((_graphData.pointsV1[i+1] - _graphData.minVal) / tempRange * gh);
            if(y1 < gy) y1 = gy; if(y1 > gy+gh) y1 = gy+gh;
            if(y2 < gy) y2 = gy; if(y2 > gy+gh) y2 = gy+gh;
            _tft->drawLine(x1, y1, x2, y2, C_TEMP_HOT);
            _tft->drawLine(x1, y1+1, x2, y2+1, C_TEMP_HOT);
        }

        if (_graphData.hasHumidity && !isnan(_currentMinHum)) {
            float humMin = _currentMinHum;
            float humMax = _currentMaxHum;
            float humRange = humMax - humMin;
            if (humRange < 5.0f) humRange = 5.0f;

            int rxAxis = gx + gw;
            _tft->drawFastVLine(rxAxis, gy, gh, C_AXIS);
            _tft->setFont(NULL);
            _tft->setTextColor(C_HUMIDITY);
            char humLabel[8];
            snprintf(humLabel, sizeof(humLabel), "%d%%", (int)humMax);
            _tft->setCursor(rxAxis + 3, gy); _tft->print(humLabel);
            snprintf(humLabel, sizeof(humLabel), "%d%%", (int)humMin);
            _tft->setCursor(rxAxis + 3, gy + gh - 8); _tft->print(humLabel);

            for (int i = 0; i < _graphData.count - 1; i++) {
                int x1 = gx + map(i, 0, _graphData.count - 1, 0, gw);
                int x2 = gx + map(i + 1, 0, _graphData.count - 1, 0, gw);
                float v1 = _graphData.pointsV2[i];
                float v2 = _graphData.pointsV2[i+1];
                if (isnan(v1) || isnan(v2)) continue;
                int y1 = gy + gh - (int)((v1 - humMin) / humRange * gh);
                int y2 = gy + gh - (int)((v2 - humMin) / humRange * gh);
                if(y1 < gy) y1 = gy; if(y1 > gy+gh) y1 = gy+gh;
                if(y2 < gy) y2 = gy; if(y2 > gy+gh) y2 = gy+gh;
                _tft->drawLine(x1, y1, x2, y2, C_HUMIDITY);
            }
        }
    } else {
        int16_t bx, by; uint16_t bw, bh;
        _tft->setFont(&FreeSansBold12pt7b); _tft->setTextColor(C_TEXT_SUB);
        String nd = tr(TR_NO_DATA);
        _tft->getTextBounds(nd, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(160 - (bw/2), 125); _tft->print(nd);
    }
    drawPeriodButtons();
}

void DisplayManager::handleTouch() {
    if (!_ts->touched()) {
        _btnHoldStartTime = 0;
        _lastPressedBtn = -1;
        if (_uiMode != MODE_DASHBOARD && !_sharedState.isBooting) {
            if (millis() - _lastTouchTime > 30000)forceDashboard();
        }
        return;
    }


    if (millis() - _lastTouchTime < 50) return;
    TS_Point p = _ts->getPoint();
    if (p.z < 400) return;


    if (_uiMode == MODE_SETTINGS_TOUCH_CAL) {
        _lastTouchTime = millis();


        if (_calPhase >= 1) {
            int16_t screenX, screenY;
            mapTouchPoint(p, screenX, screenY);
            if (screenY >= 185) {
                if (_calPhase == 2) {

                    if (_sharedState.isBooting) {
                        _uiMode = MODE_DASHBOARD;
                        _isDirty = true;
                        _forceFullRedraw = true;
                    } else {
                        showSettingsMain();
                    }
                } else {

                    _calStep = 0;
                    _calPhase = 0;
                    memset(_calRawX, 0, sizeof(_calRawX));
                    memset(_calRawY, 0, sizeof(_calRawY));
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
            }
            return;
        }


        if (_calStep < 8) {
            _calRawX[_calStep] = p.x;
            _calRawY[_calStep] = p.y;
            _calStep++;

            if (_calStep < 8) {

                _repaintSettings = true;
            } else {


                const int16_t TOLERANCE = 200;
                bool rejected = false;

                for (int i = 0; i < 4; i++) {
                    int16_t dx = abs(_calRawX[i] - _calRawX[i + 4]);
                    int16_t dy = abs(_calRawY[i] - _calRawY[i + 4]);
                    if (dx > TOLERANCE || dy > TOLERANCE) {
                        rejected = true;
                        break;
                    }
                }

                if (rejected) {

                    _calPhase = 1;
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                } else {


                    float avgRawX[4], avgRawY[4];
                    for (int i = 0; i < 4; i++) {
                        avgRawX[i] = (_calRawX[i] + _calRawX[i + 4]) / 2.0f;
                        avgRawY[i] = (_calRawY[i] + _calRawY[i + 4]) / 2.0f;
                    }


                    float rawLeft_X  = (avgRawX[0] + avgRawX[2]) / 2.0f;
                    float rawRight_X = (avgRawX[1] + avgRawX[3]) / 2.0f;
                    float rawTop_Y   = (avgRawY[0] + avgRawY[1]) / 2.0f;
                    float rawBot_Y   = (avgRawY[2] + avgRawY[3]) / 2.0f;

                    float rawLeft_Y  = (avgRawY[0] + avgRawY[2]) / 2.0f;
                    float rawRight_Y = (avgRawY[1] + avgRawY[3]) / 2.0f;
                    float rawTop_X   = (avgRawX[0] + avgRawX[1]) / 2.0f;
                    float rawBot_X   = (avgRawX[2] + avgRawX[3]) / 2.0f;


                    float dxInRawX = fabsf(rawRight_X - rawLeft_X);
                    float dxInRawY = fabsf(rawRight_Y - rawLeft_Y);
                    _calSwapXY = (dxInRawY > dxInRawX);

                    if (_calSwapXY) {
                        float spanX = rawRight_Y - rawLeft_Y;
                        _calXMin = (int16_t)(rawLeft_Y  - 20.0f * spanX / 280.0f);
                        _calXMax = (int16_t)(rawRight_Y + 20.0f * spanX / 280.0f);
                        float spanY = rawBot_X - rawTop_X;
                        _calYMin = (int16_t)(rawTop_X   - 20.0f * spanY / 200.0f);
                        _calYMax = (int16_t)(rawBot_X   + 20.0f * spanY / 200.0f);
                    } else {
                        float spanX = rawRight_X - rawLeft_X;
                        _calXMin = (int16_t)(rawLeft_X  - 20.0f * spanX / 280.0f);
                        _calXMax = (int16_t)(rawRight_X + 20.0f * spanX / 280.0f);
                        float spanY = rawBot_Y - rawTop_Y;
                        _calYMin = (int16_t)(rawTop_Y   - 20.0f * spanY / 200.0f);
                        _calYMax = (int16_t)(rawBot_Y   + 20.0f * spanY / 200.0f);
                    }

                    _calValid = true;


                    UiEvent ev;
                    ev.type = UiEvent::EVT_APPLY_TOUCH_CAL;
                    queue_try_add(&_eventQueue, &ev);

                    _calPhase = 2;
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
            }
        }
        return;
    }


    int16_t x, y;
    mapTouchPoint(p, x, y);

    if (_sharedState.isBooting) {
        if (_sharedState.showSkipButton) {
            if (y > 190 && x > 80 && x < 240) _skipPressed = true;
        }
        return;
    }


    _lastTouchTime = millis();


    bool webBusyNow = false;
    if (mutex_try_enter(&_stateMutex, NULL)) {
        webBusyNow = _webBusy;
        mutex_exit(&_stateMutex);
    }


    if (webBusyNow && _uiMode == MODE_DASHBOARD) {
        if (!acceptTouch(0xF0)) return;
        if (!_webOverlayShown) {
            drawWebBusyOverlay();
        }
        _webOverlayPending = true;
        return;
    }

    if (_uiMode == MODE_DASHBOARD) {
        if (y > 35 && y < 110) {
            if (!acceptTouch(0)) return;

            /* Right corner: graph button (priority over alarm) */
            if (_ambientShowMinMax && x > 266) {
                _ambientShowMinMax = false;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = -1; ev.param = 0;
                queue_try_add(&_eventQueue, &ev);
                return;
            }

            if (_alarmAmbientTemp || _alarmAmbientHum) {
                showAlarmAction(-1);
                return;
            }

            /* Alternar entre modo normal e modo min/max */
            _ambientShowMinMax = !_ambientShowMinMax;
            {
                SystemState snap;
                mutex_enter_blocking(&_stateMutex);
                snap = _sharedState;
                mutex_exit(&_stateMutex);
                drawAmbientPanel(snap.ambientTemp, snap.ambientHum, snap.ambientValid);
            }
            return;
        }
        if (y > 115 && y < 190) {
            if (!acceptTouch(1)) return;
            int sensorIdToGraph = -1;
            if (_sharedState.selectedSlotIdx >= 0 && _sharedState.selectedSlotIdx <= 10) sensorIdToGraph = _sharedState.selectedSlotIdx;

            /* Right corner: graph button (priority over alarm) */
            if (_slotShowMinMax && x > 266) {
                _slotShowMinMax = false;
                if (sensorIdToGraph != -1) {
                    UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = sensorIdToGraph; ev.param = 0;
                    queue_try_add(&_eventQueue, &ev);
                }
                return;
            }

            if (sensorIdToGraph >= 0 && isSlotAlarming(sensorIdToGraph)) {
                showAlarmAction((int8_t)sensorIdToGraph);
                return;
            }

            /* Alternar entre modo normal e modo min/max */
            _slotShowMinMax = !_slotShowMinMax;
            {
                SystemState snap;
                mutex_enter_blocking(&_stateMutex);
                snap = _sharedState;
                mutex_exit(&_stateMutex);
                drawSlotPanel(snap.slotTemp, snap.slotValid,
                              snap.selectedSlotIdx, snap.slotName, true);
            }
            return;
        }
        if (y > 195) {
            int btnW = 58; int gap = 5; int pitch = btnW + gap; int btnIdx = (x - 5) / pitch;
            if (btnIdx == 4) {
                if (!acceptTouch(14)) return;
                _currentPage++;
                if (_currentPage > 2) _currentPage = 0;
                drawBottomButtons(_sharedState.selectedSlotIdx, true); return;
            }
            if (btnIdx >= 0 && btnIdx <= 3) {
                if (!acceptTouch(10 + btnIdx)) return;
                if (_currentPage == 2 && btnIdx == 2) {
                    UiEvent ev; ev.type = UiEvent::EVT_OPEN_SETTINGS;
                    queue_try_add(&_eventQueue, &ev); return;
                }
                int targetId = -1;
                if (_currentPage == 0) targetId = btnIdx;
                else if (_currentPage == 1) targetId = btnIdx + 4;
                else if (_currentPage == 2) {
                    if (btnIdx == 0) targetId = 8;
                    if (btnIdx == 1) targetId = 9;
                }
                if (targetId != -1) {
                    _slotShowMinMax = false;
                    drawBottomButtons(targetId, false);
                    UiEvent ev; ev.type = UiEvent::EVT_SLOT_SELECT; ev.id = targetId;
                    queue_try_add(&_eventQueue, &ev);
                }
            }
        }
    }
    else if (_uiMode == MODE_STATS_VIEW) {
        if (y < 40 && x > 270) { if (!acceptTouch(0)) return; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
        if (y > 170) {
            if (!acceptTouch(1)) return;
            UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = 3;
            queue_try_add(&_eventQueue, &ev); return;
        }
    }
    else if (_uiMode == MODE_GRAPH_VIEW) {
        if (y < 40 && x > 270) { if (!acceptTouch(0)) return; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
        if (y >= 195) {
            int btnW = 58; int gap = 5; int startX = 5; int clickedBtn = -1;
            for(int i=0; i<5; i++) { int bx = startX + (i * (btnW + gap)); if (x >= bx && x <= bx + btnW) { clickedBtn = i; break; } }
            if (clickedBtn != -1 && clickedBtn != _graphData.timeRange) {
                if (!acceptTouch(1 + clickedBtn)) return;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = clickedBtn;
                queue_try_add(&_eventQueue, &ev);
            }
        }
    }
    else if (_uiMode == MODE_SETTINGS_THEMES) {
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int actualIndex = (_themePage * 4) + clickedIndex;
            if (actualIndex < getThemeCount() && actualIndex != _previewThemeIdx) {
                if (!acceptTouch(clickedIndex)) return;
                _previewThemeIdx = actualIndex; _themePage = _previewThemeIdx / 4; _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptTouch(10)) return;
                if (_previewThemeIdx > 0) _previewThemeIdx--; else _previewThemeIdx = getThemeCount() - 1;
                _themePage = _previewThemeIdx / 4; _repaintSettings = true;
            } else if (x < 138) {
                if (!acceptTouch(11)) return;
                if (_previewThemeIdx < getThemeCount() - 1) _previewThemeIdx++; else _previewThemeIdx = 0;
                _themePage = _previewThemeIdx / 4; _repaintSettings = true;
            } else if (x < 219) {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            } else {
                if (!acceptTouch(13)) return;
                UiEvent ev; ev.type = UiEvent::EVT_APPLY_THEME; ev.id = _previewThemeIdx; queue_try_add(&_eventQueue, &ev);
            }
        }
    }
    else if (_uiMode == MODE_SETTINGS_ALARMS) {
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int mapIdx = (_alarmPage * 4) + clickedIndex;
            if (mapIdx < _activeSensorCount) {
                /*
                 * Zona de toque do SIM/NAO: lado direito do item.
                 * Items renderizados em x=10..295, SIM/NAO fica nos ~60px finais.
                 * Zona do toggle: x >= 230 (tela).
                 */
                bool touchOnStatus = (x >= 230);

                if (touchOnStatus && mapIdx == _alarmSelection) {
                    /* Touch on YES/NO of selected item: toggle or edit */
                    if (!acceptTouch(clickedIndex + 4)) return;
                    int actualSensorId = _activeSensorsMap[_alarmSelection];
                    SensorRecord* rec = (actualSensorId == -1)
                        ? &_sysConfigPtr->ambientSensor
                        : &_sysConfigPtr->sensors[actualSensorId];

                    if (rec->alarmsActive) {
                        /* SIM → NAO: desativa e salva imediatamente */
                        rec->alarmsActive = false;
                        UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
                        ev.id = actualSensorId;
                        queue_try_add(&_eventQueue, &ev);
                        _repaintSettings = true;
                    } else {
                        /* NO → enters the limit editing screen */
                        showAlarmEdit(actualSensorId);
                    }
                } else if (mapIdx != _alarmSelection) {
                    /* Toque no nome/barra: seleciona o item */
                    if (!acceptTouch(clickedIndex)) return;
                    _alarmSelection = mapIdx; _alarmPage = _alarmSelection / 4; _repaintSettings = true;
                }
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptTouch(10)) return;
                if (_alarmSelection > 0) _alarmSelection--; else _alarmSelection = _activeSensorCount - 1;
                _alarmPage = _alarmSelection / 4; _repaintSettings = true;
            } else if (x < 138) {
                if (!acceptTouch(11)) return;
                if (_alarmSelection < _activeSensorCount - 1) _alarmSelection++; else _alarmSelection = 0;
                _alarmPage = _alarmSelection / 4; _repaintSettings = true;
            } else {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            }
        }
    }
    else if (_uiMode == MODE_SETTINGS_ALARM_EDIT) {
        bool hasHum = (_editSensorIdx == -1 || _tempAlarmConfig.rom[0] != 0x28);


        if (y >= 50 && y <= 115) {
            uint8_t zone = (x < 160) ? 0 : 1;
            if (!acceptTouch(zone)) return;
            _editFieldFocus = zone;
            _repaintSettings = true;
        }

        else if (hasHum && y >= 125 && y <= 170) {
            uint8_t zone = (x < 160) ? 2 : 3;
            if (!acceptTouch(zone)) return;
            _editFieldFocus = zone;
            _repaintSettings = true;
        }

        else if (y >= 190) {
            auto adjustVal = [](float val, float step, float minV, float maxV) -> float {
                val += step; val = round(val * 10.0f) / 10.0f;
                if (val < minV) val = minV; if (val > maxV) val = maxV; return val;
            };

            auto enforceInterlock = [&]() {
                if (_tempAlarmConfig.tempMin >= _tempAlarmConfig.tempMax) {
                    if (_editFieldFocus == 0)
                        _tempAlarmConfig.tempMax = round((_tempAlarmConfig.tempMin + 0.1f) * 10.0f) / 10.0f;
                    else
                        _tempAlarmConfig.tempMin = round((_tempAlarmConfig.tempMax - 0.1f) * 10.0f) / 10.0f;
                }
                if (hasHum && _tempAlarmConfig.humMin >= _tempAlarmConfig.humMax) {
                    if (_editFieldFocus == 2)
                        _tempAlarmConfig.humMax = round((_tempAlarmConfig.humMin + 0.1f) * 10.0f) / 10.0f;
                    else
                        _tempAlarmConfig.humMin = round((_tempAlarmConfig.humMax - 0.1f) * 10.0f) / 10.0f;
                }
                if (_tempAlarmConfig.tempMax > 150.0f) _tempAlarmConfig.tempMax = 150.0f;
                if (_tempAlarmConfig.tempMin < -50.0f) _tempAlarmConfig.tempMin = -50.0f;
                if (hasHum) {
                    if (_tempAlarmConfig.humMax > 100.0f) _tempAlarmConfig.humMax = 100.0f;
                    if (_tempAlarmConfig.humMin < 0.0f) _tempAlarmConfig.humMin = 0.0f;
                }
            };

            if (x < 70) {

                if (_lastPressedBtn != 0) { _btnHoldStartTime = millis(); _lastPressedBtn = 0; acceptTouch(10); }
                uint32_t holdTime = millis() - _btnHoldStartTime;
                float step = -0.1f; if (holdTime > 6000) step = -10.0f; else if (holdTime > 4000) step = -1.0f; else if (holdTime > 2000) step = -0.5f;
                if (_editFieldFocus == 0) _tempAlarmConfig.tempMin = adjustVal(_tempAlarmConfig.tempMin, step, -50.0f, 150.0f);
                if (_editFieldFocus == 1) _tempAlarmConfig.tempMax = adjustVal(_tempAlarmConfig.tempMax, step, -50.0f, 150.0f);
                if (_editFieldFocus == 2) _tempAlarmConfig.humMin = adjustVal(_tempAlarmConfig.humMin, step, 0.0f, 100.0f);
                if (_editFieldFocus == 3) _tempAlarmConfig.humMax = adjustVal(_tempAlarmConfig.humMax, step, 0.0f, 100.0f);
                enforceInterlock();
                _repaintSettings = true;
            }
            else if (x < 138) {

                if (_lastPressedBtn != 1) { _btnHoldStartTime = millis(); _lastPressedBtn = 1; acceptTouch(11); }
                uint32_t holdTime = millis() - _btnHoldStartTime;
                float step = 0.1f; if (holdTime > 6000) step = 10.0f; else if (holdTime > 4000) step = 1.0f; else if (holdTime > 2000) step = 0.5f;
                if (_editFieldFocus == 0) _tempAlarmConfig.tempMin = adjustVal(_tempAlarmConfig.tempMin, step, -50.0f, 150.0f);
                if (_editFieldFocus == 1) _tempAlarmConfig.tempMax = adjustVal(_tempAlarmConfig.tempMax, step, -50.0f, 150.0f);
                if (_editFieldFocus == 2) _tempAlarmConfig.humMin = adjustVal(_tempAlarmConfig.humMin, step, 0.0f, 100.0f);
                if (_editFieldFocus == 3) _tempAlarmConfig.humMax = adjustVal(_tempAlarmConfig.humMax, step, 0.0f, 100.0f);
                enforceInterlock();
                _repaintSettings = true;
            }
            else if (x < 219) {
                /* BACK: desativa o alarme e salva */
                if (!acceptTouch(12)) return;
                _lastPressedBtn = -1;
                _tempAlarmConfig.alarmsActive = false;
                if (_editSensorIdx == -1) _sysConfigPtr->ambientSensor = _tempAlarmConfig;
                else                      _sysConfigPtr->sensors[_editSensorIdx] = _tempAlarmConfig;
                UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
                ev.id = _editSensorIdx; queue_try_add(&_eventQueue, &ev);
                showSettingsAlarms(_sysConfigPtr);
            }
            else {
                /* SAVE: ativa o alarme e salva */
                if (!acceptTouch(13)) return;
                _lastPressedBtn = -1;
                _tempAlarmConfig.alarmsActive = true;
                if (_editSensorIdx == -1) _sysConfigPtr->ambientSensor = _tempAlarmConfig;
                else                      _sysConfigPtr->sensors[_editSensorIdx] = _tempAlarmConfig;
                UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
                ev.id = _editSensorIdx; queue_try_add(&_eventQueue, &ev);
                showSettingsAlarms(_sysConfigPtr);
            }
        }
    }
    else if (_uiMode == MODE_AUTH) {
        if (y > 200 && x < 120) { if (!acceptTouch(0)) return; forceDashboard(); return; }
        /* License button — accessible even during lockout */
        if (y > 200 && x > 195) { if (!acceptTouch(5)) return; _licenseFromAuth = true; showSettingsLicense(); return; }
        if (_permanentLockout || millis() < _lockoutUntil) return;
        if (y >= 80 && y <= 185) {
            int row = (y < 135) ? 0 : 1; int col = (x > 160) ? 1 : 0; int btnIdx = (row * 2) + col;
            if (!acceptTouch(1 + btnIdx)) return;
            String clickedChars = String(_keypadChars[btnIdx]); char expected = _expectedPin[_authStep];
            if (clickedChars.indexOf(expected) < 0) _isCurrentAttemptValid = false;
            _authStep++; _authFailed = false;
            if (_authStep >= _expectedPin.length()) {
                if (_isCurrentAttemptValid) {
                    _failedAttempts = 0; UiEvent ev; ev.type = UiEvent::EVT_AUTH_SUCCESS; queue_try_add(&_eventQueue, &ev); return;
                } else {
                    _authFailed = true; _failedAttempts++; _authStep = 0; _isCurrentAttemptValid = true;
                    _errorSoundPending = true;
                    if (_failedAttempts <= 2) _lockoutUntil = 0;
                    else if (_failedAttempts == 3) _lockoutUntil = millis() + 5000;
                    else if (_failedAttempts == 4) _lockoutUntil = millis() + 15000;
                    else if (_failedAttempts == 5) _lockoutUntil = millis() + 60000;
                    else { _permanentLockout = true; _lockoutUntil = millis() + 10000; }
                    _forceSettingsRedraw = true;
                }
            }
            scrambleKeys(); _repaintSettings = true;
        }
    }
    else if (_uiMode == MODE_SETTINGS_MAIN) {
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int mapIdx = (_mainMenuPage * 4) + clickedIndex;
            if (mapIdx < 7 && mapIdx != _menuSelection) {
                if (!acceptTouch(clickedIndex)) return;
                _menuSelection = mapIdx; _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptTouch(10)) return;
                if (_menuSelection > 0) _menuSelection--; else _menuSelection = 6;
                _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptTouch(11)) return;
                if (_menuSelection < 6) _menuSelection++; else _menuSelection = 0;
                _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
            }
            else if (x < 219) {
                if (!acceptTouch(12)) return;
                forceDashboard();
            }
            else {
                if (!acceptTouch(13)) return;
                UiEvent ev; ev.type = UiEvent::EVT_MENU_SELECT; ev.id = _menuSelection; queue_try_add(&_eventQueue, &ev);
            }
        }
    }
    else if (_uiMode == MODE_SETTINGS_LANG) {
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int actualIndex = (_langPage * 4) + clickedIndex;
            if (actualIndex < TOTAL_LANGS && actualIndex != _previewLangIdx) {
                if (!acceptTouch(clickedIndex)) return;
                _previewLangIdx = actualIndex;
                _langPage = _previewLangIdx / 4;
                _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptTouch(10)) return;
                if (_previewLangIdx > 0) _previewLangIdx--; else _previewLangIdx = TOTAL_LANGS - 1;
                _langPage = _previewLangIdx / 4;
                _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptTouch(11)) return;
                if (_previewLangIdx < TOTAL_LANGS - 1) _previewLangIdx++; else _previewLangIdx = 0;
                _langPage = _previewLangIdx / 4;
                _repaintSettings = true;
            }
            else if (x < 219) {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            }
            else {
                if (!acceptTouch(13)) return;
                UiEvent ev; ev.type = UiEvent::EVT_APPLY_LANG; ev.id = _previewLangIdx; queue_try_add(&_eventQueue, &ev);
            }
        }
    }


    else if (_uiMode == MODE_SETTINGS_PASSWORD) {


        if (_kbPhase >= 2) {
            if (y >= 185) {
                if (!acceptTouch(0)) return;
                if (_kbPhase == 3) {
                    showSettingsMain();
                } else {
                    _kbPhase = 0;
                    _kbCursor = 0;
                    _kbShowRaw = false;
                    memset(_kbBuffer, 0, sizeof(_kbBuffer));
                    memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
            }
            return;
        }

        char* activeBuf = (_kbPhase == 0) ? _kbBuffer : _kbConfirmBuf;


        if (y < 28 && x > 280) {
            if (!acceptTouch(1)) return;
            showSettingsMain(); return;
        }


        if (y >= 33 && y < 66) {
            if (!acceptTouch(2)) return;
            _kbShowRaw = !_kbShowRaw;
            _repaintSettings = true;
            return;
        }


        if (y >= 72 && y < 168) {
            int row = (y - 72) / 32;
            int col = (x - 1) / 32;
            if (row < 0) row = 0; if (row > 2) row = 2;
            if (col < 0) col = 0; if (col > 9) col = 9;


            if (!acceptTouch((uint8_t)(row * 10 + col + 10))) return;

            /* Update visual selection cursor */
            _kbSelRow = row;
            _kbSelCol = col;

            static const char layer0[3][10] = {
                {'q','w','e','r','t','y','u','i','o','p'},
                {'a','s','d','f','g','h','j','k','l','.'},
                {'z','x','c','v','b','n','m',',','!','?'}
            };
            static const char layer1[3][10] = {
                {'Q','W','E','R','T','Y','U','I','O','P'},
                {'A','S','D','F','G','H','J','K','L',':'},
                {'Z','X','C','V','B','N','M',';','"','\''}
            };
            static const char layer2[3][10] = {
                {'1','2','3','4','5','6','7','8','9','0'},
                {'@','#','$','%','&','*','-','+','=','~'},
                {'(',')','[',']','{','}','/','\\','^','_'}
            };

            const char (*active)[10] = (_kbLayer == 2) ? layer2
                                     : (_kbLayer == 1) ? layer1
                                     :                   layer0;

            if (_kbCursor < 7) {
                activeBuf[_kbCursor++] = active[row][col];
                activeBuf[_kbCursor] = '\0';
                if (_kbLayer == 1 && !_kbShiftLock) _kbLayer = 0;
            }
            _repaintSettings = true;
            return;
        }


        if (y >= 170 && y < 195) {
            /* Positions: Shift=1..49, 123=51..99, Space=101..219, Bksp=221..269, OK=271..319 */
            if (x < 49) {
                /* Shift */
                if (!acceptTouch(50)) return;
                if (_kbLayer == 1) {
                    _kbShiftLock = !_kbShiftLock;
                    if (!_kbShiftLock) _kbLayer = 0;
                } else {
                    _kbLayer = 1;
                    _kbShiftLock = false;
                }
                _repaintSettings = true;
            }
            else if (x < 99) {
                /* 123 */
                if (!acceptTouch(51)) return;
                _kbLayer = (_kbLayer == 2) ? 0 : 2;
                _kbShiftLock = false;
                _repaintSettings = true;
            }
            else if (x < 219) {
                /* Space */
                if (!acceptTouch(52)) return;
                if (_kbCursor < 7) {
                    activeBuf[_kbCursor++] = ' ';
                    activeBuf[_kbCursor] = '\0';
                }
                _repaintSettings = true;
            }
            else if (x < 269) {
                /* Backspace */
                if (!acceptTouch(53)) return;
                if (_kbCursor > 0) {
                    activeBuf[--_kbCursor] = '\0';
                }
                _repaintSettings = true;
            }
            else {
                /* OK — same confirmation logic */
                if (!acceptTouch(54)) return;
                if (_kbPhase == 0) {
                    if (_kbCursor < 4) {
                        _kbPhase = 2;
                        _kbMsgKey = TR_PWD_TOO_SHORT;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    } else {
                        _kbPhase = 1;
                        _kbCursor = 0;
                        _kbShowRaw = false;
                        memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
                        /* Partial redraw: title and boxes change, keys do not */
                        _repaintSettings = true;
                    }
                }
                else if (_kbPhase == 1) {
                    if (_kbCursor < 4) {
                        _kbPhase = 2;
                        _kbMsgKey = TR_PWD_TOO_SHORT;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    }
                    else if (strcmp(_kbBuffer, _kbConfirmBuf) != 0) {
                        _kbPhase = 2;
                        _kbMsgKey = TR_PWD_MISMATCH;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    }
                    else {
                        _kbPhase = 3;
                        _kbMsgKey = TR_PWD_SAVED;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;

                        UiEvent ev;
                        ev.type = UiEvent::EVT_SAVE_PASSWORD;
                        ev.id = 0;
                        ev.param = 0;
                        queue_try_add(&_eventQueue, &ev);
                    }
                }
            }
            return;
        }


        if (y >= 195) {
            int btnW = 58; int bGap = 5; int bStartX = 5;
            int btnIdx = (x - bStartX) / (btnW + bGap);
            if (btnIdx < 0) btnIdx = 0;
            if (btnIdx > 4) btnIdx = 4;
            /* Check if touch is inside button (not in gap) */
            int btnX = bStartX + btnIdx * (btnW + bGap);
            if (x < btnX || x > btnX + btnW) return;

            /* Column limits: row 3 (bar) has 5 items, rows 0-2 have 10 */
            int maxCol = (_kbSelRow == 3) ? 4 : 9;

            if (btnIdx == 0) {
                /* ◄ Esquerda */
                if (!acceptTouch(60)) return;
                _kbSelCol--;
                if (_kbSelCol < 0) _kbSelCol = maxCol;
                _repaintSettings = true;
            }
            else if (btnIdx == 1) {
                /* ► Right */
                if (!acceptTouch(61)) return;
                _kbSelCol++;
                if (_kbSelCol > maxCol) _kbSelCol = 0;
                _repaintSettings = true;
            }
            else if (btnIdx == 2) {
                /* ▲ Cima */
                if (!acceptTouch(62)) return;
                _kbSelRow--;
                if (_kbSelRow < 0) _kbSelRow = 3;
                /* Ajustar coluna ao trocar para/da barra */
                if (_kbSelRow == 3 && _kbSelCol > 4) _kbSelCol = 4;
                _repaintSettings = true;
            }
            else if (btnIdx == 3) {
                /* ▼ Baixo */
                if (!acceptTouch(63)) return;
                _kbSelRow++;
                if (_kbSelRow > 3) _kbSelRow = 0;
                /* Ajustar coluna ao trocar para/da barra */
                if (_kbSelRow == 3 && _kbSelCol > 4) _kbSelCol = 4;
                _repaintSettings = true;
            }
            else if (btnIdx == 4) {
                /* ✓ Confirm selection */
                if (!acceptTouch(64)) return;

                if (_kbSelRow == 3) {
                    /*
                     * Action bar: execute the action of the selected item.
                     * 0=Shift, 1=123, 2=Space, 3=Backspace, 4=OK
                     */
                    if (_kbSelCol == 0) {
                        /* Shift */
                        if (_kbLayer == 1) {
                            _kbShiftLock = !_kbShiftLock;
                            if (!_kbShiftLock) _kbLayer = 0;
                        } else {
                            _kbLayer = 1;
                            _kbShiftLock = false;
                        }
                    }
                    else if (_kbSelCol == 1) {
                        /* 123 */
                        _kbLayer = (_kbLayer == 2) ? 0 : 2;
                        _kbShiftLock = false;
                    }
                    else if (_kbSelCol == 2) {
                        /* Space */
                        if (_kbCursor < 7) {
                            activeBuf[_kbCursor++] = ' ';
                            activeBuf[_kbCursor] = '\0';
                        }
                    }
                    else if (_kbSelCol == 3) {
                        /* Backspace */
                        if (_kbCursor > 0) {
                            activeBuf[--_kbCursor] = '\0';
                        }
                    }
                    else if (_kbSelCol == 4) {
                        /* OK — password confirmation */
                        if (_kbPhase == 0) {
                            if (_kbCursor < 4) {
                                _kbPhase = 2;
                                _kbMsgKey = TR_PWD_TOO_SHORT;
                                _forceSettingsRedraw = true;
                            } else {
                                _kbPhase = 1;
                                _kbCursor = 0;
                                _kbShowRaw = false;
                                memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
                            }
                        }
                        else if (_kbPhase == 1) {
                            if (_kbCursor < 4) {
                                _kbPhase = 2;
                                _kbMsgKey = TR_PWD_TOO_SHORT;
                                _forceSettingsRedraw = true;
                            }
                            else if (strcmp(_kbBuffer, _kbConfirmBuf) != 0) {
                                _kbPhase = 2;
                                _kbMsgKey = TR_PWD_MISMATCH;
                                _forceSettingsRedraw = true;
                            }
                            else {
                                _kbPhase = 3;
                                _kbMsgKey = TR_PWD_SAVED;
                                _forceSettingsRedraw = true;
                                UiEvent ev;
                                ev.type = UiEvent::EVT_SAVE_PASSWORD;
                                ev.id = 0; ev.param = 0;
                                queue_try_add(&_eventQueue, &ev);
                            }
                        }
                    }
                } else {
                    /* Fila de teclas (0-2): insere o caractere selecionado */
                    static const char lay0[3][10] = {
                        {'q','w','e','r','t','y','u','i','o','p'},
                        {'a','s','d','f','g','h','j','k','l','.'},
                        {'z','x','c','v','b','n','m',',','!','?'}
                    };
                    static const char lay1[3][10] = {
                        {'Q','W','E','R','T','Y','U','I','O','P'},
                        {'A','S','D','F','G','H','J','K','L',':'},
                        {'Z','X','C','V','B','N','M',';','"','\''}
                    };
                    static const char lay2[3][10] = {
                        {'1','2','3','4','5','6','7','8','9','0'},
                        {'@','#','$','%','&','*','-','+','=','~'},
                        {'(',')','[',']','{','}','/','\\','^','_'}
                    };
                    const char (*sel)[10] = (_kbLayer == 2) ? lay2
                                          : (_kbLayer == 1) ? lay1
                                          :                   lay0;
                    if (_kbCursor < 7) {
                        activeBuf[_kbCursor++] = sel[_kbSelRow][_kbSelCol];
                        activeBuf[_kbCursor] = '\0';
                        if (_kbLayer == 1 && !_kbShiftLock) _kbLayer = 0;
                    }
                }
                _repaintSettings = true;
            }
            return;
        }
    }


    else if (_uiMode == MODE_SETTINGS_SOUNDS) {


        if (_inMelodySelect) {
            const int TOTAL_VARIANTS = 6;
            int melPage = _melSelectIdx / 4;

            if (y >= 40 && y <= 185) {
                int clickedIndex = 0;
                if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1;
                else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
                int mapIdx = (melPage * 4) + clickedIndex;
                if (mapIdx >= TOTAL_VARIANTS) return;
                if (!acceptTouch(0x80 + clickedIndex)) return;

                _melSelectIdx = (uint8_t)mapIdx;
                SoundEvent evType = SND_NONE;
                switch (_melSelectType) {
                    case 0: evType = SND_TOUCH_CLICK; break;
                    case 1: evType = SND_CONFIRM;     break;
                    case 2: evType = SND_ERROR;        break;
                    case 3: evType = SND_ALARM_START;  break;
                }
                if (evType != SND_NONE) {
                    _previewType    = (uint8_t)evType;
                    _previewMelIdx  = _melSelectIdx;
                    _previewPending = true;
                }
                _repaintSettings = true;
            }
            else if (y > 185) {
                if (x < 70) {
                    if (!acceptTouch(0x90)) return;
                    _melSelectIdx = (_melSelectIdx > 0) ? _melSelectIdx - 1 : TOTAL_VARIANTS - 1;
                    SoundEvent evType = SND_NONE;
                    switch (_melSelectType) {
                        case 0: evType = SND_TOUCH_CLICK; break;
                        case 1: evType = SND_CONFIRM;     break;
                        case 2: evType = SND_ERROR;        break;
                        case 3: evType = SND_ALARM_START;  break;
                    }
                    if (evType != SND_NONE) {
                        _previewType = (uint8_t)evType;
                        _previewMelIdx = _melSelectIdx;
                        _previewPending = true;
                    }
                    _repaintSettings = true;
                }
                else if (x < 138) {
                    if (!acceptTouch(0x91)) return;
                    _melSelectIdx = (_melSelectIdx < TOTAL_VARIANTS - 1) ? _melSelectIdx + 1 : 0;
                    SoundEvent evType = SND_NONE;
                    switch (_melSelectType) {
                        case 0: evType = SND_TOUCH_CLICK; break;
                        case 1: evType = SND_CONFIRM;     break;
                        case 2: evType = SND_ERROR;        break;
                        case 3: evType = SND_ALARM_START;  break;
                    }
                    if (evType != SND_NONE) {
                        _previewType = (uint8_t)evType;
                        _previewMelIdx = _melSelectIdx;
                        _previewPending = true;
                    }
                    _repaintSettings = true;
                }
                else if (x < 219) {
                    if (!acceptTouch(0x92)) return;
                    _inMelodySelect = false;
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
                else {
                    if (!acceptTouch(0x93)) return;
                    switch (_melSelectType) {
                        case 0:
                            _soundSettings.touchEnabled  = true;
                            _soundSettings.touchMelody   = _melSelectIdx;
                            break;
                        case 1:
                            _soundSettings.confirmEnabled = true;
                            _soundSettings.confirmMelody  = _melSelectIdx;
                            break;
                        case 2:
                            _soundSettings.errorEnabled  = true;
                            _soundSettings.errorMelody   = _melSelectIdx;
                            break;
                        case 3:
                            _soundSettings.alarmEnabled  = true;
                            _soundSettings.alarmMelody   = _melSelectIdx;
                            break;
                    }
                    _previewType    = (uint8_t)SND_CONFIRM;
                    _previewMelIdx  = _soundSettings.confirmMelody;
                    _previewPending = true;

                    _inMelodySelect = false;
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
            }
            return;
        }


        const int TOTAL_SOUND_ITEMS = 8;
        int soundPage = _soundSelection / 4;

        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1;
            else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int mapIdx = (soundPage * 4) + clickedIndex;
            if (mapIdx >= TOTAL_SOUND_ITEMS) return;
            if (!acceptTouch(clickedIndex)) return;

            if (mapIdx != _soundSelection) {
                _soundSelection = mapIdx;
                _repaintSettings = true;
            } else {
                if (mapIdx <= 3) {
                    bool* enablePtr = nullptr;
                    uint8_t melType = 0;
                    uint8_t curMel  = 0;
                    switch (mapIdx) {
                        case 0: enablePtr = &_soundSettings.touchEnabled;
                                melType = 0; curMel = _soundSettings.touchMelody;   break;
                        case 1: enablePtr = &_soundSettings.confirmEnabled;
                                melType = 1; curMel = _soundSettings.confirmMelody; break;
                        case 2: enablePtr = &_soundSettings.errorEnabled;
                                melType = 2; curMel = _soundSettings.errorMelody;   break;
                        case 3: enablePtr = &_soundSettings.alarmEnabled;
                                melType = 3; curMel = _soundSettings.alarmMelody;   break;
                    }

                    if (enablePtr && *enablePtr) {
                        *enablePtr = false;
                        _repaintSettings = true;
                    } else {
                        _inMelodySelect = true;
                        _melSelectType  = melType;
                        _melSelectIdx   = curMel;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    }
                }
                else if (mapIdx == 4) {
                    _soundSettings.webEnabled = !_soundSettings.webEnabled;
                    _repaintSettings = true;
                }
                else if (mapIdx == 5) {
                    _soundSettings.muted = !_soundSettings.muted;
                    _repaintSettings = true;
                }
                else if (mapIdx == 6) {
                    if (x < 160) { if (_soundSettings.volume >= 10) _soundSettings.volume -= 10; }
                    else          { if (_soundSettings.volume <= 90) _soundSettings.volume += 10; }
                    _touchSoundPending       = false;
                    _volumePreviewPending    = true;
                    _volumePreviewLevel      = _soundSettings.volume;
                    _repaintSettings = true;
                }
                else if (mapIdx == 7) {
                    if (x < 160) { if (_soundSettings.alarmVolume >= 10) _soundSettings.alarmVolume -= 10; }
                    else          { if (_soundSettings.alarmVolume <= 90) _soundSettings.alarmVolume += 10; }
                    _touchSoundPending          = false;
                    _alarmVolPreviewPending      = true;
                    _alarmVolPreviewLevel        = _soundSettings.alarmVolume;
                    _repaintSettings = true;
                }
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptTouch(10)) return;
                if (_soundSelection > 0) _soundSelection--; else _soundSelection = TOTAL_SOUND_ITEMS - 1;
                _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptTouch(11)) return;
                if (_soundSelection < TOTAL_SOUND_ITEMS - 1) _soundSelection++; else _soundSelection = 0;
                _repaintSettings = true;
            }
            else if (x < 219) {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            }
            else {
                if (!acceptTouch(13)) return;
                UiEvent ev; ev.type = UiEvent::EVT_SAVE_SOUNDS; ev.id = 0; ev.param = 0;
                queue_try_add(&_eventQueue, &ev);
            }
        }
    }


    else if (_uiMode == MODE_SETTINGS_LICENSE) {

        if (y >= 32 && y <= 189) {
            /* Touch on text area: upper half = previous page, lower = next page */
            if (y < 110) {
                if (!acceptTouch(0)) return;
                if (_licensePage > 0) _licensePage--;
            } else {
                if (!acceptTouch(1)) return;
                if (_licensePage < _licenseTotalPages - 1) _licensePage++;
            }
            _repaintSettings = true;
        }
        else if (y > 190) {
            if (x < 107) {
                if (!acceptTouch(10)) return;
                if (_licensePage > 0) _licensePage--;
                _repaintSettings = true;
            }
            else if (x < 213) {
                if (!acceptTouch(11)) return;
                if (_licensePage < _licenseTotalPages - 1) _licensePage++;
                _repaintSettings = true;
            }
            else {
                if (!acceptTouch(12)) return;
                if (_licenseFromAuth) {
                    _licenseFromAuth = false;
                    _uiMode = MODE_AUTH;
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                } else {
                    showSettingsMain();
                }
            }
        }
    }


    else if (_uiMode == MODE_ALARM_ACTION) {
        if (y >= 60 && y <= 105) {
            if (!acceptTouch(0)) return;
            UiEvent ev;
            ev.type = UiEvent::EVT_ALARM_SILENCE;
            ev.id   = _alarmActionSlot;
            ev.param = 120;
            queue_try_add(&_eventQueue, &ev);
        }
        else if (y >= 115 && y <= 160) {
            if (!acceptTouch(1)) return;
            UiEvent ev;
            ev.type = UiEvent::EVT_ALARM_DEACTIVATE;
            ev.id   = _alarmActionSlot;
            ev.param = 0;
            queue_try_add(&_eventQueue, &ev);
        }
        else if (y >= 170 && y <= 215) {
            if (!acceptTouch(2)) return;
            /* Voltar ao dashboard com o painel em modo min/max */
            if (_alarmActionSlot < 0) {
                _ambientShowMinMax = true;
            } else {
                _slotShowMinMax = true;
            }
            _uiMode = MODE_DASHBOARD;
            _forceFullRedraw = true;
            mutex_enter_blocking(&_stateMutex);
            _isDirty = true;
            mutex_exit(&_stateMutex);
        }
    }
}

void DisplayManager::showSettingsThemes(int currentThemeIdx) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_THEMES;
    _previewThemeIdx = currentThemeIdx;
    _themePage = currentThemeIdx / 4;
    _forceSettingsRedraw = true; _lastThemePage = -1; _lastPreviewThemeIdx = -1; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsThemes() {
    if(!_canvasWide) return;
    bool fullRedraw = _forceSettingsRedraw;
    bool pageChanged = (_themePage != _lastThemePage);
    int totalThemes = getThemeCount();
    int totalPages = (totalThemes + 3) / 4;
    if (_themePage >= totalPages) _themePage = totalPages - 1;
    if (_themePage < 0) _themePage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_CONFIG_THEMES));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw)/2, btnY + 25); _tft->print(backTxt);
        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String appTxt = tr(TR_APPLY);
        _tft->getTextBounds(appTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw)/2, btnY + 25); _tft->print(appTxt);
    }

    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
        int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
        int thumbY = trackY; if (totalPages > 1) { thumbY += (_themePage * (trackH - thumbH)) / (totalPages - 1); }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }

    int startIdx = _themePage * 4;
    int yBase = 40; int itemW = 285;
    for (int i = 0; i < 4; i++) {
        int actualIdx = startIdx + i; int y = yBase + (i * 38);
        if (!fullRedraw && !pageChanged) { if (actualIdx != _previewThemeIdx && actualIdx != _lastPreviewThemeIdx) continue; }
        _canvasWide->fillScreen(C_BG_MAIN);
        if (actualIdx < totalThemes) {
            bool isSelected = (actualIdx == _previewThemeIdx);
            uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
            _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24); _canvasWide->print(availableThemes[actualIdx].displayName);
            int pX = itemW - 55; int pY = 9;
            _canvasWide->fillRect(pX, pY, 16, 16, availableThemes[actualIdx].bgMain);
            _canvasWide->fillRect(pX + 16, pY, 16, 16, availableThemes[actualIdx].cardBg);
            _canvasWide->fillRect(pX + 32, pY, 16, 16, availableThemes[actualIdx].accent);
            if (isSelected) _canvasWide->drawRect(pX-1, pY-1, 49, 18, C_BG_MAIN); else _canvasWide->drawRect(pX-1, pY-1, 49, 18, C_TEXT_SUB);
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }
    _forceSettingsRedraw = false; _lastThemePage = _themePage; _lastPreviewThemeIdx = _previewThemeIdx;
}

void DisplayManager::showSettingsAlarms(SystemConfig* cfg) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_ALARMS; _sysConfigPtr = cfg; _activeSensorCount = 0;
    if (cfg->ambientSensor.active) { _activeSensorsMap[_activeSensorCount++] = -1; }
    for(int i = 0; i < MAX_SENSORS; i++) { if(cfg->sensors[i].active) { _activeSensorsMap[_activeSensorCount++] = i; } }
    _alarmSelection = 0; _alarmPage = 0; _lastAlarmSelection = -1; _forceSettingsRedraw = true; _lastAlarmPage = -1; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsAlarms() {
    if(!_canvasWide) return;
    bool fullRedraw = _forceSettingsRedraw; bool pageChanged = (_alarmPage != _lastAlarmPage);
    int totalPages = (_activeSensorCount + 3) / 4; if (totalPages == 0) totalPages = 1;
    if (_alarmPage >= totalPages) _alarmPage = totalPages - 1; if (_alarmPage < 0) _alarmPage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_ALARMS_TITLE));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
        /* EXIT button occupies full remaining width */
        _tft->fillRoundRect(141, btnY, 174, btnH, 8, C_ACCENT);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_BG_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (174 - bw)/2, btnY + 25); _tft->print(backTxt);
    }

    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
        int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
        int thumbY = trackY; if (totalPages > 1) { thumbY += (_alarmPage * (trackH - thumbH)) / (totalPages - 1); }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }

    int startIdx = _alarmPage * 4; int yBase = 40; int itemW = 285;
    for (int i = 0; i < 4; i++) {
        int y = yBase + (i * 38); int mapIdx = startIdx + i;

        /* Only redraws items that changed selection state or on fullRedraw/pageChanged */
        if (!fullRedraw && !pageChanged) {
            if (mapIdx != _alarmSelection && mapIdx != _lastAlarmSelection) continue;
        }

        _canvasWide->fillScreen(C_BG_MAIN);
        if (mapIdx < _activeSensorCount) {
            int actualSensorId = _activeSensorsMap[mapIdx];
            SensorRecord* rec = (actualSensorId == -1) ? &_sysConfigPtr->ambientSensor : &_sysConfigPtr->sensors[actualSensorId];
            bool isSelected = (mapIdx == _alarmSelection);
            uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

            /* Measure YES/NO indicator width to reserve space */
            const char* statusTxt = rec->alarmsActive ? tr(TR_ON) : tr(TR_OFF);
            _canvasWide->setFont(&FreeSansBold9pt7b);
            int16_t sx1, sy1; uint16_t sw, sh;
            _canvasWide->getTextBounds(statusTxt, 0, 0, &sx1, &sy1, &sw, &sh);
            int statusAreaW = (int)sw + 20;  /* margem de 10px de cada lado */

            /* Sensor name — truncated if needed to avoid collision */
            int maxNameW = itemW - statusAreaW - 15;
            char nameBuf[40];
            truncateText(_canvasWide, rec->friendlyName, nameBuf, sizeof(nameBuf), maxNameW);
            _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24);
            _canvasWide->print(nameBuf);

            /* Indicador SIM/NAO alinhado on the right */
            uint16_t statusColor;
            if (isSelected) {
                statusColor = C_BG_MAIN;
            } else {
                statusColor = rec->alarmsActive ? C_TEMP_OK : C_TEXT_OFF;
            }
            _canvasWide->setTextColor(statusColor);
            _canvasWide->setCursor(itemW - 10 - (int)sw, 24);
            _canvasWide->print(statusTxt);
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }
    _forceSettingsRedraw = false; _lastAlarmPage = _alarmPage; _lastAlarmSelection = _alarmSelection;
}

void DisplayManager::showAlarmEdit(int sensorIdx) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_ALARM_EDIT; _editSensorIdx = sensorIdx;
    if (sensorIdx == -1) { _tempAlarmConfig = _sysConfigPtr->ambientSensor; } else { _tempAlarmConfig = _sysConfigPtr->sensors[sensorIdx]; }
    _editFieldFocus = 0; _forceSettingsRedraw = true; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawAlarmEdit() {
    bool hasHum = (_editSensorIdx == -1 || _tempAlarmConfig.rom[0] != 0x28);


    if (_tempAlarmConfig.tempMin >= _tempAlarmConfig.tempMax) {
        _tempAlarmConfig.tempMax = _tempAlarmConfig.tempMin + 0.1f;
        _tempAlarmConfig.tempMax = round(_tempAlarmConfig.tempMax * 10.0f) / 10.0f;
    }
    if (hasHum && _tempAlarmConfig.humMin >= _tempAlarmConfig.humMax) {
        _tempAlarmConfig.humMax = _tempAlarmConfig.humMin + 0.1f;
        _tempAlarmConfig.humMax = round(_tempAlarmConfig.humMax * 10.0f) / 10.0f;
        if (_tempAlarmConfig.humMax > 100.0f) {
            _tempAlarmConfig.humMax = 100.0f;
            _tempAlarmConfig.humMin = 99.9f;
        }
    }

    if (_forceSettingsRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        int16_t tx1, ty1; uint16_t tw, th;
        String titleTxt = String(_tempAlarmConfig.friendlyName);
        _tft->getTextBounds(titleTxt, 0, 0, &tx1, &ty1, &tw, &th);
        _tft->setCursor((320 - tw) / 2, 22); _tft->print(titleTxt);
        _tft->setTextColor(C_TEXT_SUB); _tft->setCursor(10, 52); _tft->print(tr(TR_TEMP));
        if (hasHum) { _tft->setCursor(10, 122); _tft->print(tr(TR_HUMIDITY)); }
        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 26, 26, btnY + 12, 46, btnY + 12, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 12, 94, btnY + 26, 114, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw)/2, btnY + 25); _tft->print(backTxt);
        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String saveTxt = tr(TR_SAVE);
        _tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw)/2, btnY + 25); _tft->print(saveTxt);
        _forceSettingsRedraw = false;
    }
    auto drawBox = [&](int fieldId, int x, int y, const char* label, float val, bool isHum) {
        _canvasSmall->fillScreen(C_BG_MAIN);
        bool focused = (_editFieldFocus == fieldId);
        uint16_t bg = focused ? C_ACCENT : C_CARD_BG;
        uint16_t txt = focused ? C_BG_MAIN : C_TEXT_MAIN;
        _canvasSmall->fillRoundRect(0, 0, 140, 40, 10, bg);
        if (!focused) _canvasSmall->drawRoundRect(0, 0, 140, 40, 10, C_TEXT_SUB);
        _canvasSmall->setFont(&FreeSansBold9pt7b); _canvasSmall->setTextColor(focused ? C_BG_MAIN : C_TEXT_SUB);
        _canvasSmall->setCursor(8, 17); _canvasSmall->print(label);
        _canvasSmall->setFont(&FreeSansBold12pt7b); _canvasSmall->setTextColor(txt);
        char intPart[8]; char decPart[4];
        if (val < 0 && val > -1.0) { snprintf(intPart, sizeof(intPart), "-0"); } else { snprintf(intPart, sizeof(intPart), "%d", (int)val); }
        int fractional = abs((int)round(val * 10.0f) % 10);
        snprintf(decPart, sizeof(decPart), ".%d", fractional);
        int textAnchor = 98; int16_t bx, by; uint16_t bw, bh;
        _canvasSmall->getTextBounds(intPart, 0, 0, &bx, &by, &bw, &bh);
        _canvasSmall->setCursor(textAnchor - bw, 32); _canvasSmall->print(intPart);
        _canvasSmall->setCursor(textAnchor, 32); _canvasSmall->print(decPart);
        _canvasSmall->setFont(NULL); _canvasSmall->setCursor(122, 20);
        if (isHum) _canvasSmall->print("%"); else _canvasSmall->print("C");
        blitCanvas(_canvasSmall, x, y, 140, 40);
    };
    drawBox(0, 10,  60, "MIN", _tempAlarmConfig.tempMin, false); drawBox(1, 160, 60, "MAX", _tempAlarmConfig.tempMax, false);
    if (hasHum) { drawBox(2, 10,  130, "MIN", _tempAlarmConfig.humMin, true); drawBox(3, 160, 130, "MAX", _tempAlarmConfig.humMax, true); }
}

void DisplayManager::showSettingsMain() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_MAIN; _menuSelection = 0; _mainMenuPage = 0; _lastMainMenuPage = -1;
    _forceSettingsRedraw = true; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsMain() {
    if(!_canvasWide) return;
    bool fullRedraw = _forceSettingsRedraw; bool pageChanged = (_mainMenuPage != _lastMainMenuPage);
    const int TOTAL_ITEMS = 7; LangKey menuItems[] = {TR_MENU_THEMES, TR_MENU_ALARMS, TR_MENU_SOUNDS, TR_MENU_LANG, TR_MENU_PASSWORD, TR_MENU_TOUCH_CAL, TR_MENU_LICENSE};
    int totalPages = (TOTAL_ITEMS + 3) / 4; if (totalPages == 0) totalPages = 1;
    if (_mainMenuPage >= totalPages) _mainMenuPage = totalPages - 1; if (_mainMenuPage < 0) _mainMenuPage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_CONFIG_MAIN));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw)/2, btnY + 25); _tft->print(backTxt);
        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String enterTxt = tr(TR_ENTER);
        _tft->getTextBounds(enterTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw)/2, btnY + 25); _tft->print(enterTxt);
    }

    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
        int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
        int thumbY = trackY; if (totalPages > 1) { thumbY += (_mainMenuPage * (trackH - thumbH)) / (totalPages - 1); }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }

    int startIdx = _mainMenuPage * 4; int yBase = 40; int itemW = 285;
    for (int i = 0; i < 4; i++) {
        int y = yBase + (i * 38); int mapIdx = startIdx + i;
        _canvasWide->fillScreen(C_BG_MAIN);
        if (mapIdx < TOTAL_ITEMS) {
            bool isSelected = (mapIdx == _menuSelection);
            uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
            _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24); _canvasWide->print(tr(menuItems[mapIdx]));
            _canvasWide->fillTriangle(itemW - 20, 11, itemW - 20, 23, itemW - 10, 17, isSelected ? C_BG_MAIN : C_TEXT_SUB);
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }
    _forceSettingsRedraw = false; _lastMainMenuPage = _mainMenuPage;
}

void DisplayManager::showSettingsLang(int currentLang) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_LANG;
    _previewLangIdx = currentLang;
    _langPage = currentLang / 4;
    _forceSettingsRedraw = true;
    _lastLangPage = -1;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsLang() {
    if (!_canvasWide) return;

    bool fullRedraw  = _forceSettingsRedraw;
    bool pageChanged = (_langPage != _lastLangPage);


    int totalPages = (TOTAL_LANGS + 3) / 4;
    if (_langPage >= totalPages) _langPage = totalPages - 1;
    if (_langPage < 0) _langPage = 0;


    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);


        _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22);
        _tft->print(tr(TR_CONFIG_LANG));


        int btnY = 195; int btnH = 40;
        int16_t bx, by; uint16_t bw, bh;


        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);


        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);


        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b);
        _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw) / 2, btnY + 25);
        _tft->print(backTxt);


        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String appTxt = tr(TR_APPLY);
        _tft->getTextBounds(appTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw) / 2, btnY + 25);
        _tft->print(appTxt);
    }


    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40;
        int trackW = 8;   int trackH = 146;

        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);

        int thumbH = trackH / totalPages;
        if (thumbH < 20) thumbH = 20;
        int thumbY = trackY;
        if (totalPages > 1) {
            thumbY += (_langPage * (trackH - thumbH)) / (totalPages - 1);
        }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }


    int startIdx = _langPage * 4;
    int yBase    = 40;
    int itemW    = 285;

    for (int i = 0; i < 4; i++) {
        int actualIdx = startIdx + i;
        int y = yBase + (i * 38);


        if (!fullRedraw && !pageChanged) {
            if (actualIdx != _previewLangIdx && actualIdx != _lastPreviewLangIdx) continue;
        }

        _canvasWide->fillScreen(C_BG_MAIN);

        if (actualIdx < TOTAL_LANGS) {
            bool isSelected = (actualIdx == _previewLangIdx);
            uint16_t bg  = isSelected ? C_ACCENT  : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;


            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);


            _canvasWide->setFont(&FreeSansBold9pt7b);
            _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24);
            _canvasWide->print(LANG_NAMES[actualIdx]);


            _canvasWide->setCursor(itemW - 35, 24);
            _canvasWide->print(LANG_FLAGS[actualIdx]);
        }

        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }


    _forceSettingsRedraw = false;
    _lastLangPage = _langPage;
    _lastPreviewLangIdx = _previewLangIdx;
}


void DisplayManager::showSettingsPassword() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_PASSWORD;
    _kbLayer = 0;
    _kbShiftLock = false;
    _kbCursor = 0;
    _kbShowRaw = false;
    _kbPhase = 0;
    _kbMsgKey = TR_KEYS_COUNT;
    _kbSelRow = 0;
    _kbSelCol = 0;
    memset(_kbBuffer, 0, sizeof(_kbBuffer));
    memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
    _forceSettingsRedraw = true;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}


void DisplayManager::getNewPassword(char* out, size_t maxLen) const {
    strncpy(out, _kbBuffer, maxLen - 1);
    out[maxLen - 1] = '\0';
}


void DisplayManager::drawPasswordMessage() {
    if (!_tft) return;
    int16_t x1, y1; uint16_t w, h_bound;

    _tft->fillScreen(C_BG_MAIN);


    bool isSuccess = (_kbPhase == 3);
    uint16_t iconColor = isSuccess ? C_TEMP_OK : C_TEMP_WARM;

    if (isSuccess) {

        _tft->drawLine(130, 90, 150, 110, iconColor);
        _tft->drawLine(131, 90, 151, 110, iconColor);
        _tft->drawLine(150, 110, 190, 70, iconColor);
        _tft->drawLine(151, 110, 191, 70, iconColor);
    } else {

        _tft->drawLine(145, 70, 175, 100, iconColor);
        _tft->drawLine(146, 70, 176, 100, iconColor);
        _tft->drawLine(175, 70, 145, 100, iconColor);
        _tft->drawLine(176, 70, 146, 100, iconColor);
    }


    const char* msg = (_kbMsgKey < TR_KEYS_COUNT) ? tr(_kbMsgKey) : "Error";
    _tft->setFont(&FreeSansBold9pt7b);
    _tft->setTextColor(C_TEXT_MAIN);
    _tft->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor((320 - w) / 2, 130);
    _tft->print(msg);


    _tft->fillRoundRect(60, 185, 200, 40, 12, C_ACCENT);
    _tft->setFont(&FreeSansBold12pt7b);
    _tft->setTextColor(C_BG_MAIN);
    const char* btnLabel = tr(TR_UNDERSTOOD);
    _tft->getTextBounds(btnLabel, 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor(160 - (w / 2), 212);
    _tft->print(btnLabel);
}

void DisplayManager::drawSettingsPassword() {
    if (!_tft) return;


    if (_kbPhase >= 2) {
        drawPasswordMessage();
        _forceSettingsRedraw = false;
        return;
    }


    static const char layer0[3][10] = {
        {'q','w','e','r','t','y','u','i','o','p'},
        {'a','s','d','f','g','h','j','k','l','.'},
        {'z','x','c','v','b','n','m',',','!','?'}
    };
    static const char layer1[3][10] = {
        {'Q','W','E','R','T','Y','U','I','O','P'},
        {'A','S','D','F','G','H','J','K','L',':'},
        {'Z','X','C','V','B','N','M',';','"','\''}
    };
    static const char layer2[3][10] = {
        {'1','2','3','4','5','6','7','8','9','0'},
        {'@','#','$','%','&','*','-','+','=','~'},
        {'(',')','[',']','{','}','/','\\','^','_'}
    };


    const char (*activeLayer)[10] = (_kbLayer == 2) ? layer2
                                  : (_kbLayer == 1) ? layer1
                                  :                   layer0;


    char* activeBuf = (_kbPhase == 0) ? _kbBuffer : _kbConfirmBuf;

    int16_t x1, y1; uint16_t w, h_bound;
    bool fullRedraw = _forceSettingsRedraw;


    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
    }

    /* Title — always redrawn via canvas (changes between phases) */
    {
        /* Barra de ponta a ponta sem cantos arredondados */
        _canvasWide->fillScreen(C_CARD_BG);
        _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(C_TEXT_MAIN);
        _canvasWide->setCursor(14, 18);
        _canvasWide->print((_kbPhase == 0) ? tr(TR_NEW_PASSWORD) : tr(TR_CONFIRM_PASSWORD));

        /* X button overlaid on bar */
        _canvasWide->fillRoundRect(282, 2, 30, 22, 4, C_TEMP_WARM);
        _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(C_BG_MAIN);
        _canvasWide->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
        _canvasWide->setCursor(297 - w / 2, 18); _canvasWide->print("X");

        blitCanvas(_canvasWide, 0, 0, 320, 26);
    }


    {
        const int MAX_BOXES = 7;
        const int MIN_BOXES = 4;
        const int boxW = 32, boxH = 28, gap = 6;
        const int startY = 33;
        const int stripH = boxH + 10;

        /*
         * Visible boxes count: in phase 0 (typing), shows maximum
         * between MIN_BOXES and (cursor + 1), up to MAX_BOXES.
         * In phase 1 (confirmation), shows exactly the password length
         * already defined in _kbBuffer.
         */
        int visibleBoxes;
        if (_kbPhase == 1) {
            visibleBoxes = (int)strlen(_kbBuffer);
        } else {
            visibleBoxes = _kbCursor + 1;
            if (visibleBoxes < MIN_BOXES) visibleBoxes = MIN_BOXES;
            if (visibleBoxes > MAX_BOXES) visibleBoxes = MAX_BOXES;
        }

        int totalW = visibleBoxes * boxW + (visibleBoxes - 1) * gap;
        int startX = (320 - totalW) / 2;

        /* Desenha boxes + contador em canvas para evitar flicker */
        _canvasWide->fillScreen(C_BG_MAIN);

        for (int i = 0; i < visibleBoxes; i++) {
            int bx = startX + i * (boxW + gap);
            bool filled = (i < _kbCursor);
            bool isRequired = (i < MIN_BOXES);

            /* Box arredondado */
            _canvasWide->fillRoundRect(bx, 0, boxW, boxH, 4, C_CARD_BG);

            /* Borda com cor condicional */
            uint16_t borderColor = isRequired ? C_ACCENT_HIGH : C_TEXT_OFF;
            if (i == _kbCursor && _kbCursor < visibleBoxes) borderColor = C_ACCENT;
            _canvasWide->drawRoundRect(bx, 0, boxW, boxH, 4, borderColor);

            if (filled) {
                if (_kbShowRaw) {
                    /* Mostra caractere real */
                    _canvasWide->setFont(&FreeSansBold9pt7b);
                    _canvasWide->setTextColor(C_TEXT_MAIN);
                    char ch[2] = { activeBuf[i], '\0' };
                    int16_t cx1, cy1; uint16_t cw, ch1;
                    _canvasWide->getTextBounds(ch, 0, 0, &cx1, &cy1, &cw, &ch1);
                    _canvasWide->setCursor(bx + (boxW - cw) / 2, 20);
                    _canvasWide->print(ch);
                } else {
                    /* Mostra bolinha mascarada */
                    _canvasWide->fillCircle(bx + boxW / 2, boxH / 2, 5, C_TEXT_MAIN);
                }
            }
        }

        /* Contador abaixo dos boxes */
        char countBuf[8];
        snprintf(countBuf, sizeof(countBuf), "%d / %d", _kbCursor, visibleBoxes);
        uint16_t countColor = (_kbCursor < MIN_BOXES) ? C_TEXT_OFF : C_ACCENT;
        _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
        _canvasWide->setTextColor(countColor);
        int16_t cx1, cy1; uint16_t cw, ch1;
        _canvasWide->getTextBounds(countBuf, 0, 0, &cx1, &cy1, &cw, &ch1);
        _canvasWide->setCursor((320 - cw) / 2, boxH + 3);
        _canvasWide->print(countBuf);

        /* Blit de uma vez — sem flicker */
        blitCanvas(_canvasWide, 0, startY, 320, stripH);
    }


    {
        const int keyW = 30, keyH = 30, gap = 2;
        const int startX = 1, startY = 72;

        /* Desenha uma fila de teclas por vez via canvas para evitar flicker */
        for (int row = 0; row < 3; row++) {
            int ky = startY + row * (keyH + gap);

            /* Preenche o canvas com o fundo da tela */
            _canvasWide->fillScreen(C_BG_MAIN);

            for (int col = 0; col < 10; col++) {
                int kx = startX + col * (keyW + gap);
                char ch = activeLayer[row][col];
                bool selected = (row == _kbSelRow && col == _kbSelCol);

                /* Tecla com bordas arredondadas — destaque se selecionada */
                uint16_t keyBg  = selected ? C_ACCENT    : C_CARD_BG;
                uint16_t keyFg  = selected ? C_BG_MAIN   : C_TEXT_MAIN;
                uint16_t hintFg = selected ? C_BG_MAIN   : C_TEXT_OFF;

                _canvasWide->fillRoundRect(kx, 0, keyW, keyH, 4, keyBg);
                if (!selected) _canvasWide->drawRoundRect(kx, 0, keyW, keyH, 4, C_TEXT_SUB);

                /* Caractere principal centralizado */
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(keyFg);
                char label[2] = {ch, '\0'};
                int16_t lx1, ly1; uint16_t lw, lh;
                _canvasWide->getTextBounds(label, 0, 0, &lx1, &ly1, &lw, &lh);
                _canvasWide->setCursor(kx + (keyW - lw) / 2 - lx1, (keyH - lh) / 2 - ly1);
                _canvasWide->print(label);

                /* Hint do layer alternativo — canto superior direito, dentro da tecla */
                char hint = '\0';
                if (_kbLayer == 0)      hint = layer2[row][col];
                else if (_kbLayer == 1) hint = layer2[row][col];
                else                    hint = layer0[row][col];
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(hintFg);
                char hintStr[2] = {hint, '\0'};
                int16_t hx1, hy1; uint16_t hw, hh;
                _canvasWide->getTextBounds(hintStr, 0, 0, &hx1, &hy1, &hw, &hh);
                _canvasWide->setCursor(kx + keyW - (int)hw - 4, 3);
                _canvasWide->print(hintStr);
            }

            /* Blit da fila inteira de uma vez — sem flicker */
            blitCanvas(_canvasWide, 0, ky, 320, keyH);
        }
    }


    {
        /*
         * Action bar: Shift, 123, Space, Backspace, OK.
         * Mesma largura total das filas de teclas (x=1..319).
         * Shift=48, 123=48, Space=118, Backspace=48, OK=48, gap=2.
         */
        const int barY = 170, barH = 22;
        const int bx0 = 1;       /* Shift */
        const int bx1 = 51;      /* 123 */
        const int bx2 = 101;     /* Space */
        const int bx3 = 221;     /* Backspace */
        const int bx4 = 271;     /* OK */
        const int bw01 = 48;     /* Shift e 123 */
        const int bw2 = 118;     /* Space */
        const int bw34 = 48;     /* Backspace e OK */
        bool barActive = (_kbSelRow == 3);

        _canvasWide->fillScreen(C_BG_MAIN);

        /* Shift button (col 0) */
        {
            bool layerActive = (_kbLayer == 1) || _kbShiftLock;
            bool sel = barActive && (_kbSelCol == 0);
            uint16_t bg = sel ? C_ACCENT_HIGH : (layerActive ? C_ACCENT : C_CARD_BG);
            uint16_t fg = (sel || layerActive) ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(bx0, 0, bw01, barH, 4, bg);
            if (!sel && !layerActive) _canvasWide->drawRoundRect(bx0, 0, bw01, barH, 4, C_TEXT_SUB);
            if (sel) _canvasWide->drawRoundRect(bx0, 0, bw01, barH, 4, C_ACCENT);
            int cx = bx0 + bw01 / 2, cy = 5;
            _canvasWide->fillTriangle(cx - 5, cy + 5, cx, cy, cx + 5, cy + 5, fg);
            _canvasWide->fillRect(cx - 2, cy + 5, 4, 6, fg);
            if (_kbShiftLock) {
                _canvasWide->drawFastHLine(bx0 + 10, barH - 3, 28, fg);
            }
        }

        /* 123 button (col 1) */
        {
            bool layerActive = (_kbLayer == 2);
            bool sel = barActive && (_kbSelCol == 1);
            uint16_t bg = sel ? C_ACCENT_HIGH : (layerActive ? C_ACCENT : C_CARD_BG);
            uint16_t fg = (sel || layerActive) ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(bx1, 0, bw01, barH, 4, bg);
            if (!sel && !layerActive) _canvasWide->drawRoundRect(bx1, 0, bw01, barH, 4, C_TEXT_SUB);
            if (sel) _canvasWide->drawRoundRect(bx1, 0, bw01, barH, 4, C_ACCENT);
            _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(fg);
            int16_t tx1, ty1; uint16_t tw, th;
            _canvasWide->getTextBounds("123", 0, 0, &tx1, &ty1, &tw, &th);
            _canvasWide->setCursor(bx1 + (bw01 - (int)tw) / 2, (barH - (int)th) / 2);
            _canvasWide->print("123");
        }

        /* Space bar (col 2) */
        {
            bool sel = barActive && (_kbSelCol == 2);
            uint16_t bg = sel ? C_ACCENT_HIGH : C_CARD_BG;
            _canvasWide->fillRoundRect(bx2, 0, bw2, barH, 4, bg);
            if (sel) _canvasWide->drawRoundRect(bx2, 0, bw2, barH, 4, C_ACCENT);
            else     _canvasWide->drawRoundRect(bx2, 0, bw2, barH, 4, C_TEXT_SUB);
            uint16_t lineCol = sel ? C_BG_MAIN : C_TEXT_OFF;
            int lineX = bx2 + 20;
            int lineW = bw2 - 40;
            _canvasWide->drawFastHLine(lineX, 14, lineW, lineCol);
        }

        /* Backspace button (col 3) */
        {
            bool sel = barActive && (_kbSelCol == 3);
            uint16_t bg = sel ? C_ACCENT_HIGH : C_CARD_BG;
            uint16_t fg = sel ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(bx3, 0, bw34, barH, 4, bg);
            if (sel) _canvasWide->drawRoundRect(bx3, 0, bw34, barH, 4, C_ACCENT);
            else     _canvasWide->drawRoundRect(bx3, 0, bw34, barH, 4, C_TEXT_SUB);
            int cx = bx3 + bw34 / 2, cy = barH / 2;
            _canvasWide->fillTriangle(cx - 8, cy, cx - 2, cy - 5, cx - 2, cy + 5, fg);
            _canvasWide->fillRect(cx - 2, cy - 3, 10, 6, fg);
        }

        /* OK button (col 4) */
        {
            bool sel = barActive && (_kbSelCol == 4);
            uint16_t bg = sel ? C_ACCENT_HIGH : C_ACCENT;
            uint16_t fg = C_BG_MAIN;
            _canvasWide->fillRoundRect(bx4, 0, bw34, barH, 4, bg);
            if (sel) _canvasWide->drawRoundRect(bx4, 0, bw34, barH, 4, C_BG_MAIN);
            _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(fg);
            int16_t tx1, ty1; uint16_t tw, th;
            _canvasWide->getTextBounds("OK", 0, 0, &tx1, &ty1, &tw, &th);
            _canvasWide->setCursor(bx4 + (bw34 - (int)tw) / 2, (barH - (int)th) / 2);
            _canvasWide->print("OK");
        }

        blitCanvas(_canvasWide, 0, barY, 320, barH);
    }


    {
        /*
         * 5 dashboard-style navigation buttons (58x40, radius 12).
         * ▲  ▼  ◄  ►  ✓(confirm character)
         * Posicionados na parte inferior da tela (Y=195).
         */
        const int btnW = 58, btnH = 40, gap = 5, startX = 5;
        const int navY = 195;

        _canvasWide->fillScreen(C_BG_MAIN);

        /* ◄ button (left) */
        {
            _canvasWide->fillRoundRect(startX, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = startX + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx + 6, cy - 8, cx + 6, cy + 8, cx - 8, cy, C_TEXT_MAIN);
        }

        /* ► button (right) */
        {
            int bx = startX + (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 6, cy - 8, cx - 6, cy + 8, cx + 8, cy, C_TEXT_MAIN);
        }

        /* ▲ button (up) */
        {
            int bx = startX + 2 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 8, cy + 6, cx + 8, cy + 6, cx, cy - 8, C_TEXT_MAIN);
        }

        /* ▼ button (down) */
        {
            int bx = startX + 3 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 8, cy - 6, cx + 8, cy - 6, cx, cy + 8, C_TEXT_MAIN);
        }

        /* ✓ button (confirm selected character) */
        {
            int bx = startX + 4 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_ACCENT);
            int cx = bx + btnW / 2, cy = btnH / 2;
            /* Check icon */
            _canvasWide->drawLine(cx - 8, cy, cx - 3, cy + 6, C_BG_MAIN);
            _canvasWide->drawLine(cx - 7, cy, cx - 2, cy + 6, C_BG_MAIN);
            _canvasWide->drawLine(cx - 3, cy + 6, cx + 8, cy - 6, C_BG_MAIN);
            _canvasWide->drawLine(cx - 2, cy + 6, cx + 9, cy - 6, C_BG_MAIN);
        }

        blitCanvas(_canvasWide, 0, navY, 320, btnH);
    }

    _forceSettingsRedraw = false;
}


uint16_t DisplayManager::readPixel(int16_t x, int16_t y) {
    if (!_tft) return 0;
    _tft->startWrite(); _tft->setAddrWindow(x, y, 1, 1); _tft->endWrite();
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, LOW); SPI.transfer(0x2E);
    digitalWrite(TFT_DC, HIGH); SPI.transfer(0x00);
    uint8_t r = SPI.transfer(0x00); uint8_t g = SPI.transfer(0x00); uint8_t b = SPI.transfer(0x00);
    digitalWrite(TFT_CS, HIGH); SPI.endTransaction();
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}


void DisplayManager::readRow(int16_t y, uint16_t* buffer, int16_t w) {
    if (!_tft || !buffer) return;


    _tft->startWrite();
    _tft->setAddrWindow(0, y, w, 1);
    _tft->endWrite();


    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, LOW);
    SPI.transfer(0x2E);
    digitalWrite(TFT_DC, HIGH);
    SPI.transfer(0x00);

    for (int16_t x = 0; x < w; x++) {
        uint8_t r = SPI.transfer(0x00);
        uint8_t g = SPI.transfer(0x00);
        uint8_t b = SPI.transfer(0x00);
        buffer[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    digitalWrite(TFT_CS, HIGH);
    SPI.endTransaction();
}

uint32_t DisplayManager::fastRandom(uint32_t maxVal) {
    _rngState ^= _rngState << 13; _rngState ^= _rngState >> 17; _rngState ^= _rngState << 5;
    return _rngState % maxVal;
}

void DisplayManager::scrambleKeys() {
    const char poolNum[] = "0123456789";
    const char poolUpper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char poolLower[] = "abcdefghijklmnopqrstuvwxyz";
    const char poolSpec[] = "!@#$%^&*()_+-=[]{}|;':\",./<>?\\~";
    char expected = '\0'; if (_authStep < _expectedPin.length()) { expected = _expectedPin[_authStep]; }
    int expectedType = -1;
    if (expected >= '0' && expected <= '9') expectedType = 0;
    else if (expected >= 'A' && expected <= 'Z') expectedType = 1;
    else if (expected >= 'a' && expected <= 'z') expectedType = 2;
    else if (expected != '\0') expectedType = 3;
    int correctBtn = -1; if (expected != '\0') correctBtn = fastRandom(4);
    for (int i = 0; i < 4; i++) {
        char chars[4];
        if (i == correctBtn && expectedType == 0) chars[0] = expected; else do { chars[0] = poolNum[fastRandom(sizeof(poolNum)-1)]; } while(chars[0] == expected);
        if (i == correctBtn && expectedType == 1) chars[1] = expected; else do { chars[1] = poolUpper[fastRandom(sizeof(poolUpper)-1)]; } while(chars[1] == expected);
        if (i == correctBtn && expectedType == 2) chars[2] = expected; else do { chars[2] = poolLower[fastRandom(sizeof(poolLower)-1)]; } while(chars[2] == expected);
        if (i == correctBtn && expectedType == 3) chars[3] = expected; else do { chars[3] = poolSpec[fastRandom(sizeof(poolSpec)-1)]; } while(chars[3] == expected);
        for (int k = 3; k > 0; k--) { int j = fastRandom(k + 1); char temp = chars[k]; chars[k] = chars[j]; chars[j] = temp; }
        _keypadChars[i][0] = chars[0]; _keypadChars[i][1] = chars[1]; _keypadChars[i][2] = chars[2]; _keypadChars[i][3] = chars[3]; _keypadChars[i][4] = '\0';
    }
}

void DisplayManager::showAuthScreen(String expectedPin) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_AUTH; _forceSettingsRedraw = true; _repaintSettings = true;
    if (_permanentLockout) { _lockoutUntil = millis() + 10000; } else {
        _expectedPin = expectedPin; _authStep = 0; _authFailed = false; _isCurrentAttemptValid = true;
        _rngState = micros() ^ 0xA5A5A5A5; if (_rngState == 0) _rngState = 1;
        scrambleKeys();
    }
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawAuthScreen() {
    int16_t bx, by; uint16_t bw, bh;
    String titleTxt = tr(TR_AUTH_TITLE); String cancelTxt = tr(TR_CANCEL);

    if (_permanentLockout) {
        if (_forceSettingsRedraw) {
            _tft->fillScreen(C_TEMP_HOT); _tft->fillRect(0, 0, 320, 32, C_CARD_BG); _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
            _tft->setFont(&FreeSansBold12pt7b); _tft->setTextColor(C_BG_MAIN); String msg1 = tr(TR_ACCESS_BLOCKED);
            _tft->getTextBounds(msg1, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 110); _tft->print(msg1);
            _tft->setFont(&FreeSansBold9pt7b); String msg2 = tr(TR_REBOOT_REQ);
            _tft->getTextBounds(msg2, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 140); _tft->print(msg2);
            _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
            /* License button */
            _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
            _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_SUB);
            String licTxt = tr(TR_LICENSE_TITLE);
            _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
            _forceSettingsRedraw = false;
        }
        return;
    }

    if (_lockoutUntil > 0 && _lockoutUntil > millis()) {
        static long lastSec = -1;
        if (_forceSettingsRedraw) {
            _tft->fillScreen(C_BG_MAIN); _tft->fillRect(0, 0, 320, 32, C_CARD_BG); _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
            _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG);
            _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
            /* License button */
            _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
            _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_SUB);
            String licTxt = tr(TR_LICENSE_TITLE);
            _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
            _forceSettingsRedraw = false; lastSec = -1;
        }
        long secondsLeft = (_lockoutUntil - millis()) / 1000 + 1;
        if (secondsLeft != lastSec) {
            lastSec = secondsLeft;
            _canvasWide->fillScreen(C_BG_MAIN); _canvasWide->setFont(&FreeSansBold12pt7b); _canvasWide->setTextColor(C_TEMP_WARM);
            String txt1 = tr(TR_ATTEMPTS_EXCEEDED); _canvasWide->getTextBounds(txt1, 0, 0, &bx, &by, &bw, &bh);
            _canvasWide->setCursor((320 - bw) / 2, 25); _canvasWide->print(txt1); blitCanvas(_canvasWide, 0, 90, 320, 45);
            _canvasWide->fillScreen(C_BG_MAIN); char timeStr[64]; snprintf(timeStr, sizeof(timeStr), tr(TR_WAIT_SECONDS), secondsLeft);
            _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(C_TEXT_SUB);
            _canvasWide->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh); _canvasWide->setCursor((320 - bw) / 2, 25); _canvasWide->print(timeStr);
            blitCanvas(_canvasWide, 0, 135, 320, 45);
        }
        return;
    }

    if (_forceSettingsRedraw) {
        _tft->fillScreen(C_BG_MAIN); _tft->fillRect(0, 0, 320, 32, C_CARD_BG); _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
        _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG); _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
        /* License button no canto inferior direito */
        _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_SUB);
        String licTxt = tr(TR_LICENSE_TITLE);
        _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
        _forceSettingsRedraw = false;
    }

    /* Authentication status via canvas — avoids flicker */
    _canvasWide->fillScreen(C_BG_MAIN);
    if (_authFailed) {
        _canvasWide->setFont(&FreeSansBold9pt7b);
        _canvasWide->setTextColor(C_TEMP_HOT);
        String invMsg = tr(TR_INVALID_PASSWORD);
        _canvasWide->getTextBounds(invMsg, 0, 0, &bx, &by, &bw, &bh);
        _canvasWide->setCursor((320 - bw) / 2, 20);
        _canvasWide->print(invMsg);
    } else {
        int pinLen = (int)_expectedPin.length();
        int dotSpacing = 20;
        int dotsStartX = (320 - (pinLen * dotSpacing)) / 2 + dotSpacing / 2;
        for (int i = 0; i < pinLen; i++) {
            int cx = dotsStartX + (i * dotSpacing);
            if (i < _authStep) _canvasWide->fillCircle(cx, 15, 6, C_ACCENT);
            else               _canvasWide->drawCircle(cx, 15, 6, C_TEXT_SUB);
        }
    }
    blitCanvas(_canvasWide, 0, 35, 320, 30);

    /* Keypad buttons via canvas — 2 buttons per row, 2 rows */
    for (int row = 0; row < 2; row++) {
        int rowY = 80 + (row * 60);
        _canvasWide->fillScreen(C_BG_MAIN);
        _canvasWide->setFont(&FreeSansBold12pt7b);

        for (int col = 0; col < 2; col++) {
            int btnIdx = (row * 2) + col;
            int bx0 = (col == 0) ? 15 : 165;

            /* Button with polished rounded borders */
            _canvasWide->fillRoundRect(bx0, 0, 140, 45, 10, C_CARD_BG);
            _canvasWide->drawRoundRect(bx0, 0, 140, 45, 10, C_TEXT_SUB);

            /* Characters distributed across button */
            _canvasWide->setTextColor(C_TEXT_MAIN);
            String chars = String(_keypadChars[btnIdx]);
            int slotWidth = 35;
            for (int j = 0; j < 4; j++) {
                String singleChar = String(chars.charAt(j));
                int16_t cbx, cby; uint16_t cbw, cbh;
                _canvasWide->getTextBounds(singleChar, 0, 0, &cbx, &cby, &cbw, &cbh);
                int charX = bx0 + (j * slotWidth) + ((slotWidth - cbw) / 2) - cbx;
                _canvasWide->setCursor(charX, 31);
                _canvasWide->print(singleChar);
            }
        }
        blitCanvas(_canvasWide, 0, rowY, 320, 45);
    }
}


void DisplayManager::setTelemetryPending(uint16_t count) {
    _sharedState.pendingPkts = count;
}


/**
 * @brief Reports the result of the last telemetry send attempt.
 *
 * Success: starts flash animation (blue/white for 1s), then
 *          estabiliza em azul fixo.
 * Falha:   vermelho fixo imediatamente.
 */
void DisplayManager::setTelemetrySendStatus(bool success) {
    if (success) {
        _pktArrowState     = 3;  /* flash de envio */
        _pktArrowFlashOn   = false;
        _pktArrowFlashTime = millis();
        _pktArrowFlashEnd  = millis() + 1000;
    } else {
        _pktArrowState = 2;  /* vermelho fixo */
    }
}


void DisplayManager::showTouchCalibration() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_TOUCH_CAL;
    _calStep = 0;
    _calPhase = 0;
    memset(_calRawX, 0, sizeof(_calRawX));
    memset(_calRawY, 0, sizeof(_calRawY));
    _forceSettingsRedraw = true;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}


void DisplayManager::loadTouchCalibration(const TouchCalData* cal) {
    if (!cal || cal->magic != 0xCA) {
        _calValid = false;
        return;
    }
    _calSwapXY = (cal->flags & 0x01) != 0;
    _calXMin   = cal->xMin;
    _calXMax   = cal->xMax;
    _calYMin   = cal->yMin;
    _calYMax   = cal->yMax;
    _calValid  = true;
}


void DisplayManager::fillCalData(TouchCalData* cal) const {
    if (!cal) return;
    cal->magic = 0xCA;
    cal->flags = _calSwapXY ? 0x01 : 0x00;
    cal->xMin  = _calXMin;
    cal->xMax  = _calXMax;
    cal->yMin  = _calYMin;
    cal->yMax  = _calYMax;
}


void DisplayManager::mapTouchPoint(TS_Point raw, int16_t &outX, int16_t &outY) {
    int16_t rawForX, rawForY;

    if (_calSwapXY) {
        rawForX = raw.y;
        rawForY = raw.x;
    } else {
        rawForX = raw.x;
        rawForY = raw.y;
    }

    outX = (int16_t)constrain(map(rawForX, _calXMin, _calXMax, 0, 320), 0, 319);
    outY = (int16_t)constrain(map(rawForY, _calYMin, _calYMax, 0, 240), 0, 239);
}


void DisplayManager::drawCrosshair(int16_t cx, int16_t cy, uint16_t color) {
    const int16_t sz = 10;
    _tft->drawLine(cx - sz, cy, cx + sz, cy, color);
    _tft->drawLine(cx, cy - sz, cx, cy + sz, color);
    _tft->drawCircle(cx, cy, sz - 2, color);
}


void DisplayManager::drawCalibrationMessage() {
    if (!_tft) return;
    int16_t x1, y1; uint16_t w, h_bound;

    _tft->fillScreen(C_BG_MAIN);


    bool isSuccess = (_calPhase == 2);
    uint16_t iconColor = isSuccess ? C_TEMP_OK : C_TEMP_WARM;

    if (isSuccess) {
        _tft->drawLine(130, 90, 150, 110, iconColor);
        _tft->drawLine(131, 90, 151, 110, iconColor);
        _tft->drawLine(150, 110, 190, 70, iconColor);
        _tft->drawLine(151, 110, 191, 70, iconColor);
    } else {
        _tft->drawLine(145, 70, 175, 100, iconColor);
        _tft->drawLine(146, 70, 176, 100, iconColor);
        _tft->drawLine(175, 70, 145, 100, iconColor);
        _tft->drawLine(176, 70, 146, 100, iconColor);
    }


    const char* msg = isSuccess ? tr(TR_CAL_DONE) : tr(TR_CAL_REJECTED);
    _tft->setFont(&FreeSansBold9pt7b);
    _tft->setTextColor(C_TEXT_MAIN);
    _tft->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor((320 - w) / 2, 130);
    _tft->print(msg);


    _tft->fillRoundRect(60, 185, 200, 40, 12, C_ACCENT);
    _tft->setFont(&FreeSansBold12pt7b);
    _tft->setTextColor(C_BG_MAIN);
    const char* btnLabel = tr(TR_UNDERSTOOD);
    _tft->getTextBounds(btnLabel, 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor(160 - (w / 2), 212);
    _tft->print(btnLabel);
}


void DisplayManager::drawTouchCalibration() {
    bool fullRedraw = _forceSettingsRedraw;


    if (_calPhase >= 1) {
        drawCalibrationMessage();
        _forceSettingsRedraw = false;
        return;
    }


    int pointIdx = _calStep % 4;
    int cycleNum = (_calStep < 4) ? 1 : 2;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
    }

    if (_calStep < 8) {

        if (!fullRedraw && pointIdx > 0) {
            drawCrosshair(CAL_SCR_X[pointIdx - 1], CAL_SCR_Y[pointIdx - 1], C_BG_MAIN);
        }

        if (!fullRedraw && _calStep == 4) {
            drawCrosshair(CAL_SCR_X[3], CAL_SCR_Y[3], C_BG_MAIN);
        }


        drawCrosshair(CAL_SCR_X[pointIdx], CAL_SCR_Y[pointIdx], C_ACCENT);


        _tft->fillRect(20, 85, 280, 65, C_BG_MAIN);


        _tft->setFont(&FreeSansBold9pt7b);
        _tft->setTextColor(C_ACCENT);
        int16_t bx, by; uint16_t bw, bh;
        const char* title = tr(TR_CAL_TITLE);
        _tft->getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor((320 - bw) / 2, 100);
        _tft->print(title);


        char msg[48];
        snprintf(msg, sizeof(msg), "%s (%d/4)", tr(TR_CAL_TOUCH_POINT), pointIdx + 1);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor((320 - bw) / 2, 122);
        _tft->print(msg);


        char cycleBuf[24];
        snprintf(cycleBuf, sizeof(cycleBuf), "[ %d / 2 ]", cycleNum);
        _tft->setFont(NULL); _tft->setTextSize(1);
        _tft->setTextColor(C_TEXT_OFF);
        _tft->getTextBounds(cycleBuf, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor((320 - bw) / 2, 140);
        _tft->print(cycleBuf);
    }

    _forceSettingsRedraw = false;
}


void DisplayManager::showSettingsSounds(const SoundSettingsState& state) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_SOUNDS;
    _soundSettings = state;
    _soundSelection = 0;
    _inMelodySelect = false;
    _forceSettingsRedraw = true;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}


void DisplayManager::drawSettingsSounds() {
    if (!_canvasWide) return;

    static int lastSoundPage = -1;
    int soundPage = _soundSelection / 4;
    bool fullRedraw = _forceSettingsRedraw;
    bool pageChanged = (soundPage != lastSoundPage);

    const int TOTAL_ITEMS = 8;
    int totalPages = (TOTAL_ITEMS + 3) / 4;


    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_SOUNDS_TITLE));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);

        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);

        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw) / 2, btnY + 25); _tft->print(backTxt);

        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String saveTxt = tr(TR_SAVE);
        _tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw) / 2, btnY + 25); _tft->print(saveTxt);
    }


    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
        int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
        int thumbY = trackY;
        if (totalPages > 1) { thumbY += (soundPage * (trackH - thumbH)) / (totalPages - 1); }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }


    LangKey itemLabels[TOTAL_ITEMS] = {
        TR_SND_TOUCH, TR_SND_CONFIRM, TR_SND_ERROR, TR_SND_ALARM,
        TR_SND_WEB,   TR_SND_MUTE,    TR_SND_VOLUME, TR_SND_ALARM_VOL
    };

    int startIdx = soundPage * 4;
    int yBase = 40; int itemW = 285;

    for (int i = 0; i < 4; i++) {
        int actualIdx = startIdx + i;
        int y = yBase + (i * 38);

        _canvasWide->fillScreen(C_BG_MAIN);

        if (actualIdx < TOTAL_ITEMS) {
            bool isSelected = (actualIdx == _soundSelection);
            uint16_t bg  = isSelected ? C_ACCENT  : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;

            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

            _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24); _canvasWide->print(tr(itemLabels[actualIdx]));

            if (actualIdx == 6 || actualIdx == 7) {

                uint8_t volVal = (actualIdx == 6)
                               ? _soundSettings.volume
                               : _soundSettings.alarmVolume;
                char buf[8];
                snprintf(buf, sizeof(buf), "%d%%", volVal);
                int16_t bx, by; uint16_t bw, bh;
                _canvasWide->getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
                _canvasWide->setCursor(itemW - 15 - bw, 24);
                _canvasWide->print(buf);

                int barW = 100;
                int barX = itemW - 15 - bw - 10 - barW;
                int barY = 11; int barH = 12;
                int fillW = (int)((uint32_t)barW * volVal / 100);

                uint16_t barBg   = isSelected ? C_ACCENT_HIGH : C_BAR_BG;
                uint16_t barFill = isSelected ? C_BG_MAIN     : C_ACCENT;
                _canvasWide->fillRoundRect(barX, barY, barW, barH, 3, barBg);
                if (fillW > 0) {
                    _canvasWide->fillRoundRect(barX, barY, fillW, barH, 3, barFill);
                }
            } else {

                bool val = false;
                switch (actualIdx) {
                    case 0: val = _soundSettings.touchEnabled;   break;
                    case 1: val = _soundSettings.confirmEnabled; break;
                    case 2: val = _soundSettings.errorEnabled;   break;
                    case 3: val = _soundSettings.alarmEnabled;   break;
                    case 4: val = _soundSettings.webEnabled;     break;
                    case 5: val = _soundSettings.muted;          break;
                }
                const char* valStr = val ? tr(TR_ON) : tr(TR_OFF);
                int16_t bx, by; uint16_t bw, bh;
                _canvasWide->getTextBounds(valStr, 0, 0, &bx, &by, &bw, &bh);
                _canvasWide->setCursor(itemW - 15 - bw, 24);
                _canvasWide->print(valStr);
            }
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }

    _forceSettingsRedraw = false;
    lastSoundPage = soundPage;
}


void DisplayManager::drawMelodySelect() {
    if (!_canvasWide) return;


    static const char* MEL_NAMES[4][6] = {
        {"1. Click",      "2. Bubble",    "3. Tick",
         "4. Snap",       "5. Drop",      "6. Chirp"},
        {"1. Ascending",  "2. Fanfare",   "3. Chime",
         "4. Triumph",    "5. Sparkle",   "6. Resolve"},
        {"1. Descending", "2. Buzz",      "3. Low",
         "4. Harsh",      "5. Decline",   "6. Blip"},
        {"1. Dual Beep",  "2. Siren",     "3. Rapid",
         "4. Pulse",      "5. Escalate",  "6. Staccato"}
    };
    static const LangKey TYPE_LABELS[4] = {
        TR_SND_TOUCH, TR_SND_CONFIRM, TR_SND_ERROR, TR_SND_ALARM
    };

    uint8_t typeIdx = _melSelectType;
    if (typeIdx > 3) typeIdx = 0;

    const int TOTAL_VARIANTS = 6;
    bool fullRedraw = _forceSettingsRedraw;


    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);


        _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_ACCENT);
        _tft->setCursor(10, 22);
        _tft->print(tr(TYPE_LABELS[typeIdx]));


        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);

        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);

        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw) / 2, btnY + 25); _tft->print(backTxt);

        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String saveTxt = tr(TR_SAVE);
        _tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw) / 2, btnY + 25); _tft->print(saveTxt);
    }


    int melPage  = _melSelectIdx / 4;
    int startIdx = melPage * 4;
    int yBase    = 40;
    int itemW    = 285;

    for (int i = 0; i < 4; i++) {
        int actualIdx = startIdx + i;
        int y = yBase + (i * 38);

        _canvasWide->fillScreen(C_BG_MAIN);

        if (actualIdx < TOTAL_VARIANTS) {
            bool isSelected = (actualIdx == _melSelectIdx);
            uint16_t bg  = isSelected ? C_ACCENT  : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;

            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

            _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(14, 24);
            _canvasWide->print(MEL_NAMES[typeIdx][actualIdx]);
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }

    _forceSettingsRedraw = false;
}


bool DisplayManager::consumeTouchSound() {
    if (_touchSoundPending) {
        _touchSoundPending = false;
        return true;
    }
    return false;
}


bool DisplayManager::consumeErrorSound() {
    if (_errorSoundPending) {
        _errorSoundPending = false;
        return true;
    }
    return false;
}


bool DisplayManager::consumePreviewSound(SoundEvent& outEvent, uint8_t& outIdx) {
    if (_previewPending) {
        outEvent = (SoundEvent)_previewType;
        outIdx   = _previewMelIdx;
        _previewPending = false;
        return true;
    }
    return false;
}


bool DisplayManager::consumeVolumePreview(uint8_t& outLevel) {
    if (_volumePreviewPending) {
        outLevel = _volumePreviewLevel;
        _volumePreviewPending = false;
        return true;
    }
    return false;
}


bool DisplayManager::acceptTouch(uint8_t zoneId) {
    uint32_t now = millis();
    if (zoneId == _lastTouchRegion && (now - _lastRegionTouchTime) < 250) {
        return false;
    }
    _lastTouchRegion     = zoneId;
    _lastRegionTouchTime = now;
    _lastTouchTimestamp  = now;
    _touchSoundPending   = true;
    return true;
}


bool DisplayManager::consumeAlarmVolumePreview(uint8_t& outLevel) {
    if (_alarmVolPreviewPending) {
        outLevel = _alarmVolPreviewLevel;
        _alarmVolPreviewPending = false;
        return true;
    }
    return false;
}


void DisplayManager::setWebNotification(const char* username) {
    if (!username) return;
    strncpy(_webNotifyUser, username, sizeof(_webNotifyUser) - 1);
    _webNotifyUser[sizeof(_webNotifyUser) - 1] = '\0';
    _webNotifyStartMs = millis();
    if (_webNotifyStartMs == 0) _webNotifyStartMs = 1;
}


void DisplayManager::showSettingsLicense() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_LICENSE;
    _licensePage = 0;
    _forceSettingsRedraw = true;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}


void DisplayManager::drawSettingsLicense() {
    bool fullRedraw = _forceSettingsRedraw;

    int langIdx = _currentLangIdx;
    if (langIdx < 0 || langIdx >= TOTAL_LANGS) langIdx = 0;
    const char* licText = LICENSE_TEXT[langIdx];

    const int MAX_COLS  = 50;
    const int LINE_H    = 9;
    const int TEXT_Y0    = 36;
    const int MAX_VIS    = 17;

    /* Count total lines (license + acknowledgments already integrated) */
    int totalLines = wrapLineCount(licText, MAX_COLS);

    /* Calculate total pages */
    _licenseTotalPages = (totalLines + MAX_VIS - 1) / MAX_VIS;
    if (_licenseTotalPages < 1) _licenseTotalPages = 1;
    if (_licensePage >= _licenseTotalPages) _licensePage = _licenseTotalPages - 1;
    if (_licensePage < 0) _licensePage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);

        /* Header with title and page counter */
        _tft->fillRect(0, 0, 320, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_LICENSE_TITLE));

        char pgBuf[8];
        snprintf(pgBuf, sizeof(pgBuf), "%d/%d", _licensePage + 1, _licenseTotalPages);
        int16_t px, py; uint16_t pw, ph;
        _tft->getTextBounds(pgBuf, 0, 0, &px, &py, &pw, &ph);
        _tft->setTextColor(C_TEXT_SUB);
        _tft->setCursor(310 - (int)pw, 22); _tft->print(pgBuf);

        /* Bottom buttons */
        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

        _tft->fillRoundRect(5, btnY, 100, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(55, btnY + 12, 45, btnY + 26, 65, btnY + 26, C_TEXT_MAIN);

        _tft->fillRoundRect(110, btnY, 100, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(160, btnY + 26, 150, btnY + 12, 170, btnY + 12, C_TEXT_MAIN);

        _tft->fillRoundRect(215, btnY, 100, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(215 + (100 - bw) / 2, btnY + 25); _tft->print(backTxt);
    }

    /* Clear text area */
    _tft->fillRect(0, TEXT_Y0, 320, MAX_VIS * LINE_H, C_BG_MAIN);
    _tft->setFont(NULL); _tft->setTextSize(1);
    _tft->setTextColor(C_TEXT_SUB);

    /* Render current page */
    int startLine = _licensePage * MAX_VIS;
    renderWrapped(_tft, licText, 10, TEXT_Y0, MAX_COLS, LINE_H,
                  startLine, MAX_VIS);

    /* Page indicators (dots) */
    {
        int dotY = TEXT_Y0 + MAX_VIS * LINE_H + 2;
        int dotSpacing = 10;
        int dotsWidth  = (_licenseTotalPages - 1) * dotSpacing;
        int dotX0      = (320 - dotsWidth) / 2;
        for (int i = 0; i < _licenseTotalPages; i++) {
            int cx = dotX0 + i * dotSpacing;
            if (i == _licensePage) {
                _tft->fillCircle(cx, dotY, 3, C_ACCENT);
            } else {
                _tft->fillCircle(cx, dotY, 2, C_TEXT_OFF);
            }
        }
    }

    /* Atualizar contador no header (sem redesenhar tudo) */
    if (!fullRedraw) {
        _tft->fillRect(240, 6, 75, 22, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_SUB);
        char pgBuf[8];
        snprintf(pgBuf, sizeof(pgBuf), "%d/%d", _licensePage + 1, _licenseTotalPages);
        int16_t px, py; uint16_t pw, ph;
        _tft->getTextBounds(pgBuf, 0, 0, &px, &py, &pw, &ph);
        _tft->setCursor(310 - (int)pw, 22); _tft->print(pgBuf);
    }

    _forceSettingsRedraw = false;
}
