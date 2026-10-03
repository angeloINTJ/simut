/**
 * @file    src/ota/restore.h
 * @brief   State machine de restore de backup .bkp via streaming chunks.
 *
 * @details Diferente de backup_validate (que precisa de Stream& seekable),
 *          o restore consome chunks vindos do upload HTTP (callback
 *          handleUploadData) e mantém estado entre chunks.
 *
 *          Modo VALIDATE: lê + computa CRCs + valida; nunca toca LittleFS.
 *                      Guarda a impressão (cabeçalho) do backup validado.
 *          Modo APPLY: só aceita o backup validado por último; escreve cada
 *                      arquivo direto no caminho final, e numa falha apaga
 *                      só o arquivo que ficou incompleto. Os que terminou
 *                      ficam: o original de cada um já foi sobrescrito, e o
 *                      conteúdo é o do backup validado (achado 58, 2026-10-02).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "backup.h"
#include "backup_format.h"
#include <LittleFS.h>

namespace ota {

constexpr size_t RESTORE_MAX_PATH = 200;  /* < 256 (path_len uint16) e cabe no struct */

enum class RestoreMode : uint8_t {
    VALIDATE = 0,   /**< Não escreve em LittleFS. */
    APPLY = 1,      /**< Escreve no caminho final; numa falha, apaga só o incompleto. */
};

enum class RestorePhase : uint8_t {
    HEADER = 0,         /**< Acumulando 40 bytes do BackupHeader. */
    ENTRY_HEADER = 1,   /**< Acumulando 6 bytes do BackupEntry. */
    PATH = 2,           /**< Acumulando path_len bytes do path. */
    CONTENT = 3,        /**< Acumulando/escrevendo content_len bytes. */
    DONE = 4,           /**< payload_size completo, CRC validado. */
    FAILED = 5,         /**< Erro detectado; status conta o porquê. */
};

struct RestoreSession {
    RestoreMode  mode;
    RestorePhase phase;
    BackupStatus status;

    /* HEADER phase */
    uint8_t  header_buf[sizeof(BackupHeader)];
    uint32_t header_filled;
    BackupHeader header;

    /* PAYLOAD phase */
    uint32_t payload_remaining;
    uint32_t payload_crc;
    uint16_t file_count;

    /* ENTRY phase */
    uint8_t  entry_buf[6];
    uint32_t entry_filled;
    uint16_t cur_path_len;
    uint32_t cur_content_len;

    /* PATH phase */
    char     cur_path[RESTORE_MAX_PATH + 1];  /* +1 nul */
    uint32_t cur_path_filled;

    /* CONTENT phase */
    File     cur_file;            /**< Aberto só em APPLY. */
    bool     cur_incomplete;      /**< APPLY: cur_path aberto e ainda sem todo o conteúdo. */
    bool     wrote_any;           /**< APPLY: algum arquivo foi aberto para escrita. */
    uint32_t cur_content_remaining;
};

/**
 * @brief Inicializa sessão.
 */
void restore_session_begin(RestoreSession& s, RestoreMode mode);

/**
 * @brief Alimenta um chunk de bytes. Pode ser chamada várias vezes.
 *
 * Se o estado entrar em FAILED, chamadas subsequentes são no-op.
 *
 * @return true se ainda não falhou (FAILED ou DONE não diferenciados aqui).
 */
bool restore_session_feed(RestoreSession& s, const uint8_t* data, size_t len);

/**
 * @brief Finaliza a sessão.
 *
 * Em VALIDATE: reporta o status; com OK, guarda a impressão do backup que o
 *              próximo APPLY tem de trazer.
 * Em APPLY:   numa falha, apaga o arquivo que ficou incompleto (o original já
 *              foi truncado); os terminados ficam.
 *
 * @param s         Sessão.
 * @param fs_modified  Out: true se o APPLY abriu algum arquivo para escrita —
 *                     com sucesso ou não: um APPLY que falhou depois de gravar
 *                     também mudou o sistema de arquivos.
 * @return Status final.
 */
BackupStatus restore_session_finish(RestoreSession& s, bool* fs_modified);

/**
 * @brief Aborta a sessão: em APPLY, apaga o arquivo que ficou incompleto.
 *
 * Para uso em UPLOAD_FILE_ABORTED.
 */
void restore_session_abort(RestoreSession& s);

} /* namespace ota */
