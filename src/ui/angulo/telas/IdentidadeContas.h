#pragma once
// The account screens of the identity group (ESPEC_UI_A.md §6.4): who is at the
// panel (MODE_AUTH_USER), the list of accounts (MODE_SETTINGS_USERS) and one
// account (MODE_SETTINGS_USER_EDIT). They hold what is on the glass: a list
// model carries the rows of ITS PAGE and the total, not every account, so the
// model stays small and the firmware hashes few bytes.
// Touch is Controller (C): a tap on a row opens it; the glue does what the old
// screen did when it went through that row.
#include "IdentidadeComum.h"

namespace ua {
namespace tela {

// The three panel bits of an account, as the screens show them. The glue maps
// them from PERM_ALARM_LIMITS / PERM_ALARM_BLOCK / PERM_MAINT (SystemDefs_Limits.h).
constexpr uint8_t CTA_PERM_LIMITES = 1, CTA_PERM_ALARMES = 2, CTA_PERM_MANUT = 4;

/* ------------------------------------------------------------------ */
/* Who is at the panel                                                  */
/* ------------------------------------------------------------------ */

struct VmQuem {
  uint8_t total;                 // accounts with a PIN, the admin included, on every page
  uint8_t pagina;                // 0-based
  uint8_t nLinhas;               // rows on this page, 0..4
  struct Linha {
    char nome[ident::NOME];
    bool travada;                // out of tries until the device restarts: dimmed, "Bloqueada", not a target
  } linhas[ident::LINHAS];
};
// LINHA: indice = the row's place among all accounts (page * 4 + row), like
// DisplayManager::_usersMap's. An arrow is a target only when it goes somewhere.
enum AlvoQuem : uint8_t { QUEM_NADA, QUEM_VOLTAR, QUEM_LINHA, QUEM_PAG_ANTERIOR, QUEM_PAG_PROXIMA };
void desenharQuem( Pintor &p, const VmQuem &vm );
int alvosQuem( const VmQuem &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* The list of accounts                                                 */
/* ------------------------------------------------------------------ */

struct VmUsuarios {
  uint8_t total;                 // accounts listed (the admin is not one), on every page
  uint8_t pagina;                // 0-based
  uint8_t nLinhas;               // rows on this page, 0..4
  bool podeCriar;                // the session may manage accounts: "Nova conta" is alive
  struct Linha {
    char nome[ident::NOME];
    uint8_t perms;               // CTA_PERM_* bits: written as "Limites · Alarmes · Manut." or "Só leitura"
    bool temPin;                 // no PIN: the badge "Sem PIN" (an account without one cannot act at the panel)
  } linhas[ident::LINHAS];
};
// NOVA is a target only with podeCriar. LINHA: indice as in VmQuem.
enum AlvoUsuarios : uint8_t { USR_NADA, USR_VOLTAR, USR_NOVA, USR_LINHA, USR_PAG_ANTERIOR, USR_PAG_PROXIMA };
void desenharUsuarios( Pintor &p, const VmUsuarios &vm );
int alvosUsuarios( const VmUsuarios &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* One account                                                          */
/* ------------------------------------------------------------------ */

struct VmConta {
  bool nova;                     // being created: the name was just typed, no PIN yet
  uint8_t perms;                 // CTA_PERM_* bits as edited (the copy, not yet saved)
  bool temPin;                   // existing account: a PIN is set
  char nome[ident::NOME];        // the title
};
// INTERRUPTOR: indice 0 limits, 1 block alarms, 2 maintenance. DEFINIR_PIN and
// EXCLUIR exist only for an existing account. Voltar and Cancelar both leave
// without saving, as the old SAIR did.
enum AlvoConta : uint8_t { CTA_NADA, CTA_VOLTAR, CTA_INTERRUPTOR, CTA_DEFINIR_PIN, CTA_EXCLUIR, CTA_SALVAR, CTA_CANCELAR };
void desenharConta( Pintor &p, const VmConta &vm );
int alvosConta( const VmConta &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
