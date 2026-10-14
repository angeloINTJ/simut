/**
 * @file    src/ota/restore.cpp
 * @brief   Implementação do state machine de restore (Fase 2 OTA).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "restore.h"
#include "backup.h"
#include "backup_format.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <hardware/watchdog.h>
#include <string.h>

namespace ota {

static constexpr int RESTORE_WALK_MAX_DEPTH = 8;

/* ---------------------------------------------------------------------------
 * Helpers de path / FS
 * ------------------------------------------------------------------------- */

static bool path_is_safe(const char* p, uint16_t len) {
    if (len == 0 || p[0] != '/') return false;
    for (uint16_t i = 0; i < len; i++) {
        if (p[i] == 0) return false;
    }
    for (uint16_t i = 0; i + 1 < len; i++) {
        if (p[i] == '.' && p[i + 1] == '.') return false;
    }
    return true;
}

/* Garante que diretórios pai do path existam (para LittleFS.open(..., "w")). */
static bool ensure_parent_dirs(const char* path) {
    /* LittleFS implicit cria parents na maior parte dos casos, mas garantimos. */
    char buf[RESTORE_MAX_PATH + 1];
    size_t n = strnlen(path, sizeof(buf) - 1);
    memcpy(buf, path, n);
    buf[n] = '\0';
    for (size_t i = 1; i < n; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (!LittleFS.exists(buf)) {
                LittleFS.mkdir(buf);
            }
            buf[i] = '/';
        }
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Falha no meio de um APPLY: o que fica e o que sai (achado 58, 2026-10-02).
 *
 * O APPLY escreve cada arquivo direto no caminho final, sem .tmp e rename: o
 * sistema de arquivos vive perto de 86% cheio, e um restore inteiro em
 * arquivos temporários precisaria do dobro do espaço. Então o original de cada
 * arquivo some no momento em que ele é aberto para escrita, e um rollback não
 * tem o que devolver.
 *
 * Até 2026-10-02 o rollback APAGAVA todo arquivo que o APPLY tinha escrito.
 * Uma conexão que caía no meio levava junto o /config/system.bin já restaurado,
 * e o boot seguinte subia com a configuração de fábrica. E a lista que o
 * rollback usava cabia 200 caminhos, contados só no APPLY: um backup com mais
 * arquivos passava na validação e parava no 201º, apagando os 200 de antes.
 *
 * Agora um arquivo terminado fica, com o conteúdo do backup, e só o que ficou
 * pela metade é apagado: o original dele já foi truncado, e meio arquivo é
 * pior que nenhum. Sem lista, não há limite de arquivos, e repetir o APPLY
 * completa o que faltou.
 *
 * Manter o que terminou só é seguro com bytes conferidos, e o CRC da carga só
 * fecha no fim. Por isso o APPLY só aceita o backup validado por último
 * (s_validated, abaixo): um .bkp cortado no download ou estragado no disco
 * para na validação, antes de qualquer escrita. A impressão é o cabeçalho, não
 * os bytes — um arquivo trocado entre as duas chamadas ainda só falha no fim —
 * e não é barreira de segurança: o APPLY já exige o administrador pleno.
 * ------------------------------------------------------------------------- */

/* A impressão do último backup validado com sucesso: o APPLY só aceita um
 * cabeçalho igual. O cabeçalho carrega o CRC e o tamanho da carga, e o seu
 * próprio CRC cobre o resto (chip, versão, carimbo). */
static bool     s_validated = false;
static uint32_t s_val_header_crc, s_val_payload_crc, s_val_payload_size;

static bool header_was_validated(const BackupHeader& h) {
    return s_validated && h.header_crc32 == s_val_header_crc &&
           h.payload_crc32 == s_val_payload_crc && h.payload_size == s_val_payload_size;
}

/* O arquivo que o APPLY deixou pela metade, se houver: fecha e apaga. */
static void drop_incomplete(RestoreSession& s) {
    if (s.cur_file) s.cur_file.close();
    if (s.cur_incomplete) {
        watchdog_update();
        LittleFS.remove(s.cur_path);
        s.cur_incomplete = false;
    }
}

/* ---------------------------------------------------------------------------
 * Session lifecycle
 * ------------------------------------------------------------------------- */

static void session_reset(RestoreSession& s) {
    s.phase = RestorePhase::HEADER;
    s.status = BackupStatus::INTERNAL_ERROR;  /* substituído quando finish() */
    s.header_filled = 0;
    memset(&s.header, 0, sizeof(s.header));
    s.payload_remaining = 0;
    s.payload_crc = OTA_CRC32_INIT;
    s.file_count = 0;
    s.entry_filled = 0;
    s.cur_path_len = 0;
    s.cur_content_len = 0;
    s.cur_path[0] = '\0';
    s.cur_path_filled = 0;
    s.cur_content_remaining = 0;
    s.cur_incomplete = false;
    s.wrote_any = false;
    if (s.cur_file) s.cur_file.close();
}

void restore_session_begin(RestoreSession& s, RestoreMode mode) {
    s.mode = mode;
    session_reset(s);
}

void restore_session_abort(RestoreSession& s) {
    if (s.mode == RestoreMode::APPLY) drop_incomplete(s);
    else if (s.cur_file) s.cur_file.close();
    s.phase = RestorePhase::FAILED;
    s.status = BackupStatus::IO_ERROR;
}

/* ---------------------------------------------------------------------------
 * State machine
 * ------------------------------------------------------------------------- */

static void fail(RestoreSession& s, BackupStatus st) {
    s.status = st;
    s.phase = RestorePhase::FAILED;
    if (s.cur_file) s.cur_file.close();
}

/* Parse e valida header (chamado quando header_filled == sizeof(BackupHeader)). */
static void on_header_complete(RestoreSession& s) {
    memcpy(&s.header, s.header_buf, sizeof(BackupHeader));

    if (s.header.magic != OTA_BACKUP_MAGIC) { fail(s, BackupStatus::BAD_MAGIC); return; }
    if (s.header.schema_version != OTA_BACKUP_SCHEMA) {
        fail(s, BackupStatus::UNSUPPORTED_SCHEMA); return;
    }

    /* Header CRC: primeiros 36 bytes. */
    uint32_t hcrc = crc32_update(OTA_CRC32_INIT, s.header_buf,
                                 sizeof(BackupHeader) - sizeof(uint32_t));
    hcrc ^= 0xFFFFFFFFu;
    if (hcrc != s.header.header_crc32) { fail(s, BackupStatus::HEADER_CRC_MISMATCH); return; }

    uint8_t my_chip[8];
    read_chip_id(my_chip);
    if (memcmp(my_chip, s.header.chip_id, 8) != 0) {
        fail(s, BackupStatus::CHIP_ID_MISMATCH); return;
    }
    /* Antes de qualquer escrita: um APPLY só grava o backup validado por
     * último, e é isso que faz um arquivo terminado ser confiável numa falha
     * adiante. A página sempre validou antes; agora o aparelho exige. */
    if (s.mode == RestoreMode::APPLY && !header_was_validated(s.header)) {
        fail(s, BackupStatus::NOT_VALIDATED); return;
    }

    s.payload_remaining = s.header.payload_size;
    if (s.payload_remaining == 0) {
        /* Backup vazio. CRC esperado é 0xFFFFFFFF ^ xor-out = 0. */
        uint32_t pcrc = OTA_CRC32_INIT ^ 0xFFFFFFFFu;
        if (pcrc != s.header.payload_crc32) { fail(s, BackupStatus::PAYLOAD_CRC_MISMATCH); return; }
        s.phase = RestorePhase::DONE;
        s.status = BackupStatus::OK;
        return;
    }
    s.phase = RestorePhase::ENTRY_HEADER;
    s.entry_filled = 0;
}

static void on_entry_header_complete(RestoreSession& s) {
    s.cur_path_len     = (uint16_t)s.entry_buf[0] | ((uint16_t)s.entry_buf[1] << 8);
    s.cur_content_len  = (uint32_t)s.entry_buf[2]
                       | ((uint32_t)s.entry_buf[3] << 8)
                       | ((uint32_t)s.entry_buf[4] << 16)
                       | ((uint32_t)s.entry_buf[5] << 24);

    if (s.cur_path_len == 0 || s.cur_path_len > RESTORE_MAX_PATH) {
        fail(s, BackupStatus::PATH_TOO_LONG); return;
    }
    if (s.cur_path_len > s.payload_remaining) { fail(s, BackupStatus::PAYLOAD_TRUNCATED); return; }
    if (s.cur_content_len > s.payload_remaining - s.cur_path_len) {
        fail(s, BackupStatus::PAYLOAD_TRUNCATED); return;
    }

    s.cur_path_filled = 0;
    s.phase = RestorePhase::PATH;
}

static void on_path_complete(RestoreSession& s) {
    s.cur_path[s.cur_path_len] = '\0';
    if (!path_is_safe(s.cur_path, s.cur_path_len)) {
        fail(s, BackupStatus::PATH_INVALID); return;
    }
    if (s.mode == RestoreMode::APPLY) {
        /* Escreve direto no path final (sem rename): ver "Falha no meio de um
         * APPLY", no começo deste arquivo.
         *
         * F-RESTORE fix: feed WDT antes de cada operação que pode triggerar
         * GC do LittleFS (mkdir, open com truncate). Sob LFS fragmentado
         * (>70%), GC interno bloqueia por segundos. Em backup com 32 arquivos
         * o tempo total acumulado excedia WDT 8s e o restore perdia o tail
         * do payload (lang/themes/últimos history). */
        watchdog_update();
        ensure_parent_dirs(s.cur_path);
        watchdog_update();
        if (s.cur_file) s.cur_file.close();
        s.cur_file = LittleFS.open(s.cur_path, "w");
        watchdog_update();
        s.wrote_any = true;   /* "w" já truncou o original */
        if (!s.cur_file) { fail(s, BackupStatus::IO_ERROR); return; }
        s.cur_incomplete = true;
    }
    s.cur_content_remaining = s.cur_content_len;
    s.phase = RestorePhase::CONTENT;
}

static void on_content_complete(RestoreSession& s) {
    if (s.mode == RestoreMode::APPLY && s.cur_file) {
        s.cur_file.close();
    }
    s.cur_incomplete = false;
    s.file_count++;
    s.entry_filled = 0;
    if (s.payload_remaining == 0) {
        /* Validação final do CRC do payload. */
        uint32_t pcrc = s.payload_crc ^ 0xFFFFFFFFu;
        if (pcrc != s.header.payload_crc32) { fail(s, BackupStatus::PAYLOAD_CRC_MISMATCH); return; }
        s.phase = RestorePhase::DONE;
        s.status = BackupStatus::OK;
    } else {
        s.phase = RestorePhase::ENTRY_HEADER;
    }
}

bool restore_session_feed(RestoreSession& s, const uint8_t* data, size_t len) {
    /* Um arquivo vazio não traz byte nenhum para a fase CONTENT, então o laço
     * gira mais uma vez por ele mesmo sem bytes por ler. Sem isso, um vazio no
     * fim da carga deixava a sessão em CONTENT, e o finish marcava como
     * truncado um backup inteiro — nem a validação passava (achado ao escrever
     * os testes, 2026-10-02). */
    while ((len > 0 || (s.phase == RestorePhase::CONTENT && s.cur_content_remaining == 0)) &&
           s.phase != RestorePhase::FAILED &&
           s.phase != RestorePhase::DONE) {
        watchdog_update();
        switch (s.phase) {
            case RestorePhase::HEADER: {
                size_t need = sizeof(BackupHeader) - s.header_filled;
                size_t take = (len < need) ? len : need;
                memcpy(s.header_buf + s.header_filled, data, take);
                s.header_filled += take;
                data += take; len -= take;
                if (s.header_filled == sizeof(BackupHeader)) on_header_complete(s);
                break;
            }
            case RestorePhase::ENTRY_HEADER: {
                size_t need = sizeof(s.entry_buf) - s.entry_filled;
                size_t take = (len < need) ? len : need;
                /* Bytes do entry header CONTAM no payload_crc + payload_remaining. */
                if (take > s.payload_remaining) { fail(s, BackupStatus::PAYLOAD_TRUNCATED); break; }
                memcpy(s.entry_buf + s.entry_filled, data, take);
                s.payload_crc = crc32_update(s.payload_crc, data, take);
                s.payload_remaining -= take;
                s.entry_filled += take;
                data += take; len -= take;
                if (s.entry_filled == sizeof(s.entry_buf)) on_entry_header_complete(s);
                break;
            }
            case RestorePhase::PATH: {
                size_t need = s.cur_path_len - s.cur_path_filled;
                size_t take = (len < need) ? len : need;
                if (take > s.payload_remaining) { fail(s, BackupStatus::PAYLOAD_TRUNCATED); break; }
                memcpy(s.cur_path + s.cur_path_filled, data, take);
                s.payload_crc = crc32_update(s.payload_crc, data, take);
                s.payload_remaining -= take;
                s.cur_path_filled += take;
                data += take; len -= take;
                if (s.cur_path_filled == s.cur_path_len) on_path_complete(s);
                break;
            }
            case RestorePhase::CONTENT: {
                size_t need = s.cur_content_remaining;
                size_t take = (len < need) ? len : need;
                if (take > s.payload_remaining) { fail(s, BackupStatus::PAYLOAD_TRUNCATED); break; }
                s.payload_crc = crc32_update(s.payload_crc, data, take);
                if (s.mode == RestoreMode::APPLY && s.cur_file) {
                    size_t w = s.cur_file.write(data, take);
                    if (w != take) { fail(s, BackupStatus::IO_ERROR); break; }
                }
                s.payload_remaining -= take;
                s.cur_content_remaining -= take;
                data += take; len -= take;
                if (s.cur_content_remaining == 0) on_content_complete(s);
                break;
            }
            case RestorePhase::DONE:
            case RestorePhase::FAILED:
                break;  /* unreachable, mas keep -Wswitch happy */
        }
    }
    return s.phase != RestorePhase::FAILED;
}

BackupStatus restore_session_finish(RestoreSession& s, bool* fs_modified) {
    if (fs_modified) *fs_modified = false;

    if (s.cur_file) s.cur_file.close();

    /* Se ainda em meio ao stream, marca como truncated. */
    if (s.phase != RestorePhase::DONE && s.phase != RestorePhase::FAILED) {
        fail(s, BackupStatus::PAYLOAD_TRUNCATED);
    }

    const bool ok = (s.phase == RestorePhase::DONE && s.status == BackupStatus::OK);
    if (s.mode == RestoreMode::VALIDATE) {
        if (ok) {
            s_validated = true;
            s_val_header_crc = s.header.header_crc32;
            s_val_payload_crc = s.header.payload_crc32;
            s_val_payload_size = s.header.payload_size;
        }
    } else {
        if (!ok) drop_incomplete(s);
        if (fs_modified) *fs_modified = s.wrote_any;
    }
    return s.status;
}

} /* namespace ota */
