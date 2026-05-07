/* =============================================================================
 *  pico_hand.ino
 *
 *  "Mao robotica" para acionar remotamente os botoes BOOTSEL e RESET de um
 *  Raspberry Pi Pico alvo, atraves de comandos enviados pela serial USB.
 *
 *  Versao para a IDE Arduino — testado com o core "arduino-pico"
 *  (Earle Philhower):  https://github.com/earlephilhower/arduino-pico
 *
 *  Selecao na IDE:
 *      Placa : Raspberry Pi Pico (ou Pico W)
 *      USB Stack: Pico SDK (padrao)
 *
 *  Principio eletrico
 *  ------------------
 *  Tanto BOOTSEL quanto RUN (reset) tem pull-up no Pico alvo. Os botoes
 *  fisicos apenas curto-circuitam a linha para GND. Imitamos isso com cada
 *  GPIO em "open-drain emulado":
 *
 *      "Pressionado": GPIO como OUTPUT em LOW    -> puxa a linha para GND
 *      "Solto"      : GPIO como INPUT             -> alta impedancia
 *
 *  NUNCA dirigimos a linha para HIGH, evitando conflito com o pull-up do
 *  alvo e o risco de curto se alguem pressionar o botao fisico ao mesmo
 *  tempo.
 *
 *  Ligacao esperada
 *  ----------------
 *      Pico "mao"                 Pico alvo
 *      ----------                 ---------
 *      GPIO PIN_BOOTSEL  -------- pad/pino do botao BOOTSEL (lado quente)
 *      GPIO PIN_RESET    -------- pad/pino do botao RUN/RESET (lado quente)
 *      GND               -------- GND  (obrigatorio!)
 * ============================================================================= */

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

/* =============================================================================
 *  Configuracoes de hardware (ajuste conforme sua fiacao)
 * ============================================================================= */

/** GPIO ligado ao botao BOOTSEL do Pico alvo. */
static const uint8_t PIN_BOOTSEL = 0;

/** GPIO ligado ao pino RUN (reset) do Pico alvo. */
static const uint8_t PIN_RESET   = 1;

/** LED de bordo, usado como heartbeat para indicar firmware vivo.
 *  GP25 e o LED on-board do Pico padrao. Valor literal evita colisao
 *  com o macro `PIN_LED` definido em pins_arduino.h do core arduino-pico
 *  (que faria a variavel virar `(25u) = LED_BUILTIN` apos preprocessor). */
static const uint8_t LED_GPIO    = 25;

/* =============================================================================
 *  Tempos das sequencias (em milissegundos)
 * ============================================================================= */

/** Largura minima do pulso de RESET para o RP2040 reconhecer. */
static const uint32_t RESET_PULSE_MS      = 50;

/** Tempo que BOOTSEL fica pressionado ANTES de aplicar o reset. */
static const uint32_t BOOTSEL_HOLD_PRE_MS = 50;

/** Tempo que BOOTSEL continua pressionado APOS o RESET subir, garantindo
 *  que o bootrom enxergue o pino baixo durante a amostragem inicial. */
static const uint32_t BOOTSEL_HOLD_POS_MS = 200;

/** Periodo do heartbeat do LED. */
static const uint32_t HEARTBEAT_MS        = 500;

/* =============================================================================
 *  Constantes do parser
 * ============================================================================= */

static const size_t LINE_BUFFER_SIZE = 64;
static const size_t ARG_BUFFER_SIZE  = 16;

/* =============================================================================
 *  Camada de "botao virtual" (open-drain emulado)
 * ============================================================================= */

/**
 * Inicializa o GPIO no estado seguro (botao solto): entrada sem pull,
 * deixando o Pico alvo controlar o nivel da linha.
 *
 * @param gpio  numero do GPIO a configurar.
 */
static void pin_init_released(uint8_t gpio)
{
    /* Pre-grava LOW no registrador de saida: assim, quando virarmos OUTPUT
       em pin_press(), o nivel baixo ja estara pronto sem glitch. */
    digitalWrite(gpio, LOW);
    pinMode(gpio, INPUT);
}

/**
 * Pressiona o "botao": GPIO vira OUTPUT em LOW, puxando a linha do
 * Pico alvo para GND.
 *
 * @param gpio  numero do GPIO a pressionar.
 */
static void pin_press(uint8_t gpio)
{
    digitalWrite(gpio, LOW);
    pinMode(gpio, OUTPUT);
}

/**
 * Solta o "botao": GPIO volta a ser INPUT (alta impedancia), permitindo
 * que o pull-up do alvo levante a linha.
 *
 * @param gpio  numero do GPIO a soltar.
 */
static void pin_release(uint8_t gpio)
{
    pinMode(gpio, INPUT);
}

/**
 * Estado interno espelhado pelo firmware, usado pelo comando STATUS.
 *
 * Mantemos isto em variaveis ao inves de inferir do hardware porque, no
 * core arduino-pico, nao ha API publica direta para ler o registrador
 * de direcao (GPIO_OE) — e essa pequena duplicacao mantem o codigo
 * portavel entre cores.
 */
static bool g_bootsel_pressed = false;
static bool g_reset_pressed   = false;

/* =============================================================================
 *  Sequencias de alto nivel
 * ============================================================================= */

/**
 * Aplica um pulso de reset no Pico alvo.
 */
static void sequence_reset(void)
{
    pin_press(PIN_RESET);
    g_reset_pressed = true;
    delay(RESET_PULSE_MS);

    pin_release(PIN_RESET);
    g_reset_pressed = false;
}

/**
 * Coloca o Pico alvo em modo BOOTSEL:
 *   1. Pressiona BOOTSEL.
 *   2. Aplica um pulso completo de RESET (com BOOTSEL ainda segurado).
 *   3. Mantem BOOTSEL pressionado por mais um instante para o bootrom
 *      amostrar o pino durante a inicializacao.
 *   4. Solta BOOTSEL.
 */
static void sequence_bootsel(void)
{
    pin_press(PIN_BOOTSEL);
    g_bootsel_pressed = true;
    delay(BOOTSEL_HOLD_PRE_MS);

    pin_press(PIN_RESET);
    g_reset_pressed = true;
    delay(RESET_PULSE_MS);

    pin_release(PIN_RESET);
    g_reset_pressed = false;

    delay(BOOTSEL_HOLD_POS_MS);

    pin_release(PIN_BOOTSEL);
    g_bootsel_pressed = false;
}

/* =============================================================================
 *  Utilitarios de string
 * ============================================================================= */

/**
 * Converte string para maiusculas in-place (apenas ASCII).
 */
static void str_upper(char *s)
{
    for (; *s != '\0'; ++s) {
        *s = (char)toupper((unsigned char)*s);
    }
}

/**
 * Remove espacos/CR/LF nas duas pontas da string in-place.
 *
 * @param s  string a ser aparada (modificada in-place).
 * @return   ponteiro para o primeiro caractere util dentro de @p s.
 */
static char *str_trim(char *s)
{
    /* Avanca o inicio pulando whitespace */
    while (*s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    /* Recua o fim cortando whitespace */
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return s;
}

/* =============================================================================
 *  Tabela de comandos
 *
 *  Cada handler recebe os argumentos (ja com trim) como string. Comandos
 *  sem argumentos simplesmente ignoram o parametro.
 * ============================================================================= */

typedef void (*command_handler_t)(const char *args);

typedef struct {
    const char        *name;     /**< nome em maiusculas, ex.: "RESET"      */
    const char        *help;     /**< descricao curta exibida pelo HELP     */
    command_handler_t  handler;  /**< funcao que executa o comando          */
} command_t;

/* Forward declarations dos handlers ---------------------------------------- */
static void cmd_ping(const char *args);
static void cmd_reset(const char *args);
static void cmd_bootsel(const char *args);
static void cmd_hold(const char *args);
static void cmd_release(const char *args);
static void cmd_status(const char *args);
static void cmd_pinout(const char *args);
static void cmd_self_bootsel(const char *args);
static void cmd_help(const char *args);

/* Tabela de despacho ------------------------------------------------------- */
static const command_t COMMANDS[] = {
    { "PING",         "responde PONG (teste de conectividade)",            cmd_ping         },
    { "RESET",        "aplica pulso de reset no Pico alvo",                cmd_reset        },
    { "BOOTSEL",      "coloca o Pico alvo em modo BOOTSEL",                cmd_bootsel      },
    { "HOLD",         "HOLD <BOOTSEL|RESET>: segura o botao pressionado",  cmd_hold         },
    { "RELEASE",      "RELEASE <BOOTSEL|RESET>: solta o botao",            cmd_release      },
    { "STATUS",       "mostra o estado atual dos pinos de controle",       cmd_status       },
    { "PINOUT",       "mostra qual GPIO esta em qual funcao",              cmd_pinout       },
    { "SELF_BOOTSEL", "coloca esta MAO em BOOTSEL (para reflashar)",       cmd_self_bootsel },
    { "HELP",         "lista todos os comandos disponiveis",               cmd_help         },
};

static const size_t N_COMMANDS = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

/* =============================================================================
 *  Implementacao dos comandos
 * ============================================================================= */

static void cmd_ping(const char *args)
{
    (void)args;
    Serial.println("PONG");
}

static void cmd_reset(const char *args)
{
    (void)args;
    sequence_reset();
    Serial.println("OK RESET");
}

static void cmd_bootsel(const char *args)
{
    (void)args;
    sequence_bootsel();
    Serial.println("OK BOOTSEL");
}

/**
 * Resolve o nome textual ("BOOTSEL" ou "RESET") para o GPIO correspondente
 * e tambem para o flag de estado interno.
 *
 * @param name           string em maiusculas.
 * @param out_gpio       (saida) GPIO resolvido.
 * @param out_state_flag (saida) ponteiro para o flag de estado a atualizar.
 * @return               true se o nome era valido, false caso contrario.
 */
static bool resolve_target(const char *name, uint8_t *out_gpio, bool **out_state_flag)
{
    if (strcmp(name, "BOOTSEL") == 0) {
        *out_gpio       = PIN_BOOTSEL;
        *out_state_flag = &g_bootsel_pressed;
        return true;
    }
    if (strcmp(name, "RESET") == 0) {
        *out_gpio       = PIN_RESET;
        *out_state_flag = &g_reset_pressed;
        return true;
    }
    return false;
}

/**
 * Helper compartilhado por HOLD/RELEASE: copia o argumento para um buffer
 * local e devolve o GPIO alvo + flag de estado ja resolvidos.
 *
 * @param args            argumento bruto recebido pelo handler.
 * @param out_name        buffer onde o nome normalizado e colocado (uppercase).
 * @param out_size        tamanho do buffer @p out_name.
 * @param out_gpio        (saida) GPIO resolvido.
 * @param out_state_flag  (saida) flag de estado correspondente.
 * @return                true em sucesso, false se argumento invalido.
 */
static bool parse_target_arg(const char *args,
                             char       *out_name,
                             size_t      out_size,
                             uint8_t    *out_gpio,
                             bool      **out_state_flag)
{
    /* Defesa contra argumento ausente */
    if (args == NULL || *args == '\0') {
        return false;
    }
    strncpy(out_name, args, out_size - 1);
    out_name[out_size - 1] = '\0';
    str_upper(out_name);
    return resolve_target(out_name, out_gpio, out_state_flag);
}

static void cmd_hold(const char *args)
{
    char     target[ARG_BUFFER_SIZE];
    uint8_t  gpio;
    bool    *state_flag;

    if (!parse_target_arg(args, target, sizeof(target), &gpio, &state_flag)) {
        Serial.println("ERR: HOLD precisa de BOOTSEL ou RESET");
        return;
    }
    pin_press(gpio);
    *state_flag = true;
    Serial.printf("OK HOLD %s\n", target);
}

static void cmd_release(const char *args)
{
    char     target[ARG_BUFFER_SIZE];
    uint8_t  gpio;
    bool    *state_flag;

    if (!parse_target_arg(args, target, sizeof(target), &gpio, &state_flag)) {
        Serial.println("ERR: RELEASE precisa de BOOTSEL ou RESET");
        return;
    }
    pin_release(gpio);
    *state_flag = false;
    Serial.printf("OK RELEASE %s\n", target);
}

static void cmd_status(const char *args)
{
    (void)args;
    Serial.printf("STATUS BOOTSEL=%s RESET=%s\n",
                  g_bootsel_pressed ? "PRESSED" : "RELEASED",
                  g_reset_pressed   ? "PRESSED" : "RELEASED");
}

static void cmd_pinout(const char *args)
{
    (void)args;
    Serial.printf("PINOUT BOOTSEL=GP%u RESET=GP%u LED=GP%u\n",
                  (unsigned)PIN_BOOTSEL,
                  (unsigned)PIN_RESET,
                  (unsigned)LED_GPIO);
}

static void cmd_self_bootsel(const char *args)
{
    (void)args;
    Serial.println("OK SELF_BOOTSEL");
    Serial.flush();
    /* Pequeno delay extra para a USB drenar antes de a CPU reentrar no
       bootrom (a porta CDC desaparece nesse momento). */
    delay(100);
    /* API do core arduino-pico: poe a propria placa em modo BOOTSEL. */
    rp2040.rebootToBootloader();
}

static void cmd_help(const char *args)
{
    (void)args;
    Serial.println("Comandos disponiveis:");
    for (size_t i = 0; i < N_COMMANDS; ++i) {
        Serial.printf("  %-13s - %s\n", COMMANDS[i].name, COMMANDS[i].help);
    }
}

/* =============================================================================
 *  Parser de linha
 * ============================================================================= */

/**
 * Processa uma linha ja completa recebida pela serial.
 *
 * Quebra a linha em "comando" e "argumentos" no primeiro espaco, faz lookup
 * na tabela COMMANDS e despacha. Linhas vazias sao silenciosamente ignoradas.
 */
static void process_line(char *line)
{
    line = str_trim(line);
    if (*line == '\0') {
        return;   /* linha em branco — nao e erro */
    }

    /* Separa nome do comando e argumentos no primeiro espaco */
    char *args  = (char *)"";
    char *space = strchr(line, ' ');
    if (space != NULL) {
        *space = '\0';
        args   = str_trim(space + 1);
    }

    /* Comando case-insensitive */
    str_upper(line);

    for (size_t i = 0; i < N_COMMANDS; ++i) {
        if (strcmp(line, COMMANDS[i].name) == 0) {
            COMMANDS[i].handler(args);
            return;
        }
    }
    Serial.printf("ERR: comando desconhecido '%s' (digite HELP)\n", line);
}

/**
 * Bombeia caracteres da serial USB para um buffer de linha. Quando uma
 * quebra de linha chega, a linha e processada. Nao bloqueia: se nao houver
 * dados, retorna imediatamente.
 */
static void serial_pump(void)
{
    static char   buf[LINE_BUFFER_SIZE];
    static size_t len = 0;

    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) {
            break;
        }
        if (c == '\r' || c == '\n') {
            /* Fim de linha: processa o que tiver no buffer */
            if (len > 0) {
                buf[len] = '\0';
                process_line(buf);
                len = 0;
            }
            /* Sequencia \r\n e tratada naturalmente: na segunda iteracao,
               len ja sera 0 e a linha vazia e ignorada. */
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = (char)c;
        } else {
            /* Linha excedeu o buffer: descarta e reporta erro */
            len = 0;
            Serial.printf("ERR: linha muito longa (max %u)\n",
                          (unsigned)(sizeof(buf) - 1));
        }
    }
}

/* =============================================================================
 *  setup() / loop()
 * ============================================================================= */

void setup(void)
{
    /* USB CDC. A velocidade e ignorada na CDC, mas mantemos por convencao. */
    Serial.begin(115200);

    /* LED de heartbeat */
    pinMode(LED_GPIO, OUTPUT);
    digitalWrite(LED_GPIO, LOW);

    /* Linhas de controle comecam soltas (alta impedancia). */
    pin_init_released(PIN_BOOTSEL);
    pin_init_released(PIN_RESET);
}

void loop(void)
{
    /* Heartbeat nao bloqueante: alterna o LED a cada HEARTBEAT_MS. */
    static uint32_t last_blink_ms = 0;
    uint32_t now = millis();
    if (now - last_blink_ms >= HEARTBEAT_MS) {
        last_blink_ms = now;
        digitalWrite(LED_GPIO, !digitalRead(LED_GPIO));
    }

    /* Processa qualquer comando que tenha chegado pela serial. */
    serial_pump();
}
