/**
 * @file    DisplayManager.cpp
 * @brief   Implementation of DisplayManager — Core 1 render loop, touch handling, and all UI screens.
 * @details Contains the complete rendering engine: Core 1 entry point, snapshot-
 * based dirty rendering, dashboard with ambient/slot panels, graph
 * plotting with dual Y-axis, settings menus (themes, alarms, sounds,
 * language, password, calibration, license), authentication keypad with
 * scrambled layout and lockout, alarm flash animation with per-slot
 * masking, and the i18n dictionary for 8 languages.
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
        "AMBIENT", "Settings > Main", "Settings > Themes", "Settings > Language", "EXIT",
        "APPLY", "CANCEL", "Security Authentication", "ACCESS BLOCKED", "Reboot required",
        "Attempts Exceeded", "Wait %ld seconds...", "Invalid Password!", "Loading...", "Reading History...",
        "No Data", "MAXIMUM", "MINIMUM", "Temperature", "Humidity",
        "PLOT CHART", "1. Visual Themes", "2. Alarm Limits", "3. Alarm Sounds", "4. System Language",
        "Applying Theme...", "SAVE", "Alarm Limits", "Temp Min", "Temp Max",
        "Hum Min", "Hum Max", "ENTER", "SKIP", "5. Change Password",
        "New Password", "6. Touch Calibration", "Touch Calibration", "Touch the crosshair", "Calibration Done!",
        "Imprecise touches! Try again.", "Confirm Password", "Password too short! (min 4)", "Passwords don't match!", "Password saved!",
        "UNDERSTOOD", "Sound Settings", "Touch Click", "Confirmation", "Error Sound",
        "Alarm Sound", "Mute All", "Sys Volume", "Alarm Vol", "ON",
        "OFF", "Web Access", "Melody", "7. License", "MIT License",
        "ACTIVE", "Silence 120s", "Deactivate", "Min/Max", "Silenced",
        "%RH", "7. Touch Sensitivity", "Touch Sensitivity", "Tap %d/%d", "Calibration Done!",
        "AVERAGE", "STD DEV", "Error", "Configuration Mode", "8. System Status",
        "System Status",
        "9. Display Alignment", "Display Alignment", "Adjust +/-4 px. Saving clears touch calibration."
    },

    {
        "AMBIENTE", "Configuracoes > Principal", "Configuracoes > Temas", "Configuracoes > Idioma", "SAIR",
        "APLICAR", "CANCELAR", "Autenticacao de Seguranca", "ACESSO BLOQUEADO", "Reinicializacao requerida",
        "Tentativas Excedidas", "Aguarde %ld segundos...", "Senha Invalida!", "Carregando...", "Lendo Historico...",
        "Sem Dados", "MAXIMO", "MINIMO", "Temperatura", "Umidade",
        "GERAR GRAFICO", "1. Temas Visuais", "2. Limites de Alarme", "3. Sons de Alarme", "4. Idioma do Sistema",
        "Aplicando Tema...", "SALVAR", "Limites de Alarme", "Temp Min", "Temp Max",
        "Umid Min", "Umid Max", "ENTRAR", "PULAR", "5. Alterar Senha",
        "Nova Senha", "6. Calibrar Touch", "Calibracao do Touch", "Toque na mira", "Calibracao Concluida!",
        "Toques imprecisos! Tente novamente.", "Confirmar Senha", "Senha muito curta! (min 4)", "Senhas nao coincidem!", "Senha salva!",
        "ENTENDI", "Config. de Sons", "Toque na Tela", "Confirmacao", "Som de Erro",
        "Som de Alarme", "Silenciar Tudo", "Vol. Sistema", "Vol. Alarme", "SIM",
        "NAO", "Acesso Web", "Melodia", "7. Licenca", "Licenca MIT",
        "ATIVO", "Silenciar 120s", "Desativar", "Min/Max", "Silenciado",
        "%UR", "7. Sensibilidade do Toque", "Sensibilidade do Toque", "Toque %d/%d", "Calibracao Concluida!",
        "MEDIA", "DESVIO", "Erro", "Modo de Configuracao", "8. Status do Sistema",
        "Status do Sistema",
        "9. Alinhamento da Tela", "Alinhamento da Tela", "Ajuste +/-4 px. Salvar reinicia calibracao do touch."
    },

    {
        "AMBIENTE", "Ajustes > Principal", "Ajustes > Temas", "Ajustes > Idioma", "SALIR",
        "APLICAR", "CANCELAR", "Autenticacion de Seguridad", "ACCESO BLOQUEADO", "Reinicio requerido",
        "Intentos Excedidos", "Espere %ld segundos...", "Clave Invalida!", "Cargando...", "Leyendo Historial...",
        "Sin Datos", "MAXIMO", "MINIMO", "Temperatura", "Humedad",
        "GENERAR GRAFICO", "1. Temas Visuales", "2. Limites de Alarma", "3. Sonidos de Alarma", "4. Idioma del Sistema",
        "Aplicando Tema...", "GUARDAR", "Limites de Alarma", "Temp Min", "Temp Max",
        "Humed Min", "Humed Max", "ENTRAR", "OMITIR", "5. Cambiar Clave",
        "Nueva Clave", "6. Calibrar Touch", "Calibracion del Touch", "Toque la mira", "Calibracion Completa!",
        "Toques imprecisos! Intente de nuevo.", "Confirmar Clave", "Clave muy corta! (min 4)", "Claves no coinciden!", "Clave guardada!",
        "ENTENDIDO", "Config. de Sonidos", "Toque en Pantalla", "Confirmacion", "Sonido de Error",
        "Sonido de Alarma", "Silenciar Todo", "Vol. Sistema", "Vol. Alarma", "SI",
        "NO", "Acceso Web", "Melodia", "7. Licencia", "Licencia MIT",
        "ACTIVO", "Silenciar 120s", "Desactivar", "Min/Max", "Silenciado",
        "%RH", "7. Sensibilidad Tactil", "Sensibilidad Tactil", "Toque %d/%d", "Calibracion Completa!",
        "PROMEDIO", "DESVIACION", "Error", "Modo Configuracion", "8. Estado del Sistema",
        "Estado del Sistema",
        "9. Alineacion Pantalla", "Alineacion de Pantalla", "Ajuste +/-4 px. Guardar reinicia calibracion tactil."
    },

    {
        "AMBIANCE", "Reglages > Principal", "Reglages > Themes", "Reglages > Langue", "QUITTER",
        "APPLIQUER", "ANNULER", "Authentification de Securite", "ACCES BLOQUE", "Redemarrage requis",
        "Tentatives Depassees", "Patientez %ld secondes...", "Mot de passe invalide!", "Chargement...", "Lecture Historique...",
        "Aucune Donnee", "MAXIMUM", "MINIMUM", "Temperature", "Humidite",
        "TRACER GRAPHIQUE", "1. Themes Visuels", "2. Limites d'Alarme", "3. Sons d'Alarme", "4. Langue du Systeme",
        "Application du Theme...", "ENREGISTRER", "Limites d'Alarme", "Temp Min", "Temp Max",
        "Hum Min", "Hum Max", "ENTRER", "PASSER", "5. Changer Mot de Passe",
        "Nouveau Mot de Passe", "6. Calibrer le Tactile", "Calibration Tactile", "Touchez la cible", "Calibration Terminee!",
        "Touches imprecis! Reessayez.", "Confirmer Mot de Passe", "Mot de passe trop court! (min 4)", "Mots de passe differents!", "Mot de passe enregistre!",
        "COMPRIS", "Reglages des Sons", "Toucher Ecran", "Confirmation", "Son d'Erreur",
        "Son d'Alarme", "Tout Couper", "Vol. Systeme", "Vol. Alarme", "OUI",
        "NON", "Acces Web", "Melodie", "7. Licence", "Licence MIT",
        "ACTIF", "Silencer 120s", "Desactiver", "Min/Max", "En Silence",
        "%RH", "7. Sensibilite Tactile", "Sensibilite Tactile", "Touchez %d/%d", "Calibration Terminee!",
        "MOYENNE", "ECART-TYPE", "Erreur", "Mode Configuration", "8. Etat du Systeme",
        "Etat du Systeme",
        "9. Alignement Ecran", "Alignement de l'Ecran", "Ajustez +/-4 px. Sauver reinitialise le tactile."
    },

    {
        "UMGEBUNG", "Einstellungen > Haupt", "Einstellungen > Themen", "Einstellungen > Sprache", "BEENDEN",
        "ANWENDEN", "ABBRECHEN", "Sicherheitsauthentifizierung", "ZUGANG GESPERRT", "Neustart erforderlich",
        "Versuche Ueberschritten", "Warten Sie %ld Sekunden...", "Ungultiges Passwort!", "Laden...", "Verlauf Lesen...",
        "Keine Daten", "MAXIMUM", "MINIMUM", "Temperatur", "Feuchtigkeit",
        "DIAGRAMM ERSTELLEN", "1. Visuelle Themen", "2. Alarmgrenzen", "3. Alarmtone", "4. Systemsprache",
        "Thema Anwenden...", "SPEICHERN", "Alarmgrenzen", "Temp Min", "Temp Max",
        "Feuch Min", "Feuch Max", "EINGABE", "WEITER", "5. Passwort Aendern",
        "Neues Passwort", "6. Touch Kalibrieren", "Touch-Kalibrierung", "Fadenkreuz beruehren", "Kalibrierung Fertig!",
        "Ungenaue Beruehrungen! Erneut versuchen.", "Passwort Bestaetigen", "Passwort zu kurz! (min 4)", "Passwoerter stimmen nicht!", "Passwort gespeichert!",
        "VERSTANDEN", "Toneinstellungen", "Bildschirmberuehrung", "Bestaetigung", "Fehlerton",
        "Alarmton", "Alles Stumm", "Sys-Lautst.", "Alarm-Lautst.", "EIN",
        "AUS", "Web-Zugriff", "Melodie", "7. Lizenz", "MIT-Lizenz",
        "AKTIV", "Stumm 120s", "Deaktivieren", "Min/Max", "Stummgeschaltet",
        "%RH", "7. Touch-Empfindlichkeit", "Touch-Empfindlichkeit", "Tippen %d/%d", "Kalibrierung Abgeschlossen!",
        "MITTELWERT", "STABW.", "Fehler", "Konfigurationsmodus", "8. Systemstatus",
        "Systemstatus",
        "9. Bildausrichtung", "Bildausrichtung", "+/-4 px justieren. Speichern setzt Touch-Kalib. zurueck."
    },

    {
        "AMBIENTE", "Impostazioni > Principale", "Impostazioni > Temi", "Impostazioni > Lingua", "ESCI",
        "APPLICA", "ANNULLA", "Autenticazione di Sicurezza", "ACCESSO BLOCCATO", "Riavvio necessario",
        "Tentativi Superati", "Attendere %ld secondi...", "Password non valida!", "Caricamento...", "Lettura Cronologia...",
        "Nessun Dato", "MASSIMO", "MINIMO", "Temperatura", "Umidita",
        "GENERA GRAFICO", "1. Temi Visivi", "2. Limiti di Allarme", "3. Suoni di Allarme", "4. Lingua del Sistema",
        "Applicazione Tema...", "SALVA", "Limiti di Allarme", "Temp Min", "Temp Max",
        "Umid Min", "Umid Max", "INVIO", "SALTA", "5. Cambia Password",
        "Nuova Password", "6. Calibra Touch", "Calibrazione Touch", "Tocca il mirino", "Calibrazione Completata!",
        "Tocchi imprecisi! Riprova.", "Conferma Password", "Password troppo corta! (min 4)", "Password non corrispondono!", "Password salvata!",
        "CAPITO", "Impostaz. Suoni", "Tocco Schermo", "Conferma", "Suono Errore",
        "Suono Allarme", "Silenzia Tutto", "Vol. Sistema", "Vol. Allarme", "SI",
        "NO", "Accesso Web", "Melodia", "7. Licenza", "Licenza MIT",
        "ATTIVO", "Silenzia 120s", "Disattiva", "Min/Max", "Silenziato",
        "%RH", "7. Sensibilita Touch", "Sensibilita Touch", "Tocca %d/%d", "Calibrazione Completata!",
        "MEDIA", "DEV. STD.", "Errore", "Modalita Configurazione", "8. Stato del Sistema",
        "Stato del Sistema",
        "9. Allineamento Schermo", "Allineamento Schermo", "Regola +/-4 px. Salvando si resetta il tocco."
    },

    {
        "OKRUZHENIE", "Nastroyki > Glavnaya", "Nastroyki > Temy", "Nastroyki > Yazyk", "VYKHOD",
        "PRIMENIT", "OTMENA", "Autentifikaciya Bezopasnosti", "DOSTUP ZABLOKIROVAN", "Trebuetsya perezagruzka",
        "Popytki Prevysheny", "Zhdite %ld sekund...", "Nevernyy parol!", "Zagruzka...", "Chtenie Istorii...",
        "Net Dannykh", "MAKSIMUM", "MINIMUM", "Temperatura", "Vlazhnost",
        "POSTROIT GRAFIK", "1. Vizualnye Temy", "2. Predely Signalizacii", "3. Zvuki Signalizacii", "4. Yazyk Sistemy",
        "Primenenie Temy...", "SOKHRANIT", "Predely Signalizacii", "Temp Min", "Temp Maks",
        "Vlazh Min", "Vlazh Maks", "VVOD", "PROPUSTIT", "5. Smena Parolya",
        "Novyy Parol", "6. Kalibrovka Tachskrina", "Kalibrovka Tachskrina", "Kosnityes perekrestiya", "Kalibrovka Zavershena!",
        "Netochnyye kasaniya! Povtorite.", "Podtverdite Parol", "Parol slishkom korotkiy! (min 4)", "Paroli ne sovpadayut!", "Parol sokhranyon!",
        "PONYATNO", "Nastroyki Zvukov", "Kasanie Ekrana", "Podtverzhdenie", "Zvuk Oshibki",
        "Zvuk Signalizacii", "Vykl. Vse Zvuki", "Sis. Gromk.", "Alarm Gromk.", "VKL",
        "VYKL", "Veb Dostup", "Melodiya", "7. Licenziya", "Licenziya MIT",
        "AKTIVNO", "Tishina 120s", "Otklyuchit", "Min/Maks", "Otklyucheno",
        "%RH", "7. Chuvstvitelnost", "Chuvstvitelnost Kasaniya", "Kasanie %d/%d", "Kalibrovka Zavershena!",
        "SREDNEE", "STD. OTKL.", "Oshibka", "Rezhim Nastroyki", "8. Sostoyanie Sistemy",
        "Sostoyanie Sistemy",
        "9. Vyravnivanie Ekrana", "Vyravnivanie Ekrana", "Nastroyka +/-4 px. Sohranenie sbrosit kalibrovku."
    },

    {
        "HUANJING", "Shezhi > Zhuyao", "Shezhi > Zhuti", "Shezhi > Yuyan", "TUICHU",
        "YINGYONG", "QUXIAO", "Anquan Yanzheng", "FANGWEN BEISUODING", "Xuyao Chongqi",
        "Changshi Chaoguo", "Qing Dengdai %ld Miao...", "Mima Wuxiao!", "Jiazai Zhong...", "Duqu Lishi...",
        "Wu Shuju", "ZUIDA", "ZUIXIAO", "Wendu", "Shidu",
        "SHENGCHENG TUBIAO", "1. Shijue Zhuti", "2. Baojing Xianzhi", "3. Baojing Shengyin", "4. Xitong Yuyan",
        "Yingyong Zhuti...", "BAOCUN", "Baojing Xianzhi", "Wen Min", "Wen Zui",
        "Shi Min", "Shi Zui", "QUEREN", "TIAOGUO", "5. Xiugai Mima",
        "Xin Mima", "6. Chuping Jiaozhun", "Chuping Jiaozhun", "Qing Chumu Shizi", "Jiaozhun Wancheng!",
        "Chumu Bu Jingque! Qing Chongshi.", "Queren Mima", "Mima Tai Duan! (min 4)", "Mima Bu Yizhi!", "Mima Yi Baocun!",
        "MINGBAI", "Shengyin Shezhi", "Chuping Chumu", "Queren Shengyin", "Cuowu Shengyin",
        "Baojing Shengyin", "Jingyin Quanbu", "Xit. Yinliang", "Baoj. Yinliang", "KAI",
        "GUAN", "Web Fangwen", "Xuanlv", "7. Xuke Zheng", "MIT Xukezheng",
        "QIYONG", "Jingyin 120m", "Tingzhi", "Min/Max", "Yi Jingyin",
        "%RH", "7. Chuping Lingmindu", "Chuping Lingmindu", "Dianji %d/%d", "Jiaozhun Wancheng!",
        "PINGJUN", "BIAOZHUN", "Cuowu", "Peizhimoshi", "8. Xitong Zhuangtai",
        "Xitong Zhuangtai",
        "9. Pingmu Duiqi", "Pingmu Duiqi", "Tiaozheng +/-4 px. Baocun hou xu chongxin jiaozhun."
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
 * Se o texto original já cabe, é copiado integralmente para out.
 * Caso contrário, remove caracteres do final e acrescenta "..." de modo
 * que o resultado caiba na largura máxima. A fonte já deve estar setada
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

    /* Medir largura das reticências */
    uint16_t ellW, ellH;
    gfx->getTextBounds("...", 0, 0, &bx, &by, &ellW, &ellH);
    int16_t targetW = maxPixelW - (int16_t)ellW;
    if (targetW < 0) targetW = 0;

    /* Busca binária do comprimento máximo que cabe */
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

    /* Remove espaços finais antes das reticências */
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
    bool heavy = (_uiMode == MODE_GRAPH_LOADING || _uiMode == MODE_GRAPH_VIEW
                  || _uiMode == MODE_GRAPH_DETAIL);
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
        LOG_CODE(LOG_ERROR, "DSP", DSP_FORCE_UNPAUSE, prev, "forceUnpause: refCount=" + String(prev));
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
    _headerShowName = false;
    _headerNameTimer = 0;
    __dmb();
    _repaintGraph = true;
    mutex_exit(&_stateMutex);
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*                        CALENDÁRIO DE HISTÓRICO                            */
/* ─────────────────────────────────────────────────────────────────────────── */

void DisplayManager::showCalendar(int year, int month, uint32_t daysMask) {
    mutex_enter_blocking(&_stateMutex);
    _calYear = year;
    _calMonth = month;
    _calDaysMask = daysMask;
    _uiMode = MODE_CALENDAR;
    __dmb();
    _repaintCalendar = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setCalendarDays(uint32_t daysMask) {
    mutex_enter_blocking(&_stateMutex);
    _calDaysMask = daysMask;
    _repaintCalendar = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setGraphNavOffset(int offset) {
    mutex_enter_blocking(&_stateMutex);
    _graphNavOffset = offset;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setBootStatus(String msg, bool showSkip) {
    mutex_enter_blocking(&_stateMutex);
    if (msg.length() > 0) {
        for (int i = 0; i < 4; i++) strcpy(_sharedState.bootLogs[i], _sharedState.bootLogs[i+1]);
        safeCopy(_sharedState.bootLogs[4], msg.c_str(), sizeof(_sharedState.bootLogs[4]));
    }
    _sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::replaceBootStatus(String msg, bool showSkip) {
    mutex_enter_blocking(&_stateMutex);
    if (msg.length() > 0) {
        safeCopy(_sharedState.bootLogs[4], msg.c_str(), sizeof(_sharedState.bootLogs[4]));
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
        if (username) safeCopy(_webBusyUser, username, sizeof(_webBusyUser));
        else safeCopy(_webBusyUser, "web", sizeof(_webBusyUser));
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
    safeCopy(_sharedState.slotName, name.c_str(), sizeof(_sharedState.slotName)); _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setSlotMinMax(float minT, float maxT) {
    _slotMinTemp = minT;
    _slotMaxTemp = maxT;
}

void DisplayManager::setSystemStatus(int rssi, bool bt, String timeStr) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.wifiRssi = rssi; _sharedState.btActive = bt;
    safeCopy(_sharedState.timeString, timeStr.c_str(), sizeof(_sharedState.timeString)); _isDirty = true;
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


    _tft->fillRect(4, 4, 312, 48, RGB565(180, 30, 30));
    _tft->setFont(&FreeSansBold12pt7b);
    _tft->setTextColor(RGB565(255, 255, 255));

    char headerBuf[40];
    if (_alarmActionSlot < 0) {
        snprintf(headerBuf, sizeof(headerBuf), "! %s", tr(TR_AMBIENT));
    } else {
        /* Usar nome amigável do sensor (do sharedState) */
        mutex_enter_blocking(&_stateMutex);
        char friendlyName[32];
        safeCopy(friendlyName, _sharedState.slotName, sizeof(friendlyName));
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

    if (!_tft) _tft = new TftWithOffset(TFT_CS, TFT_DC, TFT_RST);
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

        /* Processa toque ANTES da renderização para resposta no mesmo frame */
        handleTouch();

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
                delay(200);

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
        else if (_uiMode == MODE_GRAPH_DETAIL) {
            if (_repaintGraph) { drawGraphDetailScreen(); _repaintGraph = false; }
        }
        else if (_uiMode == MODE_CALENDAR) {
            if (_repaintCalendar) { drawCalendarScreen(); _repaintCalendar = false; }
        }

        /* ── Reverte header para data/hora após 3s de exibição do nome ── */
        if ((_uiMode == MODE_GRAPH_VIEW || _uiMode == MODE_GRAPH_DETAIL)
            && _headerShowName
            && (millis() - _headerNameTimer >= 3000))
        {
            _headerShowName = false;
            drawGraphHeaderBar();
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
                /* Wrap-safe: comparação direta com millis() falha no wrap a cada ~49,7d. */
                if (timeReached(_lockoutUntil)) forceDashboard();
            } else if (_lockoutUntil > 0) {
                if (!timeReached(_lockoutUntil)) _repaintSettings = true;
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
        else if (_uiMode == MODE_SETTINGS_TOUCH_SENS) {
            if (_repaintSettings) { drawTouchSensitivity(); _repaintSettings = false; }
            /* Após 1.5s da conclusão, avança para calibração de posição */
            if (_sensDone && (millis() - _sensDoneTime > 1500)) {
                _uiMode = MODE_SETTINGS_TOUCH_CAL;
                _calStep = 0;
                _calPhase = 0;
                _forceSettingsRedraw = true;
                _repaintSettings = true;
            }
        }
        else if (_uiMode == MODE_SETTINGS_SOUNDS) {

            if (_repaintSettings) {
                if (_inMelodySelect) drawMelodySelect();
                else                 drawSettingsSounds();
                _repaintSettings = false;
            }
        }
        else if (_uiMode == MODE_SETTINGS_STATUS) {
            /* Renderiza a cada 1 segundo ou quando forçado */
            if (_repaintSettings || (millis() - _statusLastDraw >= 1000)) {
                drawSystemStatus();
                _repaintSettings = false;
            }
        }
        else if (_uiMode == MODE_SETTINGS_LICENSE) {
            if (_repaintSettings) { drawSettingsLicense(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_DISPLAY_OFFSET) {
            if (_repaintSettings) { drawSettingsDisplayOffset(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_ALARM_ACTION) {

            if (_repaintSettings) { drawAlarmAction(); _repaintSettings = false; }
        }

        /*
         * Delay adaptativo: mínimo durante interação, maior quando ocioso.
         * - Toque ativo ou repaint pendente: 1ms (máxima responsividade)
         * - Idle: 5ms (economia de CPU para Core 0)
         */
        bool touchActive = _rawTouchState;
        bool repaintPending = _isDirty || _repaintGraph || _repaintSettings || _repaintLoading;
        delay(touchActive || repaintPending ? 1 : 2);
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
            _tft->setCursor(55, 120); _tft->print(tr(TR_AP_MODE));
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

    /* Retornar painéis ao modo normal após 30s sem toque */
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

    /* Detectar mudança de estado de alarme e redesenhar botões + painéis */
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

    /*
     * Aplica offset de alinhamento do LCD explicitamente aqui porque a rotina
     * drawRGBBitmap do Adafruit_SPITFT pode devirtualizar (ou inlinar) a chamada
     * interna de setAddrWindow dependendo da versão/toolchain, bypassando o
     * override de TftWithOffset. Aplicamos o offset nas coordenadas de destino
     * e ligamos o flag de bypass no _tft para garantir que, se o override FOR
     * chamado virtualmente, ele não aplique o offset novamente (sem bypass
     * ocorreria offset duplo em bibliotecas em que o dispatch funciona).
     */
    const int8_t ox = _tft->getOffsetX();
    const int8_t oy = _tft->getOffsetY();
    dstX += ox;
    dstY += oy;

    int16_t cw = canvas->width();
    _tft->setOffsetBypass(true);
    if (w == cw) {
        _tft->drawRGBBitmap(dstX, dstY, canvas->getBuffer(), w, h);
    } else {
        uint16_t* buf = canvas->getBuffer();
        for (int16_t row = 0; row < h; row++) {
            _tft->drawRGBBitmap(dstX, dstY + row, buf + (row * cw), w, 1);
        }
    }
    _tft->setOffsetBypass(false);
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
         * Data e hora centralizadas na área disponível.
         * Formato: "dd/mm/yy - HH:MM"
         * O separador " - " fica fixo no centro; a data cresce para
         * a esquerda e a hora cresce para a direita, garantindo que
         * o texto não pule ao trocar dígitos.
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
         * Cor do NÚMERO: baseada no último resultado de envio.
         *   estado 1 ou 3 → azul (sucesso / flash de sucesso)
         *   estado 2       → vermelho (falha)
         *   estado 0       → azul (idle, nunca enviou)
         */
        uint16_t numColor = (_pktArrowState == 2) ? C_TEMP_HOT : C_ACCENT_HIGH;

        /*
         * Cor da SETA: igual ao número, exceto durante flash (estado 3)
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
             * Layout: [número][gapNum][seta][gapWifi][wifi]
             * Seta: 12px. Gap entre número e seta: 4px.
             * Gap entre seta e wifi: 3px.
             * Quando o número é largo (>=3 dígitos), o xIcon recua 1 caractere.
             */
            const int arrowTotalW = 12;
            const int gapToWifi   = 3;
            const int gapNumArrow = 4;
            int effectiveXIcon = xIcon;
            if ((int)tw > 24) effectiveXIcon -= 8;  /* recua para números grandes */

            int arrowRight = effectiveXIcon - gapToWifi;
            int arrowLeft  = arrowRight - arrowTotalW;
            int textX      = arrowLeft - gapNumArrow - (int)tw;

            /* Número — cor fixa baseada no status */
            _canvasWide->setTextColor(numColor);
            _canvasWide->setCursor(textX, 20);
            _canvasWide->print(pktBuf);

            /*
             * Seta para a direita:
             *   - Haste retangular (6×3 px) no centro vertical
             *   - Ponta triangular (6×8 px) à direita
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

            /* Reposicionar wifi se necessário */
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

    /* Card do ambiente (painel com dupla moldura) — insetado em 4 px para
     * manter 4 px de margem em cada lado horizontal do display, absorvendo
     * o offset de alinhamento de até ±4H sem perda de borda. A altura e
     * posição vertical ficam inalteradas: o topo em y=35 e a altura de 75
     * já terminam em y=110 (bem dentro de y≤236). */
    static constexpr int16_t CARD_X = 4, CARD_Y = 35;
    static constexpr int16_t CARD_W = 312, CARD_H = 75, CARD_R = 12;

    bool ambAlarm = (_alarmAmbientTemp || _alarmAmbientHum) && _alarmFlashPhase;
    uint16_t borderColor = ambAlarm ? RGB565(255, 60, 60) : C_ACCENT_HIGH;
    uint16_t cardBg = isRed ? RGB565(180, 30, 30) : C_CARD_BG;

    if (_ambientShowMinMax) {
        /* Rastrear transição de modo (sem limpeza prévia — blits cobrem 100%) */
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

        /* Posições calculadas dinamicamente */
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

        /* Posição fixa da gota: pior caso "100" + 3px gap + sufixo, a 8px do botão */
        uint16_t numMaxW;
        _canvasWide->getTextBounds("100", 0, 0, &x1, &y1, &numMaxW, &h_bound);
        int worstNumX = BTN_X - 8 - (int)sufW - 3 - (int)numMaxW;
        const int DROP_FIX = worstNumX - 6;

        /* Blit 1: Título (20px) — com cantos superiores + bordas */
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

        /* Blit 2: Mín + Máx juntas (43px) */
        {
            _canvasWide->fillScreen(cardBg);

            /* ---- Linha Mín (y=0..20) ---- */
            {
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 15);
                _canvasWide->print(tr(TR_MIN_LBL));

                /* Termômetro mini melhorado (escala proporcional do normal) */
                int tx = THERM_X, ty = 0;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);       /* base (contorno) */
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);  /* haste (contorno) */
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, cardBg); /* haste (vazio) */
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, cardBg);      /* base (vazio) */
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);     /* mercúrio (coluna) */
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);     /* mercúrio (bolha) */
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
                /* Posicionar de trás para frente: sufixo termina a 8px do botão */
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

            /* ---- Linha Máx (y=22..42) ---- */
            {
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 37);
                _canvasWide->print(tr(TR_MAX_LBL));

                /* Termômetro mini melhorado */
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

            /* Botão de gráfico — altura total das duas linhas */
            _canvasWide->fillRoundRect(BTN_X, 1, BTN_W, 42, 8, C_ACCENT);
            {
                int cx = BTN_X + BTN_W / 2;
                int cy = 22;
                /* Barras arredondadas do gráfico */
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

            /* Bordas laterais (strip intermediária, sem cantos) */
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
        /* Rastrear transição de modo */
        _ambientLastMinMax = false;

        /* =============================================================
         * MODO NORMAL — layout com ícones grandes
         * ============================================================= */
        uint16_t leftBg  = leftRed  ? RGB565(180, 30, 30) : C_CARD_BG;
        uint16_t rightBg = rightRed ? RGB565(180, 30, 30) : C_CARD_BG;

        /* Strip 1: Nome centralizado (20px) — mesma posição do min/max */
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

        /* Strip 2: Gap para centralizar conteúdo (8px) */
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
                _canvasWide->print(tr(TR_ERROR_LBL));
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
                /* Ícone de termômetro — por último */
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
                _canvasWide->print(tr(TR_ERROR_LBL));
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
                /* Ícone de gota */
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


    /* Card do slot selecionado (segundo painel com dupla moldura do dashboard),
     * posicionado abaixo do card do ambiente. Mesmo inset horizontal de 4 px
     * para garantir 4 px de margem em cada lado. */
    static constexpr int16_t CARD_X = 4, CARD_Y = 115;
    static constexpr int16_t CARD_W = 312, CARD_H = 75, CARD_R = 12;


    bool slotAlarm = isSlotAlarming(slotIdx) && _alarmFlashPhase;
    uint16_t borderColor = slotAlarm ? RGB565(255, 60, 60) : C_ACCENT_HIGH;

    if (_slotShowMinMax) {
        /* Rastrear transição de modo */
        _slotLastMinMax = true;

        /* =============================================================
         * MODO MIN/MAX — 3 blits com moldura incorporada
         * Slot não tem umidade.
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

        /* Blit 2: Mín + Máx juntas (43px) */
        {
            _canvasWide->fillScreen(panelBg);

            /* ---- Linha Mín (y=0..20) ---- */
            {
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 15);
                _canvasWide->print(tr(TR_MIN_LBL));

                /* Termômetro mini melhorado */
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

            /* ---- Linha Máx (y=22..42) ---- */
            {
                _canvasWide->setFont(&FreeSansBold9pt7b);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 37);
                _canvasWide->print(tr(TR_MAX_LBL));

                /* Termômetro mini melhorado */
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

            /* Botão de gráfico */
            _canvasWide->fillRoundRect(BTN_X, 1, BTN_W, 42, 8, C_ACCENT);
            {
                int cx = BTN_X + BTN_W / 2;
                int cy = 22;
                /* Barras arredondadas do gráfico */
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

            /* Bordas laterais (strip intermediária, sem cantos) */
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
        /* Forçar redraw do nome na transição min/max → normal */
        if (_slotLastMinMax) forceNameRedraw = true;
        _slotLastMinMax = false;

        /* =============================================================
         * MODO NORMAL — temperatura centralizada com ícone grande
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

        /* Strip de gap para centralizar conteúdo (8px) */
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
            _canvasWide->getTextBounds(tr(TR_ERROR_LBL), 0, 0, &ex1, &ey1, &ew, &eh);
            _canvasWide->setCursor((CARD_W - (int)ew) / 2, 28);
            _canvasWide->print(tr(TR_ERROR_LBL));
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

            /* Ícone de termômetro — por último */
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
    /* h=41 em vez de 45 garante 4 px de margem inferior (y+h=236 ≤ 236). Os
     * botões ocupam apenas linhas 0..39 do canvas, então as linhas 41..44 não
     * blitadas estavam vazias. */
    blitCanvas(_canvasWide, 0, 195, 320, 41);
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

    /*
     * Layout: 5 botões com ícones pixel-art (60×40 cada, gap=4)
     *   [◀ Past] [▶ Future] [📅 Cal] [🔍+ ZoomIn] [🔍- ZoomOut]
     * Total: 5×60 + 4×4 = 316px, startX=2.
     */
    const int btnW = 60, btnH = 40, btnR = 12, gap = 4, startX = 2;
    const char* ranges[] = {"1H", "6H", "12H", "24H", "7D"};

    bool canFwd     = (_graphNavOffset < 0);
    bool canZoomIn  = (_graphData.timeRange > 0);   /* 0=1H é max zoom   */
    bool canZoomOut = (_graphData.timeRange < 4);   /* 4=7D é min zoom   */

    GFXcanvas16* cv = _canvasWide;
    cv->fillScreen(C_BG_MAIN);

    /* Helper: desenha fundo do botão e retorna X */
    auto btnBase = [&](int idx, bool enabled) -> int {
        int x = startX + idx * (btnW + gap);
        uint16_t bg = enabled ? C_CARD_BG : C_BG_MAIN;
        cv->fillRoundRect(x, 0, btnW, btnH, btnR, bg);
        if (!enabled) cv->drawRoundRect(x, 0, btnW, btnH, btnR, C_TEXT_OFF);
        return x;
    };

    int cx, cy;

    /* ════ 0: Passado (◀◀) ════ */
    {
        int x = btnBase(0, true);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = C_ACCENT_HIGH;

        /* Chevron duplo esquerdo */
        cv->drawLine(cx + 2, cy - 7, cx - 5, cy, ic);
        cv->drawLine(cx - 5, cy, cx + 2, cy + 7, ic);
        cv->drawLine(cx + 3, cy - 7, cx - 4, cy, ic);
        cv->drawLine(cx - 4, cy, cx + 3, cy + 7, ic);

        cv->drawLine(cx + 8, cy - 7, cx + 1, cy, ic);
        cv->drawLine(cx + 1, cy, cx + 8, cy + 7, ic);
        cv->drawLine(cx + 9, cy - 7, cx + 2, cy, ic);
        cv->drawLine(cx + 2, cy, cx + 9, cy + 7, ic);
    }

    /* ════ 1: Futuro (▶▶) ════ */
    {
        int x = btnBase(1, canFwd);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = canFwd ? C_ACCENT_HIGH : C_TEXT_OFF;

        /* Chevron duplo direito */
        cv->drawLine(cx - 8, cy - 7, cx - 1, cy, ic);
        cv->drawLine(cx - 1, cy, cx - 8, cy + 7, ic);
        cv->drawLine(cx - 9, cy - 7, cx - 2, cy, ic);
        cv->drawLine(cx - 2, cy, cx - 9, cy + 7, ic);

        cv->drawLine(cx - 2, cy - 7, cx + 5, cy, ic);
        cv->drawLine(cx + 5, cy, cx - 2, cy + 7, ic);
        cv->drawLine(cx - 3, cy - 7, cx + 4, cy, ic);
        cv->drawLine(cx + 4, cy, cx - 3, cy + 7, ic);
    }

    /* ════ 2: Calendário (📅) ════ */
    {
        int x = btnBase(2, true);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = C_ACCENT;
        int gx = cx - 8, gy = cy - 8;

        /* Corpo do calendário 16×16 */
        cv->drawRoundRect(gx, gy + 2, 16, 14, 2, ic);

        /* Barra de título preenchida */
        cv->fillRect(gx + 1, gy + 3, 14, 4, ic);

        /* Alças superiores */
        cv->drawFastVLine(gx + 4,  gy, 4, ic);
        cv->drawFastVLine(gx + 11, gy, 4, ic);

        /* Grade interna: 3 colunas × 2 linhas de pontos */
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 3; c++) {
                cv->fillRect(gx + 2 + c * 5, gy + 9 + r * 4, 3, 2, ic);
            }
        }
    }

    /* ════ 3: Zoom In (🔍+) ════ */
    {
        int x = btnBase(3, canZoomIn);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = canZoomIn ? C_TEMP_OK : C_TEXT_OFF;

        /* Lupa */
        int lx = cx - 3, ly = cy - 3, lr = 8;
        cv->drawCircle(lx, ly, lr, ic);
        cv->drawCircle(lx, ly, lr - 1, ic);

        /* Haste diagonal */
        cv->drawLine(lx + 6, ly + 5, lx + 11, ly + 10, ic);
        cv->drawLine(lx + 5, ly + 6, lx + 10, ly + 11, ic);

        /* Símbolo + */
        cv->drawFastHLine(lx - 4, ly, 9, ic);
        cv->drawFastVLine(lx, ly - 4, 9, ic);

        /* Label do próximo range (zoom in = range-1) */
        if (canZoomIn) {
            cv->setFont(NULL); cv->setTextSize(1);
            cv->setTextColor(ic);
            const char* lbl = ranges[_graphData.timeRange - 1];
            int lblW = strlen(lbl) * 6;  /* NULL font: 6px/char */
            cv->setCursor(x + (btnW - lblW) / 2, btnH - 9);
            cv->print(lbl);
        }
    }

    /* ════ 4: Zoom Out (🔍−) ════ */
    {
        int x = btnBase(4, canZoomOut);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = canZoomOut ? C_TEMP_WARM : C_TEXT_OFF;

        /* Lupa (mesmo formato) */
        int lx = cx - 3, ly = cy - 3, lr = 8;
        cv->drawCircle(lx, ly, lr, ic);
        cv->drawCircle(lx, ly, lr - 1, ic);

        /* Haste */
        cv->drawLine(lx + 6, ly + 5, lx + 11, ly + 10, ic);
        cv->drawLine(lx + 5, ly + 6, lx + 10, ly + 11, ic);

        /* Símbolo − */
        cv->drawFastHLine(lx - 4, ly, 9, ic);

        /* Label do próximo range (zoom out = range+1) */
        if (canZoomOut) {
            cv->setFont(NULL); cv->setTextSize(1);
            cv->setTextColor(ic);
            const char* lbl = ranges[_graphData.timeRange + 1];
            int lblW = strlen(lbl) * 6;
            cv->setCursor(x + (btnW - lblW) / 2, btnH - 9);
            cv->print(lbl);
        }
    }

    /* y=195 + h=btnH; evita chegar a y=240 (ver nota acima). Se btnH for 45
     * (altura do rodapé padrão), limita a 41. */
    int16_t footerH = (btnH > 41) ? 41 : (int16_t)btnH;
    blitCanvas(_canvasWide, 0, 195, 320, footerH);
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*                   HEADER DO GRÁFICO (alternância nome/data)               */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief  Desenha apenas a barra superior (28px) do gráfico.
 *
 * Alterna a cada 3 segundos entre:
 *   - Nome do sensor (ex: "Ambiente")
 *   - Intervalo de datas/horas do gráfico (ex: "06/04 14:00 - 15:00")
 *
 * Chamada pelo strip rendering no sTop==0 e pelo timer periódico no Core 1.
 * Blita diretamente em y=0, sem repintar o corpo do gráfico.
 */
void DisplayManager::drawGraphHeaderBar() {
    if (!_canvasWide) return;

    GFXcanvas16* cv = _canvasWide;
    /* Fundo do header cobre o canvas inteiro da blit (0..27). A margem de
     * 4 px no topo é obtida via deslocamento do destino da blit (ver fim). */
    cv->fillRect(0, 0, 320, 28, C_CARD_BG);
    cv->setFont(&FreeSansBold9pt7b);

    /* ── Pill do range atual no canto esquerdo ── */
    int contentStartX = 4;
    {
        const char* ranges[] = {"1H", "6H", "12H", "24H", "7D"};
        const char* rLabel = ranges[_graphData.timeRange];
        int16_t rx, ry; uint16_t rw, rh;
        cv->getTextBounds(rLabel, 0, 0, &rx, &ry, &rw, &rh);
        int pillW = rw + 12;
        cv->fillRoundRect(4, 4, pillW, 20, 8, C_ACCENT);
        cv->setTextColor(C_BG_MAIN);
        cv->setCursor(10 - rx, 19);
        cv->print(rLabel);
        contentStartX = 4 + pillW + 4;  /* Espaço após o pill */
    }

    /* Área útil para texto central: contentStartX .. 280 */
    int centerZone = 280 - contentStartX;

    if (_headerShowName) {
        /* ── Toque no header: nome do sensor por 3 segundos ── */
        cv->setTextColor(C_TEXT_MAIN);
        int16_t bx, by; uint16_t bw, bh;
        cv->getTextBounds(_graphData.title, 0, 0, &bx, &by, &bw, &bh);
        int tx = contentStartX + (centerZone - (int)bw) / 2 - bx;
        if (tx < contentStartX) tx = contentStartX;
        cv->setCursor(tx, 20);
        cv->print(_graphData.title);
    } else if (_graphData.tsCutoff > 0 && _graphData.tsEnd > 0) {
        /*
         * Intervalo de datas centralizado.
         * Mostra a janela temporal completa (tsCutoff..tsEnd),
         * não apenas o range dos dados disponíveis.
         */
        char dateBuf[32];
        struct tm tmFirst, tmLast;
        localtime_r(&_graphData.tsCutoff, &tmFirst);
        localtime_r(&_graphData.tsEnd,    &tmLast);

        bool sameDay = (tmFirst.tm_mday == tmLast.tm_mday
                     && tmFirst.tm_mon  == tmLast.tm_mon);

        if (sameDay) {
            snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d  %02d:%02d - %02d:%02d",
                     tmFirst.tm_mday, tmFirst.tm_mon + 1,
                     tmFirst.tm_hour, tmFirst.tm_min,
                     tmLast.tm_hour,  tmLast.tm_min);
        } else {
            snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d %02d:%02d - %02d/%02d %02d:%02d",
                     tmFirst.tm_mday, tmFirst.tm_mon + 1,
                     tmFirst.tm_hour, tmFirst.tm_min,
                     tmLast.tm_mday,  tmLast.tm_mon + 1,
                     tmLast.tm_hour,  tmLast.tm_min);
        }

        uint16_t dateColor = (_graphData.count >= 2) ? C_ACCENT_HIGH : C_TEXT_SUB;
        cv->setTextColor(dateColor);
        int16_t bx, by; uint16_t bw, bh;
        cv->getTextBounds(dateBuf, 0, 0, &bx, &by, &bw, &bh);
        int tx = contentStartX + (centerZone - (int)bw) / 2 - bx;
        if (tx < contentStartX) tx = contentStartX;
        cv->setCursor(tx, 20);
        cv->print(dateBuf);
    } else {
        /* Sem dados e sem timestamps de referência */
        cv->setTextColor(C_TEXT_SUB);
        cv->setCursor(contentStartX, 20);
        cv->print(_graphData.title);
    }

    /* Botão X (fechar) — posição original (284, 2, 32, 24). x+w=316 já está na
     * fronteira segura de 4 px à direita; o y=2 é absorvido pela blit abaixo. */
    cv->fillRoundRect(284, 2, 32, 24, 6, C_TEMP_WARM);
    cv->setFont(&FreeSansBold9pt7b);
    cv->setTextColor(C_BG_MAIN);
    cv->setCursor(293, 19);
    cv->print("X");

    /* dstY=4 afasta o header 4 px do topo físico — suporta offset de display
     * -4V sem clip do conteúdo. */
    blitCanvas(cv, 0, 4, 320, 28);
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*                     TELA DE CALENDÁRIO DE HISTÓRICO                       */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief  Desenha o calendário mensal com indicadores de dias com dados.
 *
 * Layout (320×240):
 *   Header (0..27):  [◀ Mês]  "Abr 2026"  [Mês ▶]
 *   Grid (28..194):  Cabeçalho D S T Q Q S S + grade 6×7
 *   Bottom (195..239): [◀ Mês] [Hoje] [Mês ▶]
 *
 * Dias com dados recebem bolinha azul (C_ACCENT).
 * Dia atual destacado com fundo semitransparente.
 * Toque num dia com dados envia EVT_CALENDAR_DAY.
 */
void DisplayManager::drawCalendarScreen() {
    if (!_canvasWide) return;

    const char* monthNames[] = {
        "Jan","Fev","Mar","Abr","Mai","Jun",
        "Jul","Ago","Set","Out","Nov","Dez"
    };
    const char* dowHeaders[] = {"D","S","T","Q","Q","S","S"};

    /* Calcula primeiro dia da semana e total de dias */
    struct tm firstTm = {};
    firstTm.tm_year = _calYear - 1900;
    firstTm.tm_mon  = _calMonth - 1;
    firstTm.tm_mday = 1;
    mktime(&firstTm);
    int firstDow = firstTm.tm_wday;  /* 0 = Domingo */

    /* Dias no mês */
    struct tm lastTm = {};
    lastTm.tm_year = _calYear - 1900;
    lastTm.tm_mon  = _calMonth;  /* mês seguinte */
    lastTm.tm_mday = 0;          /* dia 0 do mês seguinte = último do atual */
    mktime(&lastTm);
    int daysInMonth = lastTm.tm_mday;

    /* Dia de hoje (para highlight) */
    time_t now = time(nullptr);
    struct tm nowTm;
    localtime_r(&now, &nowTm);
    int todayDay = (nowTm.tm_year + 1900 == _calYear &&
                    nowTm.tm_mon + 1 == _calMonth) ? nowTm.tm_mday : -1;

    /* ═══ STRIP RENDERING ═══ */
    GFXcanvas16* cv = _canvasWide;
    const int sH = 45;

    for (int s = 0; s * sH < 195; s++) {
        int sTop = s * sH;
        int h = sH;
        if (sTop + h > 195) h = 195 - sTop;

        cv->fillScreen(C_BG_MAIN);

        /* ── Header (y=0..27) ── */
        if (sTop == 0) {
            cv->fillRect(4, 4, 312, 28, C_CARD_BG);

            /* Botão ◀ mês */
            cv->setFont(&FreeSansBold12pt7b);
            cv->setTextColor(C_ACCENT_HIGH);
            cv->setCursor(8, 22);
            cv->print("<");

            /* Título "Abr 2026" */
            char titleBuf[16];
            snprintf(titleBuf, sizeof(titleBuf), "%s %d",
                     monthNames[_calMonth - 1], _calYear);
            cv->setFont(&FreeSansBold9pt7b);
            cv->setTextColor(C_TEXT_MAIN);
            int16_t bx, by; uint16_t bw, bh;
            cv->getTextBounds(titleBuf, 0, 0, &bx, &by, &bw, &bh);
            cv->setCursor(160 - bw / 2 - bx, 20);
            cv->print(titleBuf);

            /* Botão ▶ mês */
            cv->setFont(&FreeSansBold12pt7b);
            cv->setTextColor(C_ACCENT_HIGH);
            cv->setCursor(298, 22);
            cv->print(">");

            /* Botão X (voltar ao gráfico) — y=4 garante 4 px de margem no topo */
            cv->fillRoundRect(270, 4, 24, 24, 6, C_TEMP_WARM);
            cv->setFont(&FreeSansBold9pt7b);
            cv->setTextColor(C_BG_MAIN);
            cv->setCursor(277, 21);
            cv->print("X");
        }

        /* ── Cabeçalho dos dias da semana (y=30..42) ── */
        if (sTop <= 30 && sTop + h > 30) {
            int ry = 34 - sTop;
            cv->setFont(NULL);
            cv->setTextSize(1);
            cv->setTextColor(C_TEXT_OFF);
            for (int d = 0; d < 7; d++) {
                cv->setCursor(10 + d * 44, ry);
                cv->print(dowHeaders[d]);
            }
        }

        /* ── Grade de dias (y=44..190) ── */
        const int gridStartY = 46;
        const int cellW = 44, cellH = 24;

        for (int cell = 0; cell < 42; cell++) {
            int dayNum = cell - firstDow + 1;
            if (dayNum < 1 || dayNum > daysInMonth) continue;

            int row = cell / 7;
            int col = cell % 7;
            int cx = 10 + col * cellW + cellW / 2;
            int cy = gridStartY + row * cellH + cellH / 2;

            /* Verifica se este dia está na strip atual */
            if (cy - 8 >= sTop + h || cy + 12 < sTop) continue;

            int ry = cy - sTop;  /* Coordenada relativa ao canvas */

            bool hasData = (_calDaysMask & (1UL << dayNum)) != 0;
            bool isToday = (dayNum == todayDay);

            /* Highlight de hoje */
            if (isToday) {
                cv->fillRoundRect(cx - 16, ry - 9, 32, 20, 5, C_ACCENT);
                cv->setTextColor(C_BG_MAIN);
            } else {
                cv->setTextColor(hasData ? C_TEXT_MAIN : C_TEXT_OFF);
            }

            /* Número do dia */
            cv->setFont(&FreeSansBold9pt7b);
            cv->setTextSize(1);
            char dayStr[4];
            snprintf(dayStr, sizeof(dayStr), "%d", dayNum);
            int16_t bx, by; uint16_t bw, bh;
            cv->getTextBounds(dayStr, 0, 0, &bx, &by, &bw, &bh);
            cv->setCursor(cx - bw / 2 - bx, ry + bh / 2 - by - bh);
            cv->print(dayStr);

            /* Bolinha de indicador de dados */
            if (hasData && !isToday) {
                cv->fillCircle(cx, ry + 10, 2, C_ACCENT);
            }
        }

        blitCanvas(cv, 0, sTop, 320, h);
    }

    /* ── Bottom bar: [◀ Mês] [Hoje] [Mês ▶] ── */
    cv->fillScreen(C_BG_MAIN);

    /* Botão ◀ Mês */
    cv->fillRoundRect(5, 3, 98, 36, 10, C_CARD_BG);
    cv->setFont(&FreeSansBold9pt7b);
    cv->setTextColor(C_TEXT_SUB);
    cv->setCursor(20, 26);
    cv->print("< Mes");

    /* Botão Hoje */
    cv->fillRoundRect(108, 3, 104, 36, 10, C_ACCENT);
    cv->setTextColor(C_BG_MAIN);
    int16_t bx2, by2; uint16_t bw2, bh2;
    cv->getTextBounds("Hoje", 0, 0, &bx2, &by2, &bw2, &bh2);
    cv->setCursor(160 - bw2 / 2 - bx2, 26);
    cv->print("Hoje");

    /* Botão Mês ▶ */
    cv->fillRoundRect(217, 3, 98, 36, 10, C_CARD_BG);
    cv->setTextColor(C_TEXT_SUB);
    cv->setCursor(237, 26);
    cv->print("Mes >");

    /* 4 px de margem inferior: y=195 + h=41 = 236 ≤ 236 */
    blitCanvas(cv, 0, 195, 320, 41);
}

void DisplayManager::drawGraphIcon(int16_t x, int16_t y, uint16_t color) {
    _tft->fillRect(x,      y + 12, 6, 10, color);
    _tft->fillRect(x + 8,  y + 4,  6, 18, color);
    _tft->fillRect(x + 16, y + 8,  6, 14, color);
    _tft->drawLine(x, y+2, x+22, y+2, color);
}

void DisplayManager::drawStatsScreen() {
    int16_t x1, y1; uint16_t w, h_bound;

    /* Header via canvas — aparece instantâneo */
    if (_canvasWide) {
        GFXcanvas16* cv = _canvasWide;
        cv->fillScreen(C_BG_MAIN);
        cv->fillRect(4, 4, 312, 32, C_CARD_BG);
        cv->setFont(&FreeSansBold9pt7b); cv->setTextColor(C_TEXT_MAIN);
        cv->setCursor(14, 23); cv->print(_graphData.title);
        cv->fillRoundRect(280, 4, 36, 24, 6, C_TEMP_WARM);
        cv->setFont(&FreeSansBold9pt7b); cv->setTextColor(C_BG_MAIN);
        cv->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
        cv->setCursor(298 - w / 2, 23); cv->print("X");
        blitCanvas(cv, 0, 0, 320, 45);
    } else {
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(14, 23); _tft->print(_graphData.title);
        _tft->fillRoundRect(280, 4, 36, 24, 6, C_TEMP_WARM);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_BG_MAIN);
        _tft->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(298 - w / 2, 23); _tft->print("X");
    }

    /* Limpa zona abaixo do header/canvas (y=45..235) — 4px de margem inferior */
    _tft->fillRect(4, 45, 312, 191, C_BG_MAIN);


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


        drawTemp(_graphData.realMaxVal, leftX + 68, cardY + 52, C_TEMP_HOT, false);


        _tft->fillCircle(leftX + 25, cardY + 74, 3, C_HUMIDITY);
        drawHum(_currentMaxHum, leftX + 80, cardY + 80, C_HUMIDITY);


        _tft->fillRoundRect(rightX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(rightX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEMP_OK);
        _tft->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(rightX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MIN_LBL));

        drawTemp(_graphData.realMinVal, rightX + 68, cardY + 52, C_TEMP_OK, false);

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

        drawTemp(_graphData.realMaxVal, leftX + 55, cardY + 68, C_TEMP_HOT, true);


        _tft->fillRoundRect(rightX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(rightX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEMP_OK);
        _tft->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(rightX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MIN_LBL));

        drawTemp(_graphData.realMinVal, rightX + 55, cardY + 68, C_TEMP_OK, true);
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


/* =========================================================================== */
/*               HELPERS PARA GRÁFICO MELHORADO                              */
/* =========================================================================== */

/**
 * @brief Formata float com 1 casa decimal em buffer, sem usar snprintf %f.
 *
 * O snprintf com %f no newlib-nano (RP2040) consome ~400 bytes de stack
 * internamente para conversão float→string, causando stack overflow no
 * Core 1 que tem apenas ~2KB de stack.
 * Esta função usa apenas aritmética inteira — zero consumo de stack extra.
 *
 * @param buf   Buffer de saída (mínimo 10 bytes).
 * @param size  Tamanho do buffer.
 * @param val   Valor float a formatar.
 * @return      Ponteiro para buf (para encadear).
 */
static char* fmtFloat1(char* buf, size_t size, float val) {
    if (isnan(val)) { snprintf(buf, size, "--.-"); return buf; }
    int neg = (val < 0.0f);
    if (neg) val = -val;
    int intPart = (int)val;
    int decPart = (int)((val - (float)intPart) * 10.0f + 0.5f);
    if (decPart >= 10) { intPart++; decPart = 0; }
    if (neg) snprintf(buf, size, "-%d.%d", intPart, decPart);
    else     snprintf(buf, size, "%d.%d", intPart, decPart);
    return buf;
}

/**
 * @brief Formata float com 2 casas decimais em buffer, sem usar snprintf %f.
 * @param buf   Buffer de saída (mínimo 12 bytes).
 * @param size  Tamanho do buffer.
 * @param val   Valor float a formatar.
 * @return      Ponteiro para buf.
 */
static char* fmtFloat2(char* buf, size_t size, float val) {
    if (isnan(val)) { snprintf(buf, size, "--"); return buf; }
    int neg = (val < 0.0f);
    if (neg) val = -val;
    int intPart = (int)val;
    int decPart = (int)((val - (float)intPart) * 100.0f + 0.5f);
    if (decPart >= 100) { intPart++; decPart = 0; }
    if (neg) snprintf(buf, size, "-%d.%02d", intPart, decPart);
    else     snprintf(buf, size, "%d.%02d", intPart, decPart);
    return buf;
}

/**
 * @brief Formata timestamp para labels do eixo X do gráfico.
 *
 * Para ranges curtos (1H, 6H, 12H) mostra apenas HH:MM.
 * Para ranges longos (24H, 7D) mostra DD/MM HHh.
 *
 * @param epoch      Timestamp Unix do ponto.
 * @param buf        Buffer de saída (mínimo 12 bytes).
 * @param shortRange true para formato curto (HH:MM), false para longo (DD/MM HHh).
 */
void DisplayManager::formatGraphTime(time_t epoch, char* buf, bool shortRange) {
    struct tm ti;
    localtime_r(&epoch, &ti);
    /* Sempre exibe data e hora completas, independente do intervalo */
    (void)shortRange;
    snprintf(buf, 12, "%02d/%02d %02d:%02d", ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min);
}

/**
 * @brief Desenha marcador de diamante (losango) com label de valor flutuante.
 *
 * O diamante tem ~4px de raio. O label é posicionado acima ou abaixo
 * conforme o parâmetro 'above', com flip automático se ultrapassar
 * os limites verticais da área do gráfico.
 *
 * @param cx        Coordenada X do centro do diamante.
 * @param cy        Coordenada Y do centro do diamante.
 * @param color     Cor do marcador e do label.
 * @param value     Valor numérico para exibir no label.
 * @param above     true = label acima do ponto, false = abaixo.
 * @param unit      Sufixo da unidade (ex: "C", "%").
 * @param graphTop  Limite superior da área do gráfico (clip).
 * @param graphBot  Limite inferior da área do gráfico (clip).
 */
void DisplayManager::drawPeakMarker(int16_t cx, int16_t cy, uint16_t color,
                                     float value, bool above, const char* unit,
                                     int16_t graphTop, int16_t graphBot) {
    /* Clamp vertical para não sair da área do gráfico */
    if (cy < graphTop + 3) cy = graphTop + 3;
    if (cy > graphBot - 3) cy = graphBot - 3;

    /* Diamante preenchido (losango 4px de raio) */
    const int r = 4;
    for (int dy = -r; dy <= r; dy++) {
        int span = r - abs(dy);
        _tft->drawFastHLine(cx - span, cy + dy, span * 2 + 1, color);
    }
    /* Pixel central para contraste */
    _tft->drawPixel(cx, cy, C_BG_MAIN);

    /* Formata label de valor */
    static char valBuf[16];
    static char fBuf[10];
    fmtFloat1(fBuf, sizeof(fBuf), value);
    snprintf(valBuf, sizeof(valBuf), "%s%s", fBuf, unit);

    _tft->setFont(NULL);
    _tft->setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    _tft->getTextBounds(valBuf, 0, 0, &bx, &by, &bw, &bh);

    /* Posicionamento vertical com flip automático */
    int16_t labelX = cx - (int16_t)(bw / 2);
    int16_t labelY;
    if (above) {
        labelY = cy - r - (int16_t)bh - 3;
        if (labelY < graphTop) labelY = cy + r + 3;    /* Flip para baixo */
    } else {
        labelY = cy + r + 3;
        if (labelY + (int16_t)bh > graphBot) labelY = cy - r - (int16_t)bh - 3; /* Flip para cima */
    }

    /* Clamp horizontal para não sair da tela */
    if (labelX < 2) labelX = 2;
    if (labelX + (int16_t)bw > 318) labelX = 318 - (int16_t)bw;

    /* Fundo opaco para legibilidade sobre a curva */
    _tft->fillRect(labelX - 1, labelY - 1, bw + 2, bh + 2, C_BG_MAIN);
    _tft->setTextColor(color);
    _tft->setCursor(labelX, labelY);
    _tft->print(valBuf);
}


/* =========================================================================== */
/*                    TELA DE GRÁFICO DE HISTÓRICO (MELHORADA)               */
/* =========================================================================== */
/**
 * @brief Desenha a tela completa do gráfico de histórico com melhorias visuais.
 *
 * Melhorias sobre a versão anterior:
 * - Barra de info superior com badges MAX/MIN contendo valor + horário
 * - Se houver umidade, badges H.MAX e H.MIN adicionais
 * - Eixo X com 3 labels de tempo (início, meio, fim do período)
 * - Formato adaptativo: HH:MM para ≤12H, DD/MM HHh para 24H e 7D
 * - Marcadores de diamante nos pontos de pico e vale da curva
 * - Labels flutuantes nos extremos com flip automático se fora da área
 * - Eixo Y com 5 divisões e valores decimais
 * - Eixo Y da umidade com valor intermediário (topo, meio, base)
 * - Linha do gráfico com 2px de espessura
 */
void DisplayManager::drawGraphScreen() {
    __dmb();
    if (!_canvasWide) return;

    if (_graphData.count < 0 || _graphData.count > GRAPH_WIDTH) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_SUB);
        _tft->setCursor(60, 120); _tft->print(tr(TR_ERROR_LBL));
        drawPeriodButtons();
        return;
    }

    bool shortRange = (_graphData.timeRange <= 3); /* 1H..24H = HH:MM, 7D = DD/MM */
    bool hasHum = _graphData.hasHumidity && !isnan(_currentMinHum);
    bool hasData = (_graphData.count >= 2 && _graphData.idxMaxTemp >= 0);

    /*
     * Layout maximizado: gráfico ocupa header(28)..botões(195).
     * Margem Y interna de 2px — curva toca quase as bordas.
     * Labels Y: apenas MAX (topo) e MIN (base).
     */
    const int gx = 30;                  /* Margem esquerda (labels Y)   */
    const int gy = 30;                  /* Topo da grade                */
    const int gw = hasHum ? 250 : 285;  /* Largura da grade             */
    const int gh = 155;                 /* Altura da grade              */
    const int margin = 2;               /* Folga interna do gráfico     */
    const int timeAxisY = gy + gh + 2;  /* Labels eixo X                */

    float tempRange = 2.0f;
    float humMin = 0, humMax = 100, humRange = 5.0f;

    /*
     * Escala Y: usa realMinVal/realMaxVal calculados de TODOS os registros
     * na janela temporal, não apenas dos pontos decimados para exibição.
     * Garante que o eixo Y represente os valores extremos verdadeiros.
     */
    if (hasData) {
        tempRange = _graphData.realMaxVal - _graphData.realMinVal;
        if (tempRange < 0.001f) tempRange = 1.0f; /* Valor constante → linha no meio */
        if (hasHum) {
            humMin = _currentMinHum; humMax = _currentMaxHum;
            humRange = humMax - humMin;
            if (humRange < 0.001f) humRange = 1.0f;
        }
    }

    /* ── Pré-calcular coordenadas da curva ── */
    static int16_t pxV1[GRAPH_WIDTH], pyV1[GRAPH_WIDTH], pyV2[GRAPH_WIDTH];
    if (hasData) {
        /*
         * Posição X por índice: dados sempre preenchem toda a largura da grade.
         * Posição Y por realMinVal/realMaxVal: escala real de todos os registros.
         * Header e labels X usam tsCutoff/tsEnd para mostrar a janela temporal.
         */
        for (int i = 0; i < _graphData.count; i++) {
            pxV1[i] = gx + (int)((long)i * gw / max(1, _graphData.count - 1));

            /* Pontos NAN (sensor em erro) → pyV1 = -1 para criar buraco visível */
            if (isnan(_graphData.pointsV1[i])) {
                pyV1[i] = -1;
            } else {
                int y = gy + margin + (int)((_graphData.realMaxVal - _graphData.pointsV1[i]) / tempRange * (gh - 2 * margin));
                if (y < gy) y = gy; if (y > gy + gh) y = gy + gh;
                pyV1[i] = y;
            }

            if (hasHum && !isnan(_graphData.pointsV2[i])) {
                int yh = gy + margin + (int)((humMax - _graphData.pointsV2[i]) / humRange * (gh - 2 * margin));
                if (yh < gy) yh = gy; if (yh > gy + gh) yh = gy + gh;
                pyV2[i] = yh;
            } else {
                pyV2[i] = -1;
            }
        }
    }

    /* ── Pré-formatar textos ── */
    static char maxLbl[10], minLbl[10];
    static char humMaxLbl[8], humMinLbl[8];
    static char tBuf[12];

    if (hasData) {
        fmtFloat1(maxLbl, sizeof(maxLbl), _graphData.realMaxVal);
        fmtFloat1(minLbl, sizeof(minLbl), _graphData.realMinVal);
        if (hasHum) {
            snprintf(humMaxLbl, sizeof(humMaxLbl), "%d%%", (int)humMax);
            snprintf(humMinLbl, sizeof(humMinLbl), "%d%%", (int)humMin);
        }
    }

    /* ═══════════════════════════════════════════════════════════════ */
    /*  STRIP RENDERING: tudo no canvas 320×45                       */
    /* ═══════════════════════════════════════════════════════════════ */
    GFXcanvas16* cv = _canvasWide;
    const int sH = 45;

    for (int s = 0; s * sH < 195; s++) {
        int sTop = s * sH;
        int h = sH;
        if (sTop + h > 195) h = 195 - sTop;
        int sBot = sTop + h;

        cv->fillScreen(C_BG_MAIN);

        /* ── Header (y=0..27) — alternância nome/data via drawGraphHeaderBar ── */
        if (sTop == 0) {
            drawGraphHeaderBar();  /* Desenha header diretamente via blit y=0 */
        }

        if (hasData) {
            /* ── Eixos ── */
            if (gy < sBot && gy + gh > sTop) {
                int at = (gy > sTop) ? gy - sTop : 0;
                int ab = (gy + gh < sBot) ? gy + gh - sTop : h;
                cv->drawFastVLine(gx, at, ab - at, C_AXIS);
                if (gy + gh >= sTop && gy + gh < sBot)
                    cv->drawFastHLine(gx, gy + gh - sTop, gw, C_AXIS);
                if (hasHum)
                    cv->drawFastVLine(gx + gw, at, ab - at, C_AXIS);
            }

            /* ── Grade pontilhada horizontal (4 divisões) ── */
            for (int gi = 0; gi <= 4; gi++) {
                int lineY = gy + (gh * gi / 4);
                if (lineY >= sTop && lineY < sBot) {
                    int ry = lineY - sTop;
                    for (int x = gx + 2; x < gx + gw; x += 6) {
                        cv->drawPixel(x, ry, C_GRID);
                        cv->drawPixel(x + 1, ry, C_GRID);
                    }
                }
            }

            /* ── Grade vertical ── */
            if (gy < sBot && gy + gh > sTop) {
                int gt = (gy > sTop) ? gy - sTop : 0;
                int gb = (gy + gh < sBot) ? gy + gh - sTop : h;
                for (int x = gx; x < gx + gw; x += 40)
                    cv->drawFastVLine(x, gt, gb - gt, C_GRID);
            }

            /* ── Labels eixo Y: MAX alinhado ao topo, MIN à base da grade ── */
            cv->setFont(NULL); cv->setTextSize(1);
            int lyMax = gy;              /* Topo da grade = pico do gráfico */
            int lyMin = gy + gh - 8;     /* Base da grade = vale do gráfico */
            /* Condição de interseção: label visível se qualquer parte cruza a strip */
            if (lyMax < sBot && lyMax + 8 > sTop) {
                cv->setTextColor(C_TEMP_HOT);
                cv->setCursor(1, lyMax - sTop);
                cv->print(maxLbl);
            }
            if (lyMin < sBot && lyMin + 8 > sTop) {
                cv->setTextColor(C_TEMP_OK);
                cv->setCursor(1, lyMin - sTop);
                cv->print(minLbl);
            }

            /* ── Labels eixo Y umidade (lado direito) ── */
            if (hasHum) {
                int rxAxis = gx + gw;
                cv->setTextColor(C_HUMIDITY);
                if (lyMax < sBot && lyMax + 8 > sTop) {
                    cv->setCursor(rxAxis + 3, lyMax - sTop);
                    cv->print(humMaxLbl);
                }
                if (lyMin < sBot && lyMin + 8 > sTop) {
                    cv->setCursor(rxAxis + 3, lyMin - sTop);
                    cv->print(humMinLbl);
                }
            }

            /* ── Curva de temperatura (2px) — pula buracos (pyV1 == -1) ── */
            for (int i = 0; i < _graphData.count - 1; i++) {
                if (pyV1[i] < 0 || pyV1[i + 1] < 0) continue;  /* Buraco: sensor em erro */
                int y1 = pyV1[i], y2 = pyV1[i + 1];
                int yMn = (y1 < y2) ? y1 : y2;
                int yMx = (y1 > y2) ? y1 : y2;
                if (yMx < sTop || yMn >= sBot) continue;
                cv->drawLine(pxV1[i], y1 - sTop, pxV1[i+1], y2 - sTop, C_TEMP_HOT);
                cv->drawLine(pxV1[i], y1 - sTop + 1, pxV1[i+1], y2 - sTop + 1, C_TEMP_HOT);
            }

            /* ── Curva de umidade (1px) ── */
            if (hasHum) {
                for (int i = 0; i < _graphData.count - 1; i++) {
                    if (pyV2[i] < 0 || pyV2[i+1] < 0) continue;
                    int y1 = pyV2[i], y2 = pyV2[i + 1];
                    int yMn = (y1 < y2) ? y1 : y2;
                    int yMx = (y1 > y2) ? y1 : y2;
                    if (yMx < sTop || yMn >= sBot) continue;
                    cv->drawLine(pxV1[i], y1 - sTop, pxV1[i+1], y2 - sTop, C_HUMIDITY);
                }
            }

            /* ── Marcador último valor válido ── */
            {
                /* Busca o último ponto válido (não-NAN) para o marcador */
                int lastValidIdx = -1;
                for (int i = _graphData.count - 1; i >= 0; i--) {
                    if (pyV1[i] >= 0) { lastValidIdx = i; break; }
                }
                if (lastValidIdx >= 0) {
                    int ly = pyV1[lastValidIdx];
                    if (ly - 3 < sBot && ly + 3 >= sTop) {
                        cv->fillCircle(gx + gw, ly - sTop, 3, C_TEXT_MAIN);
                        cv->fillCircle(gx + gw, ly - sTop, 1, C_BG_MAIN);
                    }
                }
            }

            /* ── Marcadores pico/vale (diamante) — pula se ponto é NAN ── */
            auto drawDiamond = [&](int dx, int dy, uint16_t color) {
                if (dy < 0) return;  /* Ponto NAN: sem marcador */
                if (dy - 3 >= sBot || dy + 3 < sTop) return;
                int ry = dy - sTop;
                for (int dd = -3; dd <= 3; dd++) {
                    int span = 3 - abs(dd);
                    if (ry + dd >= 0 && ry + dd < h)
                        cv->drawFastHLine(dx - span, ry + dd, span * 2 + 1, color);
                }
                if (ry >= 0 && ry < h) cv->drawPixel(dx, ry, C_BG_MAIN);
            };

            if (_graphData.idxMaxTemp >= 0 && _graphData.idxMaxTemp < _graphData.count)
                drawDiamond(pxV1[_graphData.idxMaxTemp], pyV1[_graphData.idxMaxTemp], C_TEMP_HOT);
            if (_graphData.idxMinTemp >= 0 && _graphData.idxMinTemp < _graphData.count)
                drawDiamond(pxV1[_graphData.idxMinTemp], pyV1[_graphData.idxMinTemp], C_TEMP_OK);

        } else {
            /* Sem dados */
            if (120 >= sTop && 130 < sBot) {
                cv->setFont(&FreeSansBold12pt7b); cv->setTextColor(C_TEXT_SUB);
                String nd = tr(TR_NO_DATA);
                int16_t nbx, nby; uint16_t nw, nh;
                cv->getTextBounds(nd, 0, 0, &nbx, &nby, &nw, &nh);
                cv->setCursor(160 - nw / 2, 125 - sTop); cv->print(nd);
            }
        }

        /*
         * Labels eixo X (3 timestamps) — correspondem aos extremos da linha
         * desenhada (tsFirst..tsLast). O header mostra a janela completa
         * (tsCutoff..tsEnd) para contexto do zoom.
         */
        if (timeAxisY < sBot && timeAxisY + 8 > sTop && _graphData.tsFirst > 0) {
            int ry = timeAxisY - sTop;
            cv->setFont(NULL); cv->setTextSize(1); cv->setTextColor(C_TEXT_SUB);

            static char xL[6], xM[6], xR[6];
            struct tm ti;

            time_t tMid = _graphData.tsFirst + (_graphData.tsLast - _graphData.tsFirst) / 2;

            /* Primeiro ponto */
            localtime_r(&_graphData.tsFirst, &ti);
            if (shortRange) snprintf(xL, sizeof(xL), "%02d:%02d", ti.tm_hour, ti.tm_min);
            else            snprintf(xL, sizeof(xL), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
            cv->setCursor(gx, ry); cv->print(xL);

            /* Ponto médio */
            localtime_r(&tMid, &ti);
            if (shortRange) snprintf(xM, sizeof(xM), "%02d:%02d", ti.tm_hour, ti.tm_min);
            else            snprintf(xM, sizeof(xM), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
            int16_t tbx, tby; uint16_t tw, th;
            cv->getTextBounds(xM, 0, 0, &tbx, &tby, &tw, &th);
            cv->setCursor(gx + gw / 2 - (int)tw / 2, ry); cv->print(xM);

            /* Último ponto */
            localtime_r(&_graphData.tsLast, &ti);
            if (shortRange) snprintf(xR, sizeof(xR), "%02d:%02d", ti.tm_hour, ti.tm_min);
            else            snprintf(xR, sizeof(xR), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
            cv->getTextBounds(xR, 0, 0, &tbx, &tby, &tw, &th);
            cv->setCursor(gx + gw - (int)tw, ry); cv->print(xR);
        }

        blitCanvas(cv, 0, sTop, 320, h);
    }

    drawPeriodButtons();
}


/* =========================================================================== */
/*              TELA NUMÉRICA DE DETALHES DO PERÍODO                         */
/* =========================================================================== */
/**
 * @brief Desenha tela com dados numéricos legíveis do período selecionado.
 *
 * Exibe em cards grandes: MAX, MIN, AVG, σ, e período.
 * Mantém header com título/botão X e botões de período na base.
 * Toque na zona central retorna ao gráfico.
 * Todos os floats formatados via fmtFloat1/fmtFloat2 (sem snprintf %f).
 */
void DisplayManager::drawGraphDetailScreen() {
    __dmb();
    if (!_canvasWide) return;

    bool shortRange = (_graphData.timeRange <= 3); /* 1H..24H = HH:MM, 7D = DD/MM */
    bool hasHum = _graphData.hasHumidity && !isnan(_currentMinHum);
    bool isHumPage = (_detailPage == 1 && hasHum);
    uint16_t pageColor = isHumPage ? C_HUMIDITY : C_TEMP_OK;

    int16_t bx, by; uint16_t bw, bh;

    struct CardData {
        const char* label;
        char num[14];
        bool isTempUnit;  /* true = oC com circulozinho, false = tr(TR_HUM_SUFFIX) */
        char sub[12];
        uint16_t numColor;
        int icon;
    };
    static CardData cards[4];

    if (_graphData.count < 2) {
        _tft->fillRect(4, 4, 312, 191, C_BG_MAIN);
        drawGraphHeaderBar();  /* Mostra período de referência no header */
        _tft->setFont(&FreeSansBold12pt7b); _tft->setTextColor(C_TEXT_SUB);
        String nd = tr(TR_NO_DATA);
        _tft->getTextBounds(nd, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(160 - bw / 2, 120); _tft->print(nd);
        drawPeriodButtons();
        return;
    }

    /* ── Popular cards ── */
    if (!isHumPage) {
        cards[0] = { tr(TR_MAX_LBL), {0}, true, {0}, C_TEMP_OK, 0 };
        fmtFloat1(cards[0].num, sizeof(cards[0].num), _graphData.realMaxVal);
        if (_graphData.tsRealMax > 0) formatGraphTime(_graphData.tsRealMax, cards[0].sub, shortRange);

        cards[1] = { tr(TR_MIN_LBL), {0}, true, {0}, C_TEMP_OK, 1 };
        fmtFloat1(cards[1].num, sizeof(cards[1].num), _graphData.realMinVal);
        if (_graphData.tsRealMin > 0) formatGraphTime(_graphData.tsRealMin, cards[1].sub, shortRange);

        cards[2] = { tr(TR_AVG_LBL), {0}, true, {0}, C_TEMP_OK, 2 };
        fmtFloat1(cards[2].num, sizeof(cards[2].num), _graphData.avgTemp);

        cards[3] = { tr(TR_STD_LBL), {0}, true, {0}, C_TEMP_OK, 3 };
        fmtFloat2(cards[3].num, sizeof(cards[3].num), _graphData.stdTemp);
    } else {
        cards[0] = { tr(TR_MAX_LBL), {0}, false, {0}, C_HUMIDITY, 0 };
        snprintf(cards[0].num, sizeof(cards[0].num), "%d", (int)_currentMaxHum);
        if (_graphData.tsMaxHum > 0) formatGraphTime(_graphData.tsMaxHum, cards[0].sub, shortRange);

        cards[1] = { tr(TR_MIN_LBL), {0}, false, {0}, C_HUMIDITY, 1 };
        snprintf(cards[1].num, sizeof(cards[1].num), "%d", (int)_currentMinHum);
        if (_graphData.tsMinHum > 0) formatGraphTime(_graphData.tsMinHum, cards[1].sub, shortRange);

        cards[2] = { tr(TR_AVG_LBL), {0}, false, {0}, C_HUMIDITY, 2 };
        if (!isnan(_graphData.avgHum)) snprintf(cards[2].num, sizeof(cards[2].num), "%d", (int)_graphData.avgHum);
        else snprintf(cards[2].num, sizeof(cards[2].num), "--");

        cards[3] = { tr(TR_STD_LBL), {0}, false, {0}, C_HUMIDITY, 3 };
        if (!isnan(_graphData.stdHum)) fmtFloat2(cards[3].num, sizeof(cards[3].num), _graphData.stdHum);
        else snprintf(cards[3].num, sizeof(cards[3].num), "--");
    }

    /* ── Layout: 2 linhas × 2 colunas, cards maiores ── */
    const int cardW = 152, cardH = 76, cardR = 8;
    const int colL = 4, colR = 164, gapY = 4;
    const int totalH = 2 * cardH + gapY;
    const int startY = 28 + (167 - totalH) / 2;
    int rowY[2] = { startY, startY + cardH + gapY };

    /**
     * Desenha card no canvas (versão expandida).
     * - Ícone 18×18 refinado
     * - Label em FreeSansBold9pt7b, cor C_TEXT_SUB (cinza claro)
     * - Valor grande colorido (verde temp / azul hum)
     * - Unidade: temp = circulozinho "o" (NULL font) + "C" (9pt) branco
     *            hum  = tr(TR_HUM_SUFFIX) (9pt) branco
     * - Data/hora do evento na base do card (FreeSansBold9pt7b, amarelo suave)
     */
    auto drawCardOn = [&](GFXcanvas16* cv, int cx, int cy, int stripTop, int idx) {
        int ry = cy - stripTop;
        CardData& d = cards[idx];

        cv->fillRoundRect(cx, ry, cardW, cardH, cardR, C_CARD_BG);

        /* Amarelo suave para data/hora dos eventos */
        const uint16_t C_DATETIME = RGB565(190, 170, 60);

        /* ── Ícone 18×18 ── */
        int ix = cx + 6, iy = ry + 2;
        uint16_t ic = d.numColor;
        switch (d.icon) {
            case 0: { /* ▲ MAX — triângulo ascendente com contorno interno */
                cv->fillTriangle(ix, iy+16, ix+9, iy+1, ix+17, iy+16, ic);
                cv->drawTriangle(ix+2, iy+15, ix+9, iy+4, ix+15, iy+15, C_CARD_BG);
                break;
            }
            case 1: { /* ▼ MIN — triângulo descendente com contorno interno */
                cv->fillTriangle(ix, iy+1, ix+9, iy+16, ix+17, iy+1, ic);
                cv->drawTriangle(ix+2, iy+2, ix+9, iy+13, ix+15, iy+2, C_CARD_BG);
                break;
            }
            case 2: { /* ≈ MEDIA — três barras horizontais proporcionais */
                cv->fillRect(ix, iy+1,  17, 3, ic);
                cv->fillRect(ix, iy+7,  17, 3, ic);
                cv->fillRect(ix, iy+13, 17, 3, ic);
                break;
            }
            case 3: { /* σ DESVIO — curva sino refinada 18×18 */
                /* Topo da curva */
                cv->drawPixel(ix+8, iy+1, ic); cv->drawPixel(ix+9, iy+1, ic);
                cv->drawPixel(ix+7, iy+2, ic); cv->drawPixel(ix+10, iy+2, ic);
                cv->drawPixel(ix+6, iy+3, ic); cv->drawPixel(ix+11, iy+3, ic);
                /* Ombros */
                cv->drawPixel(ix+5, iy+4, ic); cv->drawPixel(ix+12, iy+4, ic);
                cv->drawPixel(ix+5, iy+5, ic); cv->drawPixel(ix+12, iy+5, ic);
                cv->drawPixel(ix+4, iy+6, ic); cv->drawPixel(ix+13, iy+6, ic);
                cv->drawPixel(ix+4, iy+7, ic); cv->drawPixel(ix+13, iy+7, ic);
                /* Corpo */
                cv->drawPixel(ix+3, iy+8, ic);  cv->drawPixel(ix+14, iy+8, ic);
                cv->drawPixel(ix+3, iy+9, ic);  cv->drawPixel(ix+14, iy+9, ic);
                cv->drawPixel(ix+2, iy+10, ic); cv->drawPixel(ix+15, iy+10, ic);
                cv->drawPixel(ix+2, iy+11, ic); cv->drawPixel(ix+15, iy+11, ic);
                /* Base larga */
                cv->drawPixel(ix+1, iy+12, ic); cv->drawPixel(ix+16, iy+12, ic);
                cv->drawPixel(ix+1, iy+13, ic); cv->drawPixel(ix+16, iy+13, ic);
                cv->drawPixel(ix,   iy+14, ic); cv->drawPixel(ix+17, iy+14, ic);
                /* Linha de base sólida */
                cv->fillRect(ix, iy+15, 18, 2, ic);
                break;
            }
        }

        /* ── Label (FreeSansBold9pt7b, cinza claro, à direita do ícone) ── */
        cv->setFont(&FreeSansBold9pt7b);
        cv->setTextColor(C_TEXT_SUB);
        cv->setCursor(ix + 22, iy + 14);
        cv->print(d.label);

        /* ── Valor + Unidade (centro vertical do card) ── */
        int vy = ry + 48;

        /* Medir largura do número */
        int16_t nb, ny2; uint16_t nw, nh;
        cv->setFont(&FreeSansBold12pt7b);
        cv->getTextBounds(d.num, 0, 0, &nb, &ny2, &nw, &nh);

        if (d.isTempUnit) {
            /*
             * Temperatura: número + "o" (circulozinho, NULL font acima) + "C" (9pt)
             * Padrão idêntico à tela principal (drawTemp).
             */
            int16_t ub2, uy3; uint16_t cw2, ch2;
            cv->setFont(&FreeSansBold9pt7b);
            cv->getTextBounds("C", 0, 0, &ub2, &uy3, &cw2, &ch2);

            /* "o" em NULL font é ~6px wide */
            int unitW = 6 + 1 + (int)cw2; /* "o" + gap + "C" */
            int totalW = (int)nw + 2 + unitW;
            int vx = cx + (cardW - totalW) / 2;

            /* Número (colorido) */
            cv->setFont(&FreeSansBold12pt7b);
            cv->setTextColor(d.numColor);
            cv->setCursor(vx, vy);
            cv->print(d.num);

            /* Circulozinho "o" (NULL font, branco, posicionado acima do baseline) */
            int oX = vx + (int)nw + 2;
            cv->setFont(NULL); cv->setTextSize(1);
            cv->setTextColor(C_TEXT_MAIN);
            cv->setCursor(oX, vy - 16);
            cv->print("o");

            /* "C" (FreeSansBold9pt7b, branco) */
            cv->setFont(&FreeSansBold9pt7b);
            cv->setTextColor(C_TEXT_MAIN);
            cv->setCursor(oX + 7, vy);
            cv->print("C");

        } else {
            /*
             * Umidade: número + tr(TR_HUM_SUFFIX) (9pt, branco)
             */
            int16_t ub2, uy3; uint16_t uw2, uh2;
            cv->setFont(&FreeSansBold9pt7b);
            cv->getTextBounds(tr(TR_HUM_SUFFIX), 0, 0, &ub2, &uy3, &uw2, &uh2);

            int totalW = (int)nw + 3 + (int)uw2;
            int vx = cx + (cardW - totalW) / 2;

            /* Número (colorido) */
            cv->setFont(&FreeSansBold12pt7b);
            cv->setTextColor(d.numColor);
            cv->setCursor(vx, vy);
            cv->print(d.num);

            /* Sufixo de umidade (branco) */
            cv->setFont(&FreeSansBold9pt7b);
            cv->setTextColor(C_TEXT_MAIN);
            cv->setCursor(vx + (int)nw + 3, vy);
            cv->print(tr(TR_HUM_SUFFIX));
        }

        /* ── Data/hora do evento (base do card, centralizado, amarelo suave) ── */
        if (d.sub[0]) {
            int16_t sx, sy; uint16_t sw, sh;
            cv->setFont(&FreeSansBold9pt7b);
            cv->setTextColor(C_DATETIME);
            cv->getTextBounds(d.sub, 0, 0, &sx, &sy, &sw, &sh);
            cv->setCursor(cx + (cardW - (int)sw) / 2, ry + cardH - 6);
            cv->print(d.sub);
        }
    };

    /* ═══════════════════════════════════════════════════════════════ */
    /*  STRIP RENDERING                                              */
    /* ═══════════════════════════════════════════════════════════════ */
    GFXcanvas16* cv = _canvasWide;
    const int sH = 45;

    for (int s = 0; s * sH < 195; s++) {
        int sTop = s * sH;
        int h = sH;
        if (sTop + h > 195) h = 195 - sTop;

        cv->fillScreen(C_BG_MAIN);

        if (sTop == 0) {
            drawGraphHeaderBar();
        }

        /* Apenas 2 linhas de cards */
        for (int r = 0; r < 2; r++) {
            int cy = rowY[r];
            if (cy < sTop + h && cy + cardH > sTop) {
                drawCardOn(cv, colL, cy, sTop, r * 2);
                drawCardOn(cv, colR, cy, sTop, r * 2 + 1);
            }
        }

        blitCanvas(cv, 0, sTop, 320, h);
    }

    drawPeriodButtons();
}




void DisplayManager::handleTouch() {
    if (!_ts->touched()) {
        /* Finger released — habilita próximo toque único */
        _touchReleased = true;

        /*
         * Detecção de release durante calibração hold-and-release.
         * Se o usuário segurou o ponto pelo tempo mínimo, registra a média
         * das amostras acumuladas ao soltar.
         */
        if (_uiMode == MODE_SETTINGS_TOUCH_CAL && _calHolding) {
            if (_calHoldReady && _calHoldSamples > 0 && _calStep < 8) {
                /* Registra ponto: média das amostras acumuladas durante o hold */
                _calRawX[_calStep] = (int16_t)(_calHoldSumX / _calHoldSamples);
                _calRawY[_calStep] = (int16_t)(_calHoldSumY / _calHoldSamples);
                _calStep++;

                if (_calStep < 8) {
                    /* Próximo ponto */
                    _repaintSettings = true;
                } else {
                    /* Todos os 8 pontos capturados — validar e calcular */
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
            _calHolding    = false;
            _calHoldReady  = false;
            _calHoldSamples = 0;
        }

        _btnHoldStartTime = 0;
        _lastPressedBtn = -1;
        if (_uiMode != MODE_DASHBOARD && !_sharedState.isBooting) {
            if (millis() - _lastTouchTime > 30000)forceDashboard();
        }
        return;
    }


    if (millis() - _lastTouchTime < 15) return;
    TS_Point p = _ts->getPoint();

    /* ── Modo de calibração de sensibilidade: threshold mínimo ──
     * Usa p.z > 50 (ruído do ADC) em vez do threshold calibrado,
     * para capturar toda a faixa de pressão do usuário.            */
    if (_uiMode == MODE_SETTINGS_TOUCH_SENS) {
        /*
         * Calibração de sensibilidade baseada em HOLD contínuo.
         *
         * O usuário pressiona e segura o crosshair. O sistema amostra
         * p.z continuamente e calcula a estabilidade rolante. Quando
         * encontra a menor pressão com leitura estável (sem oscilar),
         * define o threshold e salva.
         *
         * _sensCount       — total de amostras coletadas
         * _sensSamples[30] — buffer circular de amostras recentes
         * _sensStability   — progresso visual da barra (0..1)
         * _sensThreshold   — menor p.z estável encontrado
         */

        /* Botão CANCEL: aceita toque em qualquer pressão */
        if (p.z >= 50) {
            int16_t sx, sy;
            mapTouchPoint(p, sx, sy);
            if (sy > 195 && sx < 125) {
                if (acceptTouch(0)) { showSettingsMain(); return; }
            }
        }

        /* Após concluído, ignora toques até auto-retorno */
        if (_sensDone) return;

        /* Precisa de pressão mínima para coletar (acima do ruído do ADC) */
        if (p.z < 30) return;

        /* Amostra contínua: coleta sem exigir release */
        uint8_t idx = _sensCount % 30;
        _sensSamples[idx] = p.z;
        _sensCount++;

        /* Precisa de pelo menos 10 amostras para análise */
        if (_sensCount < 10) {
            _sensStability = (float)_sensCount / 10.0f * 0.3f;
            _repaintSettings = true;
            return;
        }

        /*
         * Análise de estabilidade rolante (últimas 10 amostras).
         * Calcula stddev/mean dos últimos 10 valores. Se < 15%,
         * a pressão atual está estável.
         */
        int startIdx = (_sensCount >= 30) ? (_sensCount % 30) : 0;
        int n = (_sensCount < 30) ? _sensCount : 30;
        if (n > 10) n = 10; /* Análise dos últimos 10 */

        float sum = 0;
        uint16_t minZ = 65535, maxZ = 0;
        int base = (int)((_sensCount - n) % 30);
        for (int i = 0; i < n; i++) {
            uint16_t v = _sensSamples[(base + i) % 30];
            sum += v;
            if (v < minZ) minZ = v;
            if (v > maxZ) maxZ = v;
        }
        float mean = sum / n;

        float varSum = 0;
        for (int i = 0; i < n; i++) {
            float d = _sensSamples[(base + i) % 30] - mean;
            varSum += d * d;
        }
        float stddev = sqrtf(varSum / n);
        float cv = (mean > 0) ? (stddev / mean) : 1.0f; /* coef. variação */

        /* Atualiza o threshold quando encontra zona estável */
        bool isStable = (cv < 0.15f) && (_sensCount >= 10);

        if (isStable) {
            /* Encontrou zona estável: threshold = menor valor estável × 0.8 */
            uint16_t candidate = (uint16_t)(minZ * 0.8f);
            if (candidate < 50) candidate = 50;

            /* Aceita se for melhor (menor) que o anterior, ou primeiro achado */
            if (_sensThreshold == 0 || candidate < _sensThreshold) {
                _sensThreshold = candidate;
            }

            /* Progresso: avança conforme tempo em zona estável */
            _sensStability += 0.02f;
            if (_sensStability > 1.0f) _sensStability = 1.0f;

            /* Após barra cheia (~2s estável): salva e conclui */
            if (_sensStability >= 1.0f) {
                _sensZThreshold = _sensThreshold;
                _sensDone = true;
                _sensDoneTime = millis();

                UiEvent ev;
                ev.type = UiEvent::EVT_SAVE_TOUCH_CAL;
                queue_try_add(&_eventQueue, &ev);
                _touchSoundPending = false;
            }
        } else {
            /* Zona instável: barra recua lentamente */
            if (_sensStability > 0.0f) _sensStability -= 0.005f;
            if (_sensStability < 0.0f) _sensStability = 0.0f;
        }

        _repaintSettings = true;
        return;
    }

    if (p.z < _sensZThreshold) return;


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
            if (!_calHolding) {
                /* Início do hold: zera acumuladores */
                _calHolding     = true;
                _calHoldReady   = false;
                _calHoldStart   = millis();
                _calHoldSumX    = 0;
                _calHoldSumY    = 0;
                _calHoldSamples = 0;
            }

            /* Acumula amostras enquanto segura */
            _calHoldSumX += p.x;
            _calHoldSumY += p.y;
            _calHoldSamples++;

            /* Após tempo mínimo de hold, sinaliza que pode soltar */
            if (!_calHoldReady && (millis() - _calHoldStart >= CAL_HOLD_MS)) {
                _calHoldReady = true;
                _repaintSettings = true; /* Redesenha crosshair verde */
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

            /* Canto direito: botão de gráfico (prioridade sobre alarme) */
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

            /* Canto direito: botão de gráfico (prioridade sobre alarme) */
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
                if (!acceptSlideTouch(10 + btnIdx)) return;
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
        /* Botão X (fechar) — canto superior direito */
        if (y < 40 && x > 284) { if (!acceptTouch(0)) return; _graphNavOffset = 0; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
        /* Toque no header — mostra nome do sensor por 3s */
        if (y < 28 && x < 284) {
            if (!acceptTouch(0)) return;
            _headerShowName = true;
            _headerNameTimer = millis();
            drawGraphHeaderBar();
            return;
        }
        /* ── Barra inferior: [◀Past][▶Fut][📅Cal][🔍+ZoomIn][🔍-ZoomOut] ── */
        if (y >= 195) {
            const int btnW = 60, gap = 4, startX = 2;
            int btn = -1;
            for (int i = 0; i < 5; i++) {
                int bx = startX + i * (btnW + gap);
                if (x >= bx && x <= bx + btnW) { btn = i; break; }
            }

            if (btn == 0) {
                /* Passado (◀) */
                if (!acceptHoldTouch(10)) return;
                UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = -1;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 1 && _graphNavOffset < 0) {
                /* Futuro (▶) — só se offset < 0 */
                if (!acceptHoldTouch(11)) return;
                UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = +1;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 2) {
                /* Calendário (📅) */
                if (!acceptTouch(0)) return;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_CALENDAR; ev.id = _graphData.sensorIdx; ev.param = 0;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 3 && _graphData.timeRange > 0) {
                /* Zoom In — range mais curto (mais detalhe) */
                if (!acceptHoldTouch(12)) return;
                int newRange = _graphData.timeRange - 1;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 4 && _graphData.timeRange < 4) {
                /* Zoom Out — range mais longo (menos detalhe) */
                if (!acceptHoldTouch(13)) return;
                int newRange = _graphData.timeRange + 1;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
                queue_try_add(&_eventQueue, &ev); return;
            }
        }
        /* Toque na zona central → detalhes de temperatura (página 0) */
        if (y >= 40 && y < 195) {
            if (!acceptTouch(10)) return;
            _detailPage = 0;
            _uiMode = MODE_GRAPH_DETAIL;
            _repaintGraph = true;
        }
    }
    else if (_uiMode == MODE_GRAPH_DETAIL) {
        /* Botão X — fechar para dashboard */
        if (y < 40 && x > 284) { if (!acceptTouch(0)) return; _graphNavOffset = 0; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
        /* Toque no header — mostra nome do sensor por 3s */
        if (y < 28 && x < 284) {
            if (!acceptTouch(0)) return;
            _headerShowName = true;
            _headerNameTimer = millis();
            drawGraphHeaderBar();
            return;
        }
        /* Barra inferior — mesma lógica do graph view */
        if (y >= 195) {
            const int btnW = 60, gap = 4, startX = 2;
            int btn = -1;
            for (int i = 0; i < 5; i++) {
                int bx = startX + i * (btnW + gap);
                if (x >= bx && x <= bx + btnW) { btn = i; break; }
            }

            if (btn == 0) {
                /* Passado (◀) */
                if (!acceptHoldTouch(10)) return;
                UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = -1;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 1 && _graphNavOffset < 0) {
                /* Futuro (▶) — só se offset < 0 */
                if (!acceptHoldTouch(11)) return;
                UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = +1;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 2) {
                /* Calendário (📅) */
                if (!acceptTouch(0)) return;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_CALENDAR; ev.id = _graphData.sensorIdx; ev.param = 0;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 3 && _graphData.timeRange > 0) {
                /* Zoom In — range mais curto (mais detalhe) */
                if (!acceptHoldTouch(12)) return;
                int newRange = _graphData.timeRange - 1;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 4 && _graphData.timeRange < 4) {
                /* Zoom Out — range mais longo (menos detalhe) */
                if (!acceptHoldTouch(13)) return;
                int newRange = _graphData.timeRange + 1;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
                queue_try_add(&_eventQueue, &ev); return;
            }
        }
        /* Toque na zona central → próxima página ou voltar ao gráfico */
        if (y >= 40 && y < 195) {
            if (!acceptTouch(10)) return;
            bool hasHumNow = _graphData.hasHumidity && !isnan(_currentMinHum);
            if (_detailPage == 0 && hasHumNow) {
                /* Temperatura → Umidade */
                _detailPage = 1;
                _repaintGraph = true;
            } else {
                /* Umidade (ou temp sem hum) → voltar ao gráfico */
                _detailPage = 0;
                _uiMode = MODE_GRAPH_VIEW;
                _repaintGraph = true;
            }
        }
    }
    /* ── CALENDÁRIO ── */
    else if (_uiMode == MODE_CALENDAR) {
        /* Botão X (voltar ao gráfico) — canto superior direito */
        if (y < 28 && x >= 270) {
            if (!acceptTouch(0)) return;
            _uiMode = MODE_GRAPH_VIEW;
            _repaintGraph = true;
            return;
        }
        /* Seta ◀ mês — header esquerdo */
        if (y < 28 && x < 30) {
            if (!acceptSlideTouch(20)) return;
            UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = -1;
            queue_try_add(&_eventQueue, &ev); return;
        }
        /* Seta ▶ mês — header direito */
        if (y < 28 && x > 290) {
            if (!acceptSlideTouch(21)) return;
            UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = +1;
            queue_try_add(&_eventQueue, &ev); return;
        }
        /* ── Grade de dias (y=44..190) ── */
        if (y >= 44 && y < 190) {
            const int gridStartY = 46, cellW = 44, cellH = 24;
            int row = (y - gridStartY) / cellH;
            int col = x / cellW;
            if (col >= 0 && col < 7 && row >= 0 && row < 6) {
                /* Calcula primeiro dia da semana */
                struct tm firstTm = {};
                firstTm.tm_year = _calYear - 1900;
                firstTm.tm_mon  = _calMonth - 1;
                firstTm.tm_mday = 1;
                mktime(&firstTm);
                int firstDow = firstTm.tm_wday;

                int cell = row * 7 + col;
                int dayNum = cell - firstDow + 1;

                /* Verifica se é dia válido com dados */
                if (dayNum >= 1 && dayNum <= 31 && (_calDaysMask & (1UL << dayNum))) {
                    if (!acceptTouch(0)) return;
                    UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_DAY;
                    ev.id = _graphData.sensorIdx;
                    ev.param = dayNum;
                    queue_try_add(&_eventQueue, &ev);
                }
            }
        }
        /* ── Barra inferior: [◀ Mês] [Hoje] [Mês ▶] ── */
        if (y >= 195) {
            if (x < 106) {
                /* ◀ Mês */
                if (!acceptSlideTouch(20)) return;
                UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = -1;
                queue_try_add(&_eventQueue, &ev);
            } else if (x >= 108 && x < 212) {
                /* Hoje — volta ao gráfico com offset 0 */
                if (!acceptTouch(0)) return;
                _graphNavOffset = 0;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = _graphData.timeRange;
                queue_try_add(&_eventQueue, &ev);
            } else if (x >= 217) {
                /* Mês ▶ */
                if (!acceptSlideTouch(21)) return;
                UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = +1;
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
                if (!acceptSlideTouch(clickedIndex)) return;
                _previewThemeIdx = actualIndex; _themePage = _previewThemeIdx / 4; _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_previewThemeIdx > 0) _previewThemeIdx--; else _previewThemeIdx = getThemeCount() - 1;
                _themePage = _previewThemeIdx / 4; _repaintSettings = true;
            } else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
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
                    /* Toque no SIM/NAO do item selecionado: toggle ou edição */
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
                        /* NAO → entra na tela de edição de limites */
                        showAlarmEdit(actualSensorId);
                    }
                } else if (mapIdx != _alarmSelection) {
                    /* Toque no nome/barra: seleciona o item */
                    if (!acceptSlideTouch(clickedIndex)) return;
                    _alarmSelection = mapIdx; _alarmPage = _alarmSelection / 4; _repaintSettings = true;
                }
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_alarmSelection > 0) _alarmSelection--; else _alarmSelection = _activeSensorCount - 1;
                _alarmPage = _alarmSelection / 4; _repaintSettings = true;
            } else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
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
                /* Decremento com hold-repeat (300ms) e aceleração */
                if (!acceptHoldTouch(10)) return;
                if (_lastPressedBtn != 0) { _btnHoldStartTime = millis(); _lastPressedBtn = 0; }
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
                /* Incremento com hold-repeat (300ms) e aceleração */
                if (!acceptHoldTouch(11)) return;
                if (_lastPressedBtn != 1) { _btnHoldStartTime = millis(); _lastPressedBtn = 1; }
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
        /* Botão de licença — acessível mesmo em lockout */
        if (y > 200 && x > 195) { if (!acceptTouch(5)) return; _licenseFromAuth = true; showSettingsLicense(); return; }
        if (_permanentLockout || !timeReached(_lockoutUntil)) return;
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
            if (mapIdx < 9 && mapIdx != _menuSelection) {
                if (!acceptSlideTouch(clickedIndex)) return;
                _menuSelection = mapIdx; _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_menuSelection > 0) _menuSelection--; else _menuSelection = 8;
                _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
                if (_menuSelection < 8) _menuSelection++; else _menuSelection = 0;
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
    else if (_uiMode == MODE_SETTINGS_DISPLAY_OFFSET) {
        /*
         * Layout dos controles (coordenadas lógicas — o próprio offset aplicado
         * ao TFT ja desloca a imagem):
         *   Pad direcional centrado em (160, 120):
         *     ▲  (130..190, 55..95)   → Y -= 1
         *     ▼  (130..190, 145..185) → Y += 1
         *     ◀  (80..140, 100..140)  → X -= 1
         *     ▶  (180..240, 100..140) → X += 1
         *   Botão de reset central (148..172, 108..132) → zera ambos
         *   Rodapé:
         *     BACK  (10..130, 200..240)   → descarta e volta
         *     APPLY (190..310, 200..240)  → dispara EVT_APPLY_DISPLAY_OFFSET
         */
        /*
         * Cada ajuste é aplicado ao TFT em tempo real via setDisplayOffset(),
         * permitindo calibração visual imediata. BACK reverte ao offset salvo;
         * APPLY dispara EVT_APPLY_DISPLAY_OFFSET (Core 0 persiste + reseta touch).
         * Toda mudança força redraw completo para evitar artefatos do conteúdo
         * desenhado com o offset anterior na tela anterior.
         */
        bool changed = false;
        if (y >= 55 && y <= 95 && x >= 130 && x <= 190) {
            if (!acceptHoldTouch(20)) return;
            if (_offsetPreviewY > -4) { _offsetPreviewY--; changed = true; }
        }
        else if (y >= 145 && y <= 185 && x >= 130 && x <= 190) {
            if (!acceptHoldTouch(21)) return;
            if (_offsetPreviewY <  4) { _offsetPreviewY++; changed = true; }
        }
        else if (y >= 100 && y <= 140 && x >= 80 && x <= 140) {
            if (!acceptHoldTouch(22)) return;
            if (_offsetPreviewX > -4) { _offsetPreviewX--; changed = true; }
        }
        else if (y >= 100 && y <= 140 && x >= 180 && x <= 240) {
            if (!acceptHoldTouch(23)) return;
            if (_offsetPreviewX <  4) { _offsetPreviewX++; changed = true; }
        }
        else if (y >= 108 && y <= 132 && x >= 148 && x <= 172) {
            if (!acceptTouch(24)) return;
            if (_offsetPreviewX != 0 || _offsetPreviewY != 0) {
                _offsetPreviewX = 0; _offsetPreviewY = 0; changed = true;
            }
        }
        else if (y >= 200 && x <= 130) {
            if (!acceptTouch(25)) return;
            /* Descarta ajuste em preview: restaura offset salvo antes de sair. */
            _offsetPreviewX = _offsetSavedX;
            _offsetPreviewY = _offsetSavedY;
            if (_tft) _tft->setDisplayOffset(_offsetSavedX, _offsetSavedY);
            showSettingsMain();
            return;
        }
        else if (y >= 200 && x >= 190) {
            if (!acceptTouch(26)) return;
            UiEvent ev;
            ev.type  = UiEvent::EVT_APPLY_DISPLAY_OFFSET;
            ev.id    = _offsetPreviewX;
            ev.param = _offsetPreviewY;
            queue_try_add(&_eventQueue, &ev);
            return;
        }

        if (changed) {
            if (_tft) _tft->setDisplayOffset(_offsetPreviewX, _offsetPreviewY);
            /* Redraw completo: frame anterior foi desenhado com offset diferente,
             * pixels antigos permanecem fora da nova área e precisam ser limpos. */
            _forceSettingsRedraw = true;
            _repaintSettings = true;
        }
    }
    else if (_uiMode == MODE_SETTINGS_LANG) {
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int actualIndex = (_langPage * 4) + clickedIndex;
            if (actualIndex < TOTAL_LANGS && actualIndex != _previewLangIdx) {
                if (!acceptSlideTouch(clickedIndex)) return;
                _previewLangIdx = actualIndex;
                _langPage = _previewLangIdx / 4;
                _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_previewLangIdx > 0) _previewLangIdx--; else _previewLangIdx = TOTAL_LANGS - 1;
                _langPage = _previewLangIdx / 4;
                _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
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

            /* Atualizar cursor de seleção visual */
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
            /* Novas posições: Shift=1..49, 123=51..99, Espaço=101..219, Bksp=221..269, OK=271..319 */
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
                /* Espaço */
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
                /* OK — mesma lógica de confirmação */
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
                        /* Redesenho parcial: título e boxes mudam, teclas não */
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
            /* Verificar se o toque está dentro do botão (não no gap) */
            int btnX = bStartX + btnIdx * (btnW + bGap);
            if (x < btnX || x > btnX + btnW) return;

            /* Limites de coluna: fila 3 (barra) tem 5 itens, filas 0-2 têm 10 */
            int maxCol = (_kbSelRow == 3) ? 4 : 9;

            if (btnIdx == 0) {
                /* ◄ Esquerda */
                if (!acceptTouch(60)) return;
                _kbSelCol--;
                if (_kbSelCol < 0) _kbSelCol = maxCol;
                _repaintSettings = true;
            }
            else if (btnIdx == 1) {
                /* ► Direita */
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
                /* ✓ Confirma seleção */
                if (!acceptTouch(64)) return;

                if (_kbSelRow == 3) {
                    /*
                     * Barra de ações: executar a ação do item selecionado.
                     * 0=Shift, 1=123, 2=Espaço, 3=Backspace, 4=OK
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
                        /* Espaço */
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
                        /* OK — confirmação da senha */
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
                if (!acceptSlideTouch(0x80 + clickedIndex)) return;

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
            if (!acceptSlideTouch(clickedIndex)) return;

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
                    if (!acceptHoldTouch(20)) return;
                    if (x < 160) { if (_soundSettings.volume >= 10) _soundSettings.volume -= 10; }
                    else          { if (_soundSettings.volume <= 90) _soundSettings.volume += 10; }
                    _touchSoundPending       = false;
                    _volumePreviewPending    = true;
                    _volumePreviewLevel      = _soundSettings.volume;
                    _repaintSettings = true;
                }
                else if (mapIdx == 7) {
                    if (!acceptHoldTouch(21)) return;
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
                if (!acceptHoldTouch(10)) return;
                if (_soundSelection > 0) _soundSelection--; else _soundSelection = TOTAL_SOUND_ITEMS - 1;
                _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
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


    else if (_uiMode == MODE_SETTINGS_STATUS) {
        if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_statusPage > 0) _statusPage--; else _statusPage = STATUS_PAGES - 1;
                _forceSettingsRedraw = true; _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
                if (_statusPage < STATUS_PAGES - 1) _statusPage++; else _statusPage = 0;
                _forceSettingsRedraw = true; _repaintSettings = true;
            }
            else if (x < 219) {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            }
        }
    }

    else if (_uiMode == MODE_SETTINGS_LICENSE) {

        if (y >= 32 && y <= 189) {
            /* Toque na área de texto: metade superior = pág anterior, inferior = próxima */
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
                if (!acceptHoldTouch(10)) return;
                if (_licensePage > 0) _licensePage--;
                _repaintSettings = true;
            }
            else if (x < 213) {
                if (!acceptHoldTouch(11)) return;
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
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
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
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_ALARMS_TITLE));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
        /* Botão SAIR ocupa toda a largura restante */
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

        /* Só redesenha itens que mudaram de estado de seleção ou em fullRedraw/pageChanged */
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

            /* Medir a largura do indicador SIM/NAO para reservar espaço */
            const char* statusTxt = rec->alarmsActive ? tr(TR_ON) : tr(TR_OFF);
            _canvasWide->setFont(&FreeSansBold9pt7b);
            int16_t sx1, sy1; uint16_t sw, sh;
            _canvasWide->getTextBounds(statusTxt, 0, 0, &sx1, &sy1, &sw, &sh);
            int statusAreaW = (int)sw + 20;  /* margem de 10px de cada lado */

            /* Nome do sensor — truncado se necessário para não colidir */
            int maxNameW = itemW - statusAreaW - 15;
            char nameBuf[40];
            truncateText(_canvasWide, rec->friendlyName, nameBuf, sizeof(nameBuf), maxNameW);
            _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24);
            _canvasWide->print(nameBuf);

            /* Indicador SIM/NAO alinhado à direita */
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
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
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
    const int TOTAL_ITEMS = 9; LangKey menuItems[] = {TR_MENU_THEMES, TR_MENU_ALARMS, TR_MENU_SOUNDS, TR_MENU_LANG, TR_MENU_PASSWORD, TR_MENU_TOUCH_CAL, TR_MENU_LICENSE, TR_MENU_STATUS, TR_MENU_DISPLAY_OFFSET};
    int totalPages = (TOTAL_ITEMS + 3) / 4; if (totalPages == 0) totalPages = 1;
    if (_mainMenuPage >= totalPages) _mainMenuPage = totalPages - 1; if (_mainMenuPage < 0) _mainMenuPage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
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
        _canvasWide->setTextSize(1); /* Garante reset após tela de status */
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


        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
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

    /* Título — redesenha sempre via canvas (muda entre fases) */
    {
        /* Barra de ponta a ponta sem cantos arredondados */
        _canvasWide->fillScreen(C_CARD_BG);
        _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(C_TEXT_MAIN);
        _canvasWide->setCursor(14, 18);
        _canvasWide->print((_kbPhase == 0) ? tr(TR_NEW_PASSWORD) : tr(TR_CONFIRM_PASSWORD));

        /* Botão X sobreposto à barra — y=4 mantém 4 px de margem no topo,
         * resistindo ao offset de display -4V sem clip das linhas superiores. */
        _canvasWide->fillRoundRect(282, 4, 30, 22, 4, C_TEMP_WARM);
        _canvasWide->setFont(&FreeSansBold9pt7b); _canvasWide->setTextColor(C_BG_MAIN);
        _canvasWide->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
        _canvasWide->setCursor(297 - w / 2, 20); _canvasWide->print("X");

        /* Blit com dstY=4 empurra o header 4 px para baixo na tela — evita
         * clip do topo a offset -4V. h sobe para 30 para acompanhar a
         * extensão vertical do conteúdo (título + botão X até y=26). */
        blitCanvas(_canvasWide, 0, 4, 320, 30);
    }


    {
        const int MAX_BOXES = 7;
        const int MIN_BOXES = 4;
        const int boxW = 32, boxH = 28, gap = 6;
        const int startY = 33;
        const int stripH = boxH + 10;

        /*
         * Número de boxes visíveis: na fase 0 (digitação), mostra o máximo
         * entre MIN_BOXES e (cursor + 1), até MAX_BOXES.
         * Na fase 1 (confirmação), mostra exatamente o tamanho da senha
         * já definida em _kbBuffer.
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
         * Barra de ações: Shift, 123, Espaço, Backspace, OK.
         * Mesma largura total das filas de teclas (x=1..319).
         * Shift=48, 123=48, Espaço=118, Backspace=48, OK=48, gap=2.
         */
        const int barY = 170, barH = 22;
        const int bx0 = 1;       /* Shift */
        const int bx1 = 51;      /* 123 */
        const int bx2 = 101;     /* Espaço */
        const int bx3 = 221;     /* Backspace */
        const int bx4 = 271;     /* OK */
        const int bw01 = 48;     /* Shift e 123 */
        const int bw2 = 118;     /* Espaço */
        const int bw34 = 48;     /* Backspace e OK */
        bool barActive = (_kbSelRow == 3);

        _canvasWide->fillScreen(C_BG_MAIN);

        /* Botão Shift (col 0) */
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

        /* Botão 123 (col 1) */
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

        /* Barra de espaço (col 2) */
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

        /* Botão Backspace (col 3) */
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

        /* Botão OK (col 4) */
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
         * 5 botões de navegação no estilo dashboard (58x40, raio 12).
         * ▲  ▼  ◄  ►  ✓(confirma caractere)
         * Posicionados na parte inferior da tela (Y=195).
         */
        const int btnW = 58, btnH = 40, gap = 5, startX = 5;
        const int navY = 195;

        _canvasWide->fillScreen(C_BG_MAIN);

        /* Botão ◄ (esquerda) */
        {
            _canvasWide->fillRoundRect(startX, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = startX + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx + 6, cy - 8, cx + 6, cy + 8, cx - 8, cy, C_TEXT_MAIN);
        }

        /* Botão ► (direita) */
        {
            int bx = startX + (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 6, cy - 8, cx - 6, cy + 8, cx + 8, cy, C_TEXT_MAIN);
        }

        /* Botão ▲ (cima) */
        {
            int bx = startX + 2 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 8, cy + 6, cx + 8, cy + 6, cx, cy - 8, C_TEXT_MAIN);
        }

        /* Botão ▼ (baixo) */
        {
            int bx = startX + 3 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 8, cy - 6, cx + 8, cy - 6, cx, cy + 8, C_TEXT_MAIN);
        }

        /* Botão ✓ (confirma caractere selecionado) */
        {
            int bx = startX + 4 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_ACCENT);
            int cx = bx + btnW / 2, cy = btnH / 2;
            /* Ícone de check */
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
            _tft->fillScreen(C_TEMP_HOT); _tft->fillRect(4, 4, 312, 32, C_CARD_BG); _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
            _tft->setFont(&FreeSansBold12pt7b); _tft->setTextColor(C_BG_MAIN); String msg1 = tr(TR_ACCESS_BLOCKED);
            _tft->getTextBounds(msg1, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 110); _tft->print(msg1);
            _tft->setFont(&FreeSansBold9pt7b); String msg2 = tr(TR_REBOOT_REQ);
            _tft->getTextBounds(msg2, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 140); _tft->print(msg2);
            _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
            /* Botão de licença */
            _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
            _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_SUB);
            String licTxt = tr(TR_LICENSE_TITLE);
            _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
            _forceSettingsRedraw = false;
        }
        return;
    }

    if (_lockoutUntil > 0 && !timeReached(_lockoutUntil)) {
        static long lastSec = -1;
        if (_forceSettingsRedraw) {
            _tft->fillScreen(C_BG_MAIN); _tft->fillRect(4, 4, 312, 32, C_CARD_BG); _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
            _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG);
            _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
            /* Botão de licença */
            _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
            _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_SUB);
            String licTxt = tr(TR_LICENSE_TITLE);
            _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
            _forceSettingsRedraw = false; lastSec = -1;
        }
        long secondsLeft = (long)(timeRemaining(_lockoutUntil) / 1000) + 1;
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
        _tft->fillScreen(C_BG_MAIN); _tft->fillRect(4, 4, 312, 32, C_CARD_BG); _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
        _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG); _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
        /* Botão de licença no canto inferior direito */
        _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_SUB);
        String licTxt = tr(TR_LICENSE_TITLE);
        _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
        _forceSettingsRedraw = false;
    }

    /* Status da autenticação via canvas — evita flicker */
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

    /* Botões do keypad via canvas — 2 botões por fila, 2 filas */
    for (int row = 0; row < 2; row++) {
        int rowY = 80 + (row * 60);
        _canvasWide->fillScreen(C_BG_MAIN);
        _canvasWide->setFont(&FreeSansBold12pt7b);

        for (int col = 0; col < 2; col++) {
            int btnIdx = (row * 2) + col;
            int bx0 = (col == 0) ? 15 : 165;

            /* Botão com bordas arredondadas bem acabadas */
            _canvasWide->fillRoundRect(bx0, 0, 140, 45, 10, C_CARD_BG);
            _canvasWide->drawRoundRect(bx0, 0, 140, 45, 10, C_TEXT_SUB);

            /* Caracteres distribuídos no botão */
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
 * @brief Informa o resultado do último envio de telemetria.
 *
 * Sucesso: inicia animação de flash (azul/branco por 1s), depois
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
    /*
     * Fluxo integrado: sensibilidade primeiro, depois posição.
     * 1. MODE_SETTINGS_TOUCH_SENS — taps para calibrar threshold de pressão
     * 2. MODE_SETTINGS_TOUCH_CAL  — crosshairs para calibrar posição
     * A transição 1→2 é automática após conclusão da sensibilidade.
     */
    _sensCount     = 0;
    _sensStability = 0.0f;
    _sensThreshold = 0;
    _sensDone      = false;
    _sensDoneTime  = 0;
    _calStep = 0;
    _calPhase = 0;
    memset(_calRawX, 0, sizeof(_calRawX));
    memset(_calRawY, 0, sizeof(_calRawY));

    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_TOUCH_SENS;
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

    /* Threshold de sensibilidade: usa valor salvo, fallback 400 se zero */
    _sensZThreshold = (cal->zThreshold > 0) ? cal->zThreshold : 400;
}


void DisplayManager::fillCalData(TouchCalData* cal) const {
    if (!cal) return;
    cal->magic = 0xCA;
    cal->flags = _calSwapXY ? 0x01 : 0x00;
    cal->xMin  = _calXMin;
    cal->xMax  = _calXMax;
    cal->yMin  = _calYMin;
    cal->yMax  = _calYMax;
    cal->zThreshold = _sensZThreshold;
}


/* =========================================================================== */
/*              TELA DE AJUSTE DE POSICIONAMENTO DO DISPLAY                  */
/* =========================================================================== */

/**
 * @brief Entra na tela de ajuste de offset; snapshot do estado salvo para BACK.
 *
 * O estado "preview" é inicializado com o offset atualmente aplicado ao TFT
 * (que corresponde ao valor persistido, carregado via loadDisplayOffset() no
 * boot). Qualquer alteração via setas é aplicada live ao _tft, e BACK restaura
 * o snapshot caso o usuário desista.
 */
void DisplayManager::showSettingsDisplayOffset() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_DISPLAY_OFFSET;
    _offsetSavedX  = _tft ? _tft->getOffsetX() : 0;
    _offsetSavedY  = _tft ? _tft->getOffsetY() : 0;
    _offsetPreviewX = _offsetSavedX;
    _offsetPreviewY = _offsetSavedY;
    _lastOffsetDrawX = 99;  /* sentinel força redraw dos valores numéricos */
    _lastOffsetDrawY = 99;
    _forceSettingsRedraw = true;
    _repaintSettings     = true;
    mutex_exit(&_stateMutex);
}


/**
 * @brief Renderiza a tela de ajuste de offset seguindo o padrão das demais
 *        telas de configuração (barra superior com título + rodapé de botões).
 *
 * Layout:
 *   [TÍTULO]     (0..320, 0..32)
 *   Pad direcional centralizado em (160,120):
 *       ▲ (130..190, 55..95)
 *     ◀ (80..140, 100..140)   ● (148..172, 108..132)   ▶ (180..240, 100..140)
 *       ▼ (130..190, 145..185)
 *   Indicador numérico       (190..310, 60..180) — "X:+2  Y:-1"
 *   Hint de uso              (y ≈ 175..195) — pequena legenda da tela
 *   Rodapé: [BACK] [APPLY]   (y >= 200)
 */
void DisplayManager::drawSettingsDisplayOffset() {
    if (!_tft) return;
    bool full = _forceSettingsRedraw;
    int16_t bx, by; uint16_t bw, bh;

    if (full) {
        _tft->fillScreen(C_BG_MAIN);

        /* Barra superior — título. */
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22);
        _tft->print(tr(TR_DISPLAY_OFFSET_TITLE));

        /* Pad direcional — desenha os 4 cursos como cápsulas com seta. */
        const int cx = 160, cy = 120;
        /* UP */
        _tft->fillRoundRect(130, 55, 60, 40, 8, C_CARD_BG);
        _tft->fillTriangle(cx, 62, cx - 10, 86, cx + 10, 86, C_TEXT_MAIN);
        /* DOWN */
        _tft->fillRoundRect(130, 145, 60, 40, 8, C_CARD_BG);
        _tft->fillTriangle(cx - 10, 154, cx + 10, 154, cx, 178, C_TEXT_MAIN);
        /* LEFT */
        _tft->fillRoundRect(80, 100, 60, 40, 8, C_CARD_BG);
        _tft->fillTriangle(90, cy, 120, cy - 10, 120, cy + 10, C_TEXT_MAIN);
        /* RIGHT */
        _tft->fillRoundRect(180, 100, 60, 40, 8, C_CARD_BG);
        _tft->fillTriangle(230, cy, 200, cy - 10, 200, cy + 10, C_TEXT_MAIN);
        /* Botão central de reset (círculo pequeno). */
        _tft->fillRoundRect(148, 108, 24, 24, 4, C_ACCENT);
        _tft->drawCircle(cx, cy, 4, C_BG_MAIN);

        /* Hint curto abaixo do pad. */
        _tft->setFont(&FreeSansBold9pt7b);
        _tft->setTextColor(C_TEXT_SUB);
        {
            const char* hint = tr(TR_DISPLAY_OFFSET_HINT);
            _tft->getTextBounds(hint, 0, 0, &bx, &by, &bw, &bh);
            int hx = (320 - (int)bw) / 2;
            if (hx < 4) hx = 4;
            _tft->setCursor(hx, 198);
            _tft->print(hint);
        }

        /* Rodapé — BACK (esq) e APPLY (dir). */
        _tft->fillRoundRect(10, 204, 120, 32, 8, C_CARD_BG);
        _tft->setTextColor(C_TEXT_MAIN);
        {
            String back = tr(TR_BACK);
            _tft->getTextBounds(back, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(10 + (120 - (int)bw) / 2, 226);
            _tft->print(back);
        }
        _tft->fillRoundRect(190, 204, 120, 32, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        {
            String apply = tr(TR_APPLY);
            _tft->getTextBounds(apply, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(190 + (120 - (int)bw) / 2, 226);
            _tft->print(apply);
        }

        _forceSettingsRedraw = false;
        _lastOffsetDrawX = 99;  /* força redraw numérico abaixo */
    }

    /* Valores numéricos do offset — repintados só quando mudam. */
    if (_offsetPreviewX != _lastOffsetDrawX || _offsetPreviewY != _lastOffsetDrawY) {
        _tft->fillRect(245, 60, 70, 90, C_BG_MAIN);
        _tft->setFont(&FreeSansBold12pt7b);
        _tft->setTextColor(C_TEXT_MAIN);
        char buf[16];
        snprintf(buf, sizeof(buf), "X %c%d",
                 _offsetPreviewX >= 0 ? '+' : '-',
                 abs((int)_offsetPreviewX));
        _tft->setCursor(250, 90);
        _tft->print(buf);
        snprintf(buf, sizeof(buf), "Y %c%d",
                 _offsetPreviewY >= 0 ? '+' : '-',
                 abs((int)_offsetPreviewY));
        _tft->setCursor(250, 130);
        _tft->print(buf);
        _lastOffsetDrawX = _offsetPreviewX;
        _lastOffsetDrawY = _offsetPreviewY;
    }
}


/* =========================================================================== */
/*          PERSISTÊNCIA DO OFFSET DE DISPLAY (API pública)                  */
/* =========================================================================== */

/**
 * @brief Carrega o offset de posicionamento do bloco de config persistida e
 *        aplica imediatamente ao TFT. Invocado no boot por AppManager.
 */
void DisplayManager::loadDisplayOffset(const DisplayOffsetData* data) {
    if (!data || data->magic != 0xD0) {
        if (_tft) _tft->setDisplayOffset(0, 0);
        _offsetSavedX = 0;
        _offsetSavedY = 0;
        return;
    }
    int8_t ox = constrain((int)data->offsetX, -4, 4);
    int8_t oy = constrain((int)data->offsetY, -4, 4);
    if (_tft) _tft->setDisplayOffset(ox, oy);
    _offsetSavedX = ox;
    _offsetSavedY = oy;
}


/**
 * @brief Preenche o struct com o offset atualmente aplicado, para persistência.
 */
void DisplayManager::fillDisplayOffsetData(DisplayOffsetData* data) const {
    if (!data) return;
    data->magic    = 0xD0;
    data->offsetX  = _tft ? _tft->getOffsetX() : 0;
    data->offsetY  = _tft ? _tft->getOffsetY() : 0;
    data->reserved = 0;
}


int8_t DisplayManager::getDisplayOffsetX() const {
    return _tft ? _tft->getOffsetX() : 0;
}

int8_t DisplayManager::getDisplayOffsetY() const {
    return _tft ? _tft->getOffsetY() : 0;
}


/**
 * @brief Reseta a calibração do touch para os valores padrão de fábrica.
 *
 * Restaura os limites raw genéricos (200..3800) e invalida a flag.
 * A próxima interação usará mapeamento estimado até nova calibração.
 */
void DisplayManager::resetTouchCalibration() {
    _calValid  = false;
    _calSwapXY = false;
    _calXMin   = 200;
    _calXMax   = 3800;
    _calYMin   = 200;
    _calYMax   = 3800;
    _sensZThreshold = 400;
}


/* =========================================================================== */
/*               CALIBRAÇÃO DE SENSIBILIDADE DO TOUCH                        */
/* =========================================================================== */

/**
 * @brief Inicia a tela de calibração de sensibilidade.
 * Reseta contadores e entra no modo de coleta de amostras.
 */
void DisplayManager::showTouchSensitivity() {
    _sensCount     = 0;
    _sensStability = 0.0f;
    _sensThreshold = 0;
    _sensDone      = false;
    _sensDoneTime  = 0;
    _uiMode = MODE_SETTINGS_TOUCH_SENS;
    _forceSettingsRedraw = true;
    _repaintSettings = true;
}

/**
 * @brief Desenha a tela de calibração de sensibilidade.
 *
 * Layout:
 * - Título na barra superior
 * - Crosshair alvo no centro
 * - Texto de progresso "Tap N/20"
 * - Barra vertical à direita mostrando estabilidade (0..100%)
 * - Valor numérico do threshold
 */
void DisplayManager::drawTouchSensitivity() {
    bool fullRedraw = _forceSettingsRedraw;
    _forceSettingsRedraw = false;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);

        /* Barra de título */
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22);
        _tft->print(tr(TR_SENS_TITLE));

        /* Botão CANCEL (canto inferior esquerdo) */
        _tft->fillRoundRect(5, 195, 120, 40, 8, C_CARD_BG);
        int16_t bx, by; uint16_t bw, bh;
        String backTxt = tr(TR_CANCEL);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(5 + (120 - bw) / 2, 220);
        _tft->print(backTxt);

        /* Moldura da barra vertical (direita) */
        _tft->drawRect(289, 38, 26, 154, C_TEXT_OFF);
    }

    /* Crosshair central */
    int cx = 140, cy = 115;
    uint16_t crossColor = _sensDone ? C_TEMP_OK : C_ACCENT;
    _tft->fillRect(cx - 30, cy - 1, 60, 3, C_BG_MAIN);
    _tft->fillRect(cx - 1, cy - 30, 3, 60, C_BG_MAIN);
    _tft->drawLine(cx - 15, cy, cx + 15, cy, crossColor);
    _tft->drawLine(cx, cy - 15, cx, cy + 15, crossColor);
    _tft->drawCircle(cx, cy, 12, crossColor);

    /* Texto de progresso */
    _tft->fillRect(80, 150, 140, 30, C_BG_MAIN);
    _tft->setFont(&FreeSansBold9pt7b);
    _tft->setTextColor(C_TEXT_MAIN);

    if (_sensDone) {
        int16_t bx2, by2; uint16_t bw2, bh2;
        String doneMsg = tr(TR_SENS_DONE);
        _tft->getTextBounds(doneMsg, 0, 0, &bx2, &by2, &bw2, &bh2);
        _tft->setCursor(140 - bw2 / 2, 168);
        _tft->print(doneMsg);
    } else {
        /* Instrução: reutiliza "Toque na mira" da calibração de posição */
        int16_t bx2, by2; uint16_t bw2, bh2;
        String holdMsg = tr(TR_CAL_TOUCH_POINT);
        _tft->getTextBounds(holdMsg, 0, 0, &bx2, &by2, &bw2, &bh2);
        _tft->setCursor(140 - bw2 / 2, 168);
        _tft->print(holdMsg);
    }

    /* Barra vertical de estabilidade (dentro da moldura) */
    int barX = 290, barY = 39, barW = 24, barH = 152;
    int fillH = (int)(barH * _sensStability);
    if (fillH > barH) fillH = barH;

    /* Fundo (parte não preenchida) */
    if (fillH < barH) {
        _tft->fillRect(barX, barY, barW, barH - fillH, C_BG_MAIN);
    }
    /* Preenchimento (de baixo para cima) */
    uint16_t barColor = (_sensStability >= 0.85f) ? C_TEMP_OK : C_ACCENT;
    if (fillH > 0) {
        _tft->fillRect(barX, barY + barH - fillH, barW, fillH, barColor);
    }

    /* Valor numérico abaixo da barra */
    _tft->fillRect(280, 195, 40, 20, C_BG_MAIN);
    _tft->setFont(NULL);
    _tft->setTextSize(1);
    _tft->setTextColor(C_TEXT_OFF);
    char valBuf[8];
    snprintf(valBuf, sizeof(valBuf), "%d", _sensThreshold);
    _tft->setCursor(295, 198);
    _tft->print(valBuf);
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


        drawCrosshair(CAL_SCR_X[pointIdx], CAL_SCR_Y[pointIdx],
                      _calHoldReady ? C_TEMP_OK : C_ACCENT);


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
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
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


        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
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


/**
 * @brief Aceita toque único — exige que o dedo tenha sido levantado
 *        desde o último toque aceito. Impede repetição por segurar.
 */
bool DisplayManager::acceptTouch(uint8_t zoneId) {
    if (!_touchReleased) return false;

    _touchReleased       = false;
    _lastTouchRegion     = zoneId;
    _lastRegionTouchTime = millis();
    _lastTouchTimestamp  = millis();
    _touchSoundPending   = true;
    return true;
}

/**
 * @brief Aceita toque com repetição por segurar (hold-repeat).
 *
 * Primeiro toque: aceita imediatamente e toca o bip.
 * Enquanto segura: repete a cada HOLD_REPEAT_MS (300ms) com bip.
 * Usado para botões de navegação de lista e incremento/decremento.
 */
bool DisplayManager::acceptHoldTouch(uint8_t zoneId) {
    uint32_t now = millis();

    if (_touchReleased) {
        /* Primeiro toque: aceita e toca bip */
        _touchReleased       = false;
        _lastTouchRegion     = zoneId;
        _lastRegionTouchTime = now;
        _lastTouchTimestamp  = now;
        _holdRepeatLastFire  = now;
        _touchSoundPending   = true;
        return true;
    }

    /* Segurar: repete a cada 300ms com bip */
    if (zoneId == _lastTouchRegion && (now - _holdRepeatLastFire >= HOLD_REPEAT_MS)) {
        _holdRepeatLastFire = now;
        _lastTouchTimestamp = now;
        _touchSoundPending  = true;
        return true;
    }

    return false;
}

/**
 * @brief Aceita toque com deslizamento entre zonas.
 *
 * Primeiro toque: aceita imediatamente com bip.
 * Deslizar para zona diferente: aceita com bip (sem exigir release).
 * Manter na mesma zona: não repete.
 * Usado para slots, períodos de gráfico e listas de seleção.
 */
bool DisplayManager::acceptSlideTouch(uint8_t zoneId) {
    /* Primeiro toque ou deslizou para zona diferente */
    if (_touchReleased || zoneId != _lastTouchRegion) {
        _touchReleased       = false;
        _lastTouchRegion     = zoneId;
        _lastRegionTouchTime = millis();
        _lastTouchTimestamp  = millis();
        _touchSoundPending   = true;
        return true;
    }

    /* Mesma zona, segurando: não repete */
    return false;
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
    safeCopy(_webNotifyUser, username, sizeof(_webNotifyUser));
    _webNotifyUser[sizeof(_webNotifyUser) - 1] = '\0';
    _webNotifyStartMs = millis();
    if (_webNotifyStartMs == 0) _webNotifyStartMs = 1;
}


/* =========================================================================== */
/*                   STATUS DO SISTEMA EM TEMPO REAL                         */
/* =========================================================================== */

void DisplayManager::showSystemStatus() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_STATUS;
    _statusPage = 0;
    _statusLastDraw = 0;
    _forceSettingsRedraw = true;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::updateSystemStatus(const SystemStatusData& data) {
    _statusData = data;
}

/**
 * @brief Desenha a tela de status do sistema com flicker zero.
 *
 * Usa canvas (strip rendering) para toda a área de conteúdo.
 * 4 páginas: Sistema, Rede, Sensores, Telemetria.
 * Auto-refresh a cada 1 segundo via timer no render loop.
 */
void DisplayManager::drawSystemStatus() {
    bool fullRedraw = _forceSettingsRedraw;
    _forceSettingsRedraw = false;

    GFXcanvas16* cv = _canvasWide;
    if (!cv) return;

    const SystemStatusData& d = _statusData;

    /* ── Header + Botões (somente no fullRedraw) ── */
    if (fullRedraw) {
        cv->fillScreen(C_CARD_BG);
        cv->setFont(&FreeSansBold9pt7b);
        cv->setTextColor(C_TEXT_MAIN);
        cv->setCursor(10, 20); cv->print(tr(TR_STATUS_TITLE));

        /* Dots de página */
        cv->setFont(NULL); cv->setTextSize(1);
        for (int p = 0; p < STATUS_PAGES; p++) {
            int dx = 280 + p * 10;
            if (p == _statusPage) cv->fillCircle(dx, 14, 3, C_ACCENT);
            else                  cv->drawCircle(dx, 14, 2, C_TEXT_OFF);
        }
        blitCanvas(cv, 0, 0, 320, 28);

        /* Botões ← → BACK */
        _tft->fillRoundRect(5, 195, 62, 40, 8, C_CARD_BG);
        _tft->fillTriangle(36, 207, 26, 221, 46, 221, C_TEXT_MAIN);
        _tft->fillRoundRect(73, 195, 62, 40, 8, C_CARD_BG);
        _tft->fillTriangle(104, 221, 94, 207, 114, 207, C_TEXT_MAIN);
        _tft->fillRoundRect(141, 195, 75, 40, 8, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        int16_t bx, by; uint16_t bw, bh;
        const char* bt = tr(TR_BACK);
        _tft->getTextBounds(bt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw) / 2, 220); _tft->print(bt);
    }

    /*
     * Tabela simples: font NULL size 2 (12×16px).
     * Cada linha: 20px (16px texto + 4px gap).
     * Área útil: y=28..194 = 166px → 8 linhas por página.
     * Label à esquerda, valor à direita, separados por linha pontilhada.
     */

    static char buf[64];
    static char fbuf[12];

    /* Monta array de linhas para a página atual */
    struct Row { const char* lbl; char val[28]; uint16_t color; };
    static Row rows[8];
    int nRows = 0;

    auto addRow = [&](const char* lbl, const char* val, uint16_t c = 0) {
        if (nRows >= 8) return;
        rows[nRows].lbl = lbl;
        safeCopy(rows[nRows].val, val, sizeof(rows[nRows].val));
        rows[nRows].color = c ? c : C_TEXT_MAIN;
        nRows++;
    };

    if (_statusPage == 0) {
        addRow("Device", d.deviceName);
        addRow("Firmware", d.fwVersion);
        unsigned long s = (unsigned long)d.uptimeSec;
        snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
                 s/86400, (s%86400)/3600, (s%3600)/60, s%60);
        addRow("Uptime", buf);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)d.heapFree);
        addRow("Heap Free", buf, d.heapFree < 20000 ? C_TEMP_HOT : C_TEMP_OK);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)d.flashUsed);
        addRow("Flash Used", buf);
        fmtFloat1(fbuf, sizeof(fbuf), d.boardTemp);
        snprintf(buf, sizeof(buf), "%s oC", fbuf);
        addRow("Board Temp", buf);
        snprintf(buf, sizeof(buf), "GMT%+d", (int)d.timezone);
        addRow("Timezone", buf);
    }
    else if (_statusPage == 1) {
        addRow("WiFi", d.wifiConnected ? "Connected" : "Disconnected",
               d.wifiConnected ? C_TEMP_OK : C_TEMP_HOT);
        addRow("SSID", d.ssid);
        addRow("IP", d.ip);
        addRow("MAC", d.mac);
        snprintf(buf, sizeof(buf), "%ld dBm", (long)d.rssi);
        uint16_t rc = (d.rssi > -60) ? C_TEMP_OK : (d.rssi > -80) ? C_ACCENT : C_TEMP_HOT;
        addRow("RSSI", buf, rc);
        addRow("NTP", d.ntpSynced ? "Synced" : "Not synced",
               d.ntpSynced ? C_TEMP_OK : C_TEMP_HOT);
        addRow("NTP Server", d.ntpServer);
    }
    else if (_statusPage == 2) {
        snprintf(buf, sizeof(buf), "%d", d.activeSensors);
        addRow("Active", buf);
        if (d.ambientValid) {
            fmtFloat1(fbuf, sizeof(fbuf), d.ambientTemp);
            snprintf(buf, sizeof(buf), "%s oC", fbuf);
            addRow("Ambient T", buf);
            snprintf(buf, sizeof(buf), "%d%%", (int)d.ambientHum);
            addRow("Ambient H", buf);
        } else {
            addRow("Ambient T", "--", C_TEXT_OFF);
            addRow("Ambient H", "--", C_TEXT_OFF);
        }
    }
    else if (_statusPage == 3) {
        addRow("Transport", d.telTransport == 1 ? "MQTT" : "HTTP");
        addRow("Server", d.telServer);
        snprintf(buf, sizeof(buf), "%u", (unsigned)d.telPending);
        addRow("Pending", buf, d.telPending > 50 ? C_TEMP_HOT : C_TEMP_OK);
        snprintf(buf, sizeof(buf), "%u", (unsigned)d.telFails);
        addRow("Fails", buf, d.telFails > 0 ? C_TEMP_HOT : C_TEMP_OK);
        snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)d.telInterval);
        addRow("Interval", buf);
        if (d.telTransport == 1) {
            addRow("MQTT", d.mqttConnected ? "Connected" : "Disconnected",
                   d.mqttConnected ? C_TEMP_OK : C_TEMP_HOT);
        }
    }

    /* ── Renderiza tabela em strips de 42px ── */
    const int rowH = 20;
    const int valX  = 150; /* Coluna dos valores */

    for (int strip = 0; strip < 4; strip++) {
        int sTop = 28 + strip * 42;
        int sH = 42;
        if (sTop + sH > 195) sH = 195 - sTop;
        if (sH <= 0) break;

        cv->fillScreen(C_BG_MAIN);

        for (int r = 0; r < 2; r++) {
            int ri = strip * 2 + r; /* Índice absoluto da linha */
            if (ri >= nRows) break;

            int ly = r * rowH + 2;

            /* Label */
            cv->setFont(NULL); cv->setTextSize(2);
            cv->setTextColor(C_TEXT_SUB);
            cv->setCursor(4, ly);
            cv->print(rows[ri].lbl);

            /* Valor */
            cv->setTextColor(rows[ri].color);
            cv->setCursor(valX, ly);
            cv->print(rows[ri].val);

            /* Separador pontilhado */
            int sepY = ly + 17;
            if (sepY < sH) {
                for (int dx = 4; dx < 316; dx += 4)
                    cv->drawPixel(dx, sepY, C_GRID);
            }
        }

        blitCanvas(cv, 0, sTop, 320, sH);
    }

    _statusLastDraw = millis();

    /* Restaura textSize para não contaminar outras telas */
    cv->setTextSize(1);
    cv->setFont(NULL);
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

    /* Contar linhas totais (licença + acknowledgments já integrados) */
    int totalLines = wrapLineCount(licText, MAX_COLS);

    /* Calcular total de páginas */
    _licenseTotalPages = (totalLines + MAX_VIS - 1) / MAX_VIS;
    if (_licenseTotalPages < 1) _licenseTotalPages = 1;
    if (_licensePage >= _licenseTotalPages) _licensePage = _licenseTotalPages - 1;
    if (_licensePage < 0) _licensePage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);

        /* Header com título e contador de páginas */
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&FreeSansBold9pt7b); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_LICENSE_TITLE));

        char pgBuf[8];
        snprintf(pgBuf, sizeof(pgBuf), "%d/%d", _licensePage + 1, _licenseTotalPages);
        int16_t px, py; uint16_t pw, ph;
        _tft->getTextBounds(pgBuf, 0, 0, &px, &py, &pw, &ph);
        _tft->setTextColor(C_TEXT_SUB);
        _tft->setCursor(310 - (int)pw, 22); _tft->print(pgBuf);

        /* Botões inferiores */
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

    /* Limpar área de texto */
    _tft->fillRect(0, TEXT_Y0, 320, MAX_VIS * LINE_H, C_BG_MAIN);
    _tft->setFont(NULL); _tft->setTextSize(1);
    _tft->setTextColor(C_TEXT_SUB);

    /* Renderizar página atual */
    int startLine = _licensePage * MAX_VIS;
    renderWrapped(_tft, licText, 10, TEXT_Y0, MAX_COLS, LINE_H,
                  startLine, MAX_VIS);

    /* Indicador de páginas (dots) */
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
