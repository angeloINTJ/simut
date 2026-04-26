#!/usr/bin/env python3
"""F-LANGPACK Etapa 3 — gera data/lang/language_pt-BR.lng.

Coleta material de:
  • git show 130d6de:DisplayManager_i18n.cpp     → DICTIONARY[1] (78 PT strings)
  • git show 130d6de:LogManager.cpp              → translateCodePt switch (~115 mappings)
  • SystemDefs_Logging.h                          → enum LogCode (name → numeric)
  • /tmp/trl_pairs.tsv                            → 60 (en, pt) pares de TRL
  • data/help_pt.txt                              → bloco @HELP
  • data/license_pt.txt                           → bloco @LICENSE

Computa FNV-1a 32-bit do EN para cada par TRL e emite o .lng final.
Acentos são restaurados manualmente em PT_ACCENTS (post-processing).
"""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path("/home/angelo/Documentos/SIMUT_Versa")
BASELINE_SHA = "130d6de"
OUT = ROOT / "data" / "lang" / "language_pt-BR.lng"


def fnv1a32(s: str) -> int:
    h = 0x811c9dc5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def git_show(path: str) -> str:
    return subprocess.run(
        ["git", "show", f"{BASELINE_SHA}:{path}"],
        check=True, cwd=ROOT, capture_output=True, text=True,
    ).stdout


def parse_logcode_enum(src: str) -> dict[str, int]:
    """Parses `enum LogCode { ... };` returning {name: value}."""
    m = re.search(r"enum\s+LogCode\s*\{(.*?)\};", src, re.DOTALL)
    if not m:
        raise RuntimeError("LogCode enum not found")
    body = m.group(1)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", "", body)

    out: dict[str, int] = {}
    current = 0
    for entry in body.split(","):
        entry = entry.strip()
        if not entry:
            continue
        if "=" in entry:
            name, val = entry.split("=", 1)
            name = name.strip()
            try:
                current = int(val.strip(), 0)
            except ValueError:
                continue
        else:
            name = entry.strip()
        if not re.fullmatch(r"\w+", name):
            continue
        out[name] = current
        current += 1
    return out


def parse_dictionary_pt(src: str) -> list[str]:
    """Extracts the 2nd `{ ... }` block of DICTIONARY[LANG_COUNT][TR_KEYS_COUNT]."""
    # Find DICTIONARY = { ...
    m = re.search(r"DICTIONARY\[[^\]]+\]\[[^\]]+\]\s*=\s*\{(.*?)\n\};", src, re.DOTALL)
    if not m:
        raise RuntimeError("DICTIONARY block not found")
    body = m.group(1)
    # Split into 2 sub-blocks at top-level `{` and `}`
    blocks = []
    depth = 0
    cur_start = -1
    for i, c in enumerate(body):
        if c == "{":
            if depth == 0:
                cur_start = i + 1
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                blocks.append(body[cur_start:i])
    if len(blocks) < 2:
        raise RuntimeError(f"Expected 2 lang sub-blocks, got {len(blocks)}")
    pt_block = blocks[1]
    # Extract all C string literals: "..." (no escapes in this codebase)
    return re.findall(r'"([^"]*)"', pt_block)


def parse_translate_code_pt(src: str) -> list[tuple[str, str]]:
    """Returns [(CASE_NAME, "PT text"), ...] preserving source order."""
    m = re.search(
        r"static\s+const\s+char\*\s+translateCodePt\s*\([^)]*\)\s*\{(.*?)^\}",
        src, re.DOTALL | re.MULTILINE,
    )
    if not m:
        raise RuntimeError("translateCodePt not found")
    body = m.group(1)
    pairs: list[tuple[str, str]] = []
    for cm in re.finditer(r"case\s+(\w+)\s*:\s*return\s*\"([^\"]*)\"\s*;", body):
        pairs.append((cm.group(1), cm.group(2)))
    return pairs


def parse_lang_keys(src: str) -> list[str]:
    """Returns the names of LangKey enum entries in source order
       (excluding the TR_KEYS_COUNT sentinel)."""
    m = re.search(r"enum\s+LangKey\s*\{(.*?)\}\s*;", src, re.DOTALL)
    if not m:
        raise RuntimeError("LangKey enum not found")
    body = m.group(1)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", "", body)
    names = []
    for tok in re.split(r"[,\s]+", body):
        tok = tok.strip()
        if not tok or not re.fullmatch(r"\w+", tok):
            continue
        if tok == "TR_KEYS_COUNT":
            break
        names.append(tok)
    return names


def load_trl_pairs(path: Path) -> list[tuple[str, str]]:
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        if "\t" not in line:
            continue
        en, pt = line.split("\t", 1)
        out.append((en, pt))
    return out


def find_balanced_braces(text: str, start: int) -> int:
    """Returns index of matching `}` for the `{` at `start`, accounting for
       quoted strings (so `{s}` inside a JS string literal doesn't confuse
       the depth counter). Returns -1 if unbalanced."""
    assert text[start] == "{"
    depth = 0
    in_str = False
    escape = False
    for i in range(start, len(text)):
        c = text[i]
        if escape:
            escape = False
            continue
        if in_str:
            if c == "\\":
                escape = True
            elif c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return i
    return -1


def extract_webui_pt_dicts(webui_text: str) -> dict[str, str]:
    """Walks WebUI.h finding every `pt: {...}` block, parses as JSON, merges
       into a single dict. Keys are unique across pages (prefix-namespaced)."""
    merged: dict[str, str] = {}
    pos = 0
    while True:
        idx = webui_text.find("pt: {", pos)
        if idx < 0:
            break
        brace_open = webui_text.find("{", idx)
        brace_close = find_balanced_braces(webui_text, brace_open)
        if brace_close < 0:
            raise RuntimeError(f"Unbalanced pt: block at offset {idx}")
        block = webui_text[brace_open: brace_close + 1]
        try:
            d = json.loads(block)
            for k, v in d.items():
                if k in merged and merged[k] != v:
                    print(f"  WARN: key '{k}' redefined in pt block")
                merged[k] = v
        except Exception as e:
            raise RuntimeError(f"JSON parse failed at offset {idx}: {e}\nBlock: {block[:200]}")
        pos = brace_close + 1
    return merged


# ── Acentuação manual: dicionário "ASCII PT atual → UTF-8 com acentos" ──
# Aplicado em pós-processamento sobre cada string PT antes de escrever.
# Cobre as transformações mais comuns; o restante segue ASCII.
PT_ACCENTS: dict[str, str] = {
    # ── Substantivos comuns (com variantes de capitalização) ──
    "CONFIGURACOES": "CONFIGURAÇÕES",
    "Configuracoes": "Configurações",
    "configuracoes": "configurações",
    "CONFIGURACAO": "CONFIGURAÇÃO",
    "Configuracao": "Configuração",
    "configuracao": "configuração",
    "CALIBRACAO": "CALIBRAÇÃO",
    "Calibracao": "Calibração",
    "calibracao": "calibração",
    "Calibracoes": "Calibrações",
    "calibracoes": "calibrações",
    "SESSAO": "SESSÃO",
    "Sessao": "Sessão",
    "sessao": "sessão",
    "Sessoes": "Sessões",
    "sessoes": "sessões",
    "Inicializacao": "Inicialização",
    "Reinicializacao": "Reinicialização",
    "Autenticacao": "Autenticação",
    "Confirmacao": "Confirmação",
    "Aplicacao": "Aplicação",
    "Sincronizacao": "Sincronização",
    "Atualizacao": "Atualização",
    "Operacao": "Operação",
    "Migracao": "Migração",
    "Conexao": "Conexão",
    "Sao": "São",
    "Versao": "Versão",
    "Memoria": "Memória",
    "Historico": "Histórico",
    "Grafico": "Gráfico",
    "Graficos": "Gráficos",
    "Hardware": "Hardware",  # fica igual
    "Codigo": "Código",
    "Codigos": "Códigos",
    "Ingles": "Inglês",
    "Portugues": "Português",
    "Acesso": "Acesso",
    "Sucesso": "Sucesso",
    "Maximo": "Máximo",
    "Minimo": "Mínimo",
    "Maxima": "Máxima",
    "Minima": "Mínima",
    "Padrao": "Padrão",
    "Limite": "Limite",
    "Limites": "Limites",
    "Senha": "Senha",
    "Senhas": "Senhas",
    "Sistema": "Sistema",
    "Sensor": "Sensor",
    "Sensores": "Sensores",
    "Volume": "Volume",
    "Vol.": "Vol.",
    "Sons": "Sons",
    "Som": "Som",
    "Tema": "Tema",
    "Temas": "Temas",
    "Idioma": "Idioma",
    "Botao": "Botão",
    "Botoes": "Botões",
    # Adjetivos
    "Invalido": "Inválido",
    "Invalida": "Inválida",
    "Valido": "Válido",
    "Valida": "Válida",
    "Critico": "Crítico",
    "Critica": "Crítica",
    "Ativo": "Ativo",
    "Inativo": "Inativo",
    "Necessario": "Necessário",
    "Necessaria": "Necessária",
    "Permitido": "Permitido",
    "Disponivel": "Disponível",
    "Indisponivel": "Indisponível",
    "Salvo": "Salvo",
    "Salva": "Salva",
    "Concluida": "Concluída",
    "Concluido": "Concluído",
    "Imprecisos": "Imprecisos",
    "Inicial": "Inicial",
    "Recuperada": "Recuperada",
    "Recuperado": "Recuperado",
    "Recuperacao": "Recuperação",
    # Verbos
    "Aplicar": "Aplicar",
    "Aplicando": "Aplicando",
    "Salvar": "Salvar",
    "Cancelar": "Cancelar",
    "Carregando": "Carregando",
    "Lendo": "Lendo",
    "Reiniciando": "Reiniciando",
    "Reset": "Reset",
    "Solicitado": "Solicitado",
    "Acionado": "Acionado",
    "Disparado": "Disparado",
    "Cancelado": "Cancelado",
    "Cancelada": "Cancelada",
    "Silenciado": "Silenciado",
    "Silenciar": "Silenciar",
    "Desativar": "Desativar",
    "Apagar": "Apagar",
    "Apagado": "Apagado",
    "Resetada": "Resetada",
    "Resetado": "Resetado",
    "Apagou": "Apagou",
    "Resetou": "Resetou",
    "Procurando": "Procurando",
    "Conectando": "Conectando",
    "Conectado": "Conectado",
    "Conectada": "Conectada",
    "Iniciando": "Iniciando",
    "Iniciado": "Iniciado",
    "Iniciada": "Iniciada",
    "Salva": "Salva",  # dup ok
    "Tente": "Tente",
    "Toque": "Toque",
    "Aguarde": "Aguarde",
    # Outras frases-chave (palavras compostas)
    "Falha": "Falha",
    "Padrao": "Padrão",
    "Setado": "Setado",
    "Setada": "Setada",
    "Excedido": "Excedido",
    "Excedida": "Excedida",
    "Excedidas": "Excedidas",
    "Excedidas!": "Excedidas!",
    "Tentativas": "Tentativas",
    "Logout:": "Logout:",
    "Apos": "Após",
    "Atras": "Atrás",
    "Apenas": "Apenas",
    "Possivel": "Possível",
    "Impossivel": "Impossível",
    "Multiplo": "Múltiplo",
    "Multipla": "Múltipla",
    "Multiplas": "Múltiplas",
    "Multiplos": "Múltiplos",
    "Categoria": "Categoria",
    "Categorias": "Categorias",
    "Identico": "Idêntico",
    "Identica": "Idêntica",
    "Generico": "Genérico",
    "Generica": "Genérica",
    "Ultimo": "Último",
    "Ultima": "Última",
    "Pendente": "Pendente",
    "Modulo": "Módulo",
    "Modulos": "Módulos",
    "Acao": "Ação",
    "Acoes": "Ações",
    "Sao": "São",
    "Voce": "Você",
    "Esta": "Está",  # cuidado com "Esta página" vs "Está/Esta" — manter sob revisão
    "Tras": "Trás",
    "tres": "três",
    "Tres": "Três",
    "Numero": "Número",
    "Numeros": "Números",
    "Permissao": "Permissão",
    "Permissoes": "Permissões",
    "Razao": "Razão",
    "Razoes": "Razões",
    "Excecao": "Exceção",
    "Excecoes": "Exceções",
    "Funcao": "Função",
    "Funcoes": "Funções",
    "Selecao": "Seleção",
    "Selecionar": "Selecionar",
    "Pagina": "Página",
    "Paginas": "Páginas",
    "Aplicado": "Aplicado",
    "Aplicada": "Aplicada",
    "Aplicar": "Aplicar",
    "Salvas": "Salvas",
    "Encontrado": "Encontrado",
    "Encontrada": "Encontrada",
    "Encontrados": "Encontrados",
    "Nao": "Não",
    "Sim": "Sim",
    "Logs": "Logs",
    "Tela": "Tela",
    "Telas": "Telas",
    "Tamanho": "Tamanho",
    "Acionada": "Acionada",
    "Modo": "Modo",
    "Mais": "Mais",
    "Menos": "Menos",
    "Ja": "Já",
    "Verificando": "Verificando",
    "Provisoria": "Provisória",
    "Provisorio": "Provisório",
    "Stealth": "Stealth",
    "Dormente": "Dormente",
    "Limite": "Limite",
    "Insertir": "Inserir",  # typo defensive
    "Calibracao": "Calibração",  # dup ok
    "Sufixo": "Sufixo",
    "Acionado": "Acionado",
    "Acidental": "Acidental",
    "Aproximada": "Aproximada",
    "Pode": "Pode",
    "Forcado": "Forçado",
    "Forcada": "Forçada",
    "Forcar": "Forçar",
    "Forcando": "Forçando",
    # Frases muito específicas (substring matching)
    "Calibracao do Touch": "Calibração do Touch",
    "Calibracao do touch": "Calibração do touch",
    "Toque na mira": "Toque na mira",
    "Calibracao Concluida": "Calibração Concluída",
    "Toques imprecisos": "Toques imprecisos",
    "muito curta": "muito curta",
    "nao coincidem": "não coincidem",
    "Vol. Sistema": "Vol. Sistema",
    "Vol. Alarme": "Vol. Alarme",
    "Sensibilidade do Toque": "Sensibilidade do Toque",
    "Status do Sistema": "Status do Sistema",
    "Modo de Configuracao": "Modo de Configuração",
    "Alinhamento da Tela": "Alinhamento da Tela",
    "Bem-vindo": "Bem-vindo",
    "Acesso Web": "Acesso Web",
    # OK / typo passthrough
    "OK": "OK",
    # ── Audit pass: palavras encontradas no .lng v1 que faltavam acento ──
    "ACAO": "AÇÃO",
    "ADEQUACAO": "ADEQUAÇÃO",
    "COMERCIALIZACAO": "COMERCIALIZAÇÃO",
    "Correcao": "Correção", "correcao": "correção",
    "GRAFICO": "GRÁFICO", "grafico": "gráfico", "graficos": "gráficos",
    "Licenca": "Licença", "licenca": "licença",
    "MANUTENCAO": "MANUTENÇÃO",
    "MAXIMO": "MÁXIMO", "MINIMO": "MÍNIMO",
    "NAO": "NÃO",
    "NEGOCIACOES": "NEGOCIAÇÕES",
    "RECLAMACAO": "RECLAMAÇÃO",
    "Relatorio": "Relatório", "relatorio": "relatório",
    "Resolucao": "Resolução", "resolucao": "resolução",
    "Usuario": "Usuário", "usuario": "usuário", "usuarios": "usuários",
    "VIOLACAO": "VIOLAÇÃO",
    "alteracoes": "alterações", "Alteracoes": "Alterações",
    "amigavel": "amigável", "Amigavel": "Amigável",
    "concluida": "concluída", "concluido": "concluído",
    "condicoes": "condições", "Condicoes": "Condições",
    "critica": "crítica", "Critica": "Crítica",
    "documentacao": "documentação", "Documentacao": "Documentação",
    "forcado": "forçado", "forcada": "forçada", "forcando": "forçando",
    "historico": "histórico", "Historicos": "Históricos",
    "invalido": "inválido", "invalida": "inválida",
    "limitacao": "limitação", "Limitacao": "Limitação",
    "nao": "não",
    "necessaria": "necessária", "necessario": "necessário",
    "primaria": "primária", "primario": "primário",
    "provisoria": "provisória",
    "restricoes": "restrições",
    "sao": "são",
    "secundario": "secundário", "secundaria": "secundária",
    "sincronizacao": "sincronização",
    # ── Termos técnicos comuns em logs ──
    "Telemetria": "Telemetria", "telemetria": "telemetria",  # já correta
    "preso": "preso", "presa": "presa",  # sem acento
    "salva": "salva",  # sem acento
    "sublicenciar": "sublicenciar",  # sem acento
    # ── Outras palavras do help_pt.txt e license_pt.txt ──
    "ARQUIVOS": "ARQUIVOS",  # sem acento
    "Versao:": "Versão:",
    "primeiramente": "primeiramente",
    "diretorio": "diretório", "Diretorio": "Diretório",
    "diretorios": "diretórios",
    "ortografico": "ortográfico", "ortografica": "ortográfica",
    "estatistica": "estatística", "estatisticas": "estatísticas",
    "Estatistica": "Estatística", "Estatisticas": "Estatísticas",
    "automatica": "automática", "automatico": "automático",
    "Automatica": "Automática", "Automatico": "Automático",
    "automatizado": "automatizado",  # sem acento
    "responsavel": "responsável", "Responsavel": "Responsável",
    "incluindo": "incluindo",  # sem acento
    "compativel": "compatível", "Compativel": "Compatível",
    "preferencias": "preferências",
    "Preferencias": "Preferências",
    "permissao": "permissão", "Permissao": "Permissão",
    "intervalo": "intervalo",  # sem acento
    "publico": "público", "publica": "pública",
    "Publico": "Público", "Publica": "Pública",
    "publicas": "públicas", "publicos": "públicos",
    "minimo": "mínimo", "minima": "mínima",
    "maximo": "máximo", "maxima": "máxima",
    "anonimo": "anônimo", "anonima": "anônima",
    "MIT": "MIT",
    "JavaScript": "JavaScript",
    "API": "API",
    "REST": "REST",
    "JSON": "JSON",
    # Help-specific
    "Pagina": "Página", "Paginas": "Páginas",
    "Numerico": "Numérico", "Numerica": "Numérica",
    "Endereco": "Endereço", "endereco": "endereço",
    "enderecos": "endereços",
    "binario": "binário",
    "comum": "comum",  # sem acento
    "comuns": "comuns",  # sem acento
    # Menores palavras autônomas
    "ate": "até", "Ate": "Até",
    "tem": "tem",  # sem acento (verbo) — cuidado se aparecer "Item"/"Tem"
    "voce": "você",
    "voces": "vocês",
    "esta": "está",  # ambíguo — "esta página" vs "está pronto"
    "esse": "esse", "essa": "essa",  # sem acento
    "isso": "isso",  # sem acento
    "ja": "já",
    "so": "só",
    # Frases comuns help/license
    "Substancial": "Substancial",  # sem acento
    "JUDICIAIS": "JUDICIAIS",  # sem acento
    "QUALQUER": "QUALQUER",  # sem acento
    "Direitos": "Direitos",  # sem acento
    "Reservados": "Reservados",  # sem acento
    "PROPRIETARIOS": "PROPRIETÁRIOS",
    "PROPRIETARIO": "PROPRIETÁRIO",
    "exigida": "exigida",  # sem acento
    "exigido": "exigido",  # sem acento
    "permitida": "permitida",  # sem acento
    "necessitar": "necessitar",  # sem acento
    "garantia": "garantia",  # sem acento
    "GARANTIA": "GARANTIA",  # sem acento
    "incluir": "incluir",  # sem acento
    "fornecer": "fornecer",  # sem acento
    "Comercializacao": "Comercialização",
    "comercializacao": "comercialização",
    "Adequacao": "Adequação", "adequacao": "adequação",
    "Reclamacao": "Reclamação", "reclamacao": "reclamação",
    "Violacao": "Violação", "violacao": "violação",
    "Manutencao": "Manutenção", "manutencao": "manutenção",
    "Negociacoes": "Negociações", "negociacoes": "negociações",
    "Acoes": "Ações", "acoes": "ações",
    "Acao": "Ação", "acao": "ação",
    # Help-strings adicional
    "Comandos": "Comandos",  # sem acento
    "Sintaxe": "Sintaxe",  # sem acento
    "interface": "interface",  # sem acento
    "Interface": "Interface",  # sem acento
    "remoto": "remoto",  # sem acento
    "remota": "remota",  # sem acento
    "Padrao:": "Padrão:",
    "ingles": "inglês",
    "portugues": "português",
    "espacos": "espaços",
    "espaco": "espaço",
    "Espaco": "Espaço", "Espacos": "Espaços",
    "tambem": "também",
    "Tambem": "Também",
    "ASCII": "ASCII",
    "UTF-8": "UTF-8",
    "Storage": "Storage",  # sem acento (termo técnico)
    "Touch": "Touch",
    "Display": "Display",
    "Bluetooth": "Bluetooth",
    "WiFi": "WiFi",
    "Acesso": "Acesso",  # sem acento
    "rapido": "rápido", "rapida": "rápida",
    "Rapido": "Rápido", "Rapida": "Rápida",
    "ultima": "última", "ultimo": "último",
    "ultimas": "últimas", "ultimos": "últimos",
    "Ultima": "Última", "Ultimo": "Último",
    "rotina": "rotina",  # sem acento
    "diaria": "diária", "diario": "diário",
    "Diaria": "Diária", "Diario": "Diário",
    "diarias": "diárias", "diarios": "diários",
    "horaria": "horária", "horario": "horário",
    "horarios": "horários",
    "Horario": "Horário", "Horaria": "Horária",
    "Horarios": "Horários",
    "preco": "preço",
    "Preco": "Preço",
    "exigirao": "exigirão",
    "incluirao": "incluirão",
    "lancamento": "lançamento", "Lancamento": "Lançamento",
    "executar": "executar",  # sem acento
    "ate aqui": "até aqui",
    # ── Audit pass 2 ──
    "Seguranca": "Segurança", "seguranca": "segurança",
    "SEGURANCA": "SEGURANÇA",
    "versao": "versão",
    "Colisao": "Colisão", "colisao": "colisão",
    "Divergencia": "Divergência", "divergencia": "divergência",
    "DIAGNOSTICO": "DIAGNÓSTICO",
    "ESPECIFICO": "ESPECÍFICO", "especifico": "específico", "especifica": "específica",
    "Especifico": "Específico", "Especifica": "Específica",
    "SERAO": "SERÃO", "serao": "serão",
    "estatico": "estático", "estatica": "estática",
    "Estatico": "Estático", "Estatica": "Estática",
    "fabrica": "fábrica", "Fabrica": "Fábrica",
    "fisico": "físico", "fisica": "física",
    "Fisico": "Físico", "Fisica": "Física",
    "padrao": "padrão",
}


def apply_accents(s: str) -> str:
    """Aplica substituições com fronteiras de palavra (re.escape + \\b) para
       evitar matches dentro de outras palavras (ex: 'so' não pode pegar
       'sólicitado'). Frases multi-palavra (com espaço) usam substring puro
       já que \\b não funciona no meio."""
    if not s:
        return s
    keys = sorted(PT_ACCENTS.keys(), key=len, reverse=True)
    for k in keys:
        v = PT_ACCENTS[k]
        if k == v:
            continue  # passthrough — sem trabalho
        if " " in k:
            # frase multi-palavra: substring puro
            if k in s:
                s = s.replace(k, v)
        else:
            # palavra única: word boundaries para evitar matches internos
            pattern = r"\b" + re.escape(k) + r"\b"
            s = re.sub(pattern, lambda _: v, s)
    return s


# ──────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────

print(f"== F-LANGPACK Etapa 3: gerando {OUT.name}")

# 1) LangKey enum (current, post-Etapa-1)
display_h = (ROOT / "DisplayManager.h").read_text(encoding="utf-8")
lang_keys = parse_lang_keys(display_h)
print(f"  LangKey: {len(lang_keys)} entries")

# 2) DICTIONARY[1] = PT strings, source @ 130d6de
old_i18n = git_show("DisplayManager_i18n.cpp")
dict_pt = parse_dictionary_pt(old_i18n)
print(f"  DICTIONARY[PT]: {len(dict_pt)} strings (esperado: {len(lang_keys)})")
if len(dict_pt) != len(lang_keys):
    raise RuntimeError(f"DICT mismatch: {len(dict_pt)} != {len(lang_keys)}")

# 3) LogCode enum
log_logging = (ROOT / "SystemDefs_Logging.h").read_text(encoding="utf-8")
logcode_map = parse_logcode_enum(log_logging)
print(f"  LogCode enum: {len(logcode_map)} entries")

# 4) translateCodePt @ 130d6de
old_logmgr = git_show("LogManager.cpp")
pt_cases = parse_translate_code_pt(old_logmgr)
print(f"  translateCodePt: {len(pt_cases)} cases")

# 5) TRL pairs
trl_pairs = load_trl_pairs(Path("/tmp/trl_pairs.tsv"))
print(f"  TRL pairs: {len(trl_pairs)}")

# 6) HELP/LICENSE — buscar do git@130d6de já que removemos os .txt PT
help_pt = git_show("data/help_pt.txt")
license_pt = git_show("data/license_pt.txt")

# 7) WEBDICT — extrai blocos pt:{...} de WebUI.h
webui_text = (ROOT / "WebUI.h").read_text(encoding="utf-8")
webdict = extract_webui_pt_dicts(webui_text)
print(f"  WEBDICT: {len(webdict)} keys extraídas de WebUI.h")

# ──────────────────────────────────────────────────────────────────────────
# Compose .lng
# ──────────────────────────────────────────────────────────────────────────
OUT.parent.mkdir(parents=True, exist_ok=True)
lines: list[str] = []
lines.append("# SIMUT Versa — language pack pt-BR")
lines.append("# Gerado por tools/build_lang_pack.py (Etapa 3)")
lines.append("# Encoding: UTF-8. UI/CLI sofrem unaccent() em runtime.")
lines.append("")
lines.append("@NAME Portugues")
lines.append("@CODE pt-BR")
lines.append("")
lines.append("@DICT")
for k, s in zip(lang_keys, dict_pt):
    lines.append(apply_accents(s))
lines.append("")
lines.append("@LOGCODES")
for name, txt in pt_cases:
    if name not in logcode_map:
        # Pula códigos não mapeados (ex: ERR_UNKNOWN se não no enum)
        continue
    lines.append(f"{logcode_map[name]} {apply_accents(txt)}")
lines.append("")
lines.append("@TRL")
for en, pt in trl_pairs:
    h = fnv1a32(en)
    lines.append(f"{h:08x} {apply_accents(pt)}")
lines.append("")
lines.append("@HELP")
lines.append(apply_accents(help_pt.rstrip("\n")))
lines.append("")
lines.append("@LICENSE")
lines.append(apply_accents(license_pt.rstrip("\n")))
lines.append("")
lines.append("@WEBDICT")
# Web consome UTF-8 direto; sem unaccent. Já vem com acentos da WebUI.h.
lines.append(json.dumps(webdict, ensure_ascii=False, separators=(",", ":")))

content = "\n".join(lines) + "\n"
OUT.write_text(content, encoding="utf-8")
print(f"\n✓ Escrito: {OUT}")
print(f"  Tamanho: {len(content.encode('utf-8'))} bytes ({len(content.encode('utf-8'))/1024:.1f} KB)")
