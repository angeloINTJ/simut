#!/usr/bin/env python3
"""F-LANGPACK — gera data/lang/language_es-ES.lng a partir do PT-BR.

Estratégia: lê language_pt-BR.lng (já validado em HW), aplica
substituição PT→ES word-by-word + phrase-level (longest-first match
para evitar partial-match ambíguo). Acentos espanhóis incluídos.

@HELP e @LICENSE ficam omitidos — quando ES selecionado, firmware
faz fallback para /help_en.txt e /license_en.txt (EN inline).

DESATUALIZADO — LEIA ANTES DE RODAR. O data/lang/language_es-ES.lng
versionado é a FONTE DE VERDADE (validado em HW) e NÃO é reproduzível
por este script: o pack de hoje carrega @HELP/@LICENSE traduzidos para
o espanhol, que este gerador dropa. Rodar sem --force é RECUSADO na
escrita, de propósito, para não trocar tradução real por fallback EN em
silêncio. Para mexer numa string, editar o .lng à mão e conferir com
tools/check_lang_packs.py continua sendo o caminho seguro; regenerar do
zero exige --force e revalidação no ferro.
"""
import json
import re
import sys
from pathlib import Path

# Derived from the script's own location, not hardcoded. It used to read
# Path("/home/angelo/Documentos/SIMUT") — case wrong on a case-sensitive FS
# (the project is .../simut), so the generator did not run at all, which is
# part of why the checked-in pack drifted from what this script produces.
ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "data" / "lang" / "language_pt-BR.lng"
OUT = ROOT / "data" / "lang" / "language_es-ES.lng"


# ──────────────────────────────────────────────────────────────────────────
# Frases inteiras (longest-first via sorted aplicado depois) — match exato
# de strings completas tem prioridade sobre word-level abaixo.
# ──────────────────────────────────────────────────────────────────────────
PT_TO_ES_PHRASES: dict[str, str] = {
    # @DICT (TFT)
    "Painel de Controle": "Panel de Control",
    "Histórico e Logs": "Historial y Logs",
    "Alarmes e Sons": "Alarmas y Sonidos",
    "Bom dia": "Buenos días",
    "Boa tarde": "Buenas tardes",
    "Boa noite": "Buenas noches",
    "Olá": "Hola",
    "Data e Hora": "Fecha y Hora",
    "Tempo Ativo": "Tiempo Activo",
    "Sinal Wi-Fi": "Señal Wi-Fi",
    "Registros Pendentes": "Registros Pendientes",
    "Uso de RAM": "Uso de RAM",
    "Carregando sensores...": "Cargando sensores...",
    "Nenhum sensor.": "Ningún sensor.",
    "Aguardando...": "Esperando...",
    "Aguardando sinc.": "Esperando sinc.",
    "Clique para capturar": "Click para capturar",
    "Capturando...": "Capturando...",
    "Falha NTP": "Fallo NTP",
    "Conectando...": "Conectando...",
    "Avaliando...": "Evaluando...",
    "Sem Dados": "Sin Datos",
    "Sem dados.": "Sin datos.",
    "Conexão perdida.": "Conexión perdida.",
    "Eventos do Sistema": "Eventos del Sistema",
    "Filtrar...": "Filtrar...",
    "Atualizar": "Actualizar",
    "Limpar": "Borrar",
    "Carregando...": "Cargando...",
    "Carregar": "Cargar",
    "Nenhum evento.": "Ningún evento.",
    "Configurações > Principal": "Configuración > Principal",
    "Configurações > Temas": "Configuración > Temas",
    "Configurações > Idioma": "Configuración > Idioma",
    "Autenticação de Segurança": "Autenticación de Seguridad",
    "ACESSO BLOQUEADO": "ACCESO BLOQUEADO",
    "Reinicialização requerida": "Reinicio requerido",
    "Tentativas Excedidas": "Intentos Excedidos",
    "Aguarde %ld segundos...": "Espere %ld segundos...",
    "Senha Inválida!": "Contraseña Inválida!",
    "Lendo Histórico...": "Leyendo Historial...",
    "GERAR GRÁFICO": "GENERAR GRÁFICO",
    "1. Temas Visuais": "1. Temas Visuales",
    "2. Limites de Alarme": "2. Límites de Alarma",
    "3. Sons de Alarme": "3. Sonidos de Alarma",
    "4. Idioma do Sistema": "4. Idioma del Sistema",
    "Aplicando Tema...": "Aplicando Tema...",
    "Limites de Alarme": "Límites de Alarma",
    "5. Alterar Senha": "5. Cambiar Contraseña",
    "Nova Senha": "Nueva Contraseña",
    "6. Calibrar Touch": "6. Calibrar Touch",
    "Calibração do Touch": "Calibración del Touch",
    "Toque na mira": "Toque la mira",
    "Calibração Concluída!": "¡Calibración Completada!",
    "Toques imprecisos! Tente novamente.": "Toques imprecisos! Intente nuevamente.",
    "Confirmar Senha": "Confirmar Contraseña",
    "Senha muito curta! (min 4)": "Contraseña muy corta! (min 4)",
    "Senhas não coincidem!": "Contraseñas no coinciden!",
    "Senha salva!": "Contraseña guardada!",
    "ENTENDI": "ENTENDIDO",
    "Config. de Sons": "Config. de Sonidos",
    "Toque na Tela": "Toque en Pantalla",
    "Confirmação": "Confirmación",
    "Som de Erro": "Sonido de Error",
    "Som de Alarme": "Sonido de Alarma",
    "Silenciar Tudo": "Silenciar Todo",
    "Vol. Sistema": "Vol. Sistema",
    "Vol. Alarme": "Vol. Alarma",
    "Acesso Web": "Acceso Web",
    "7. Licença": "7. Licencia",
    "Licença MIT": "Licencia MIT",
    "ATIVO": "ACTIVO",
    "Silenciar 120s": "Silenciar 120s",
    "Desativar": "Desactivar",
    "Min/Max": "Min/Max",
    "Silenciado": "Silenciado",
    "7. Sensibilidade do Toque": "7. Sensibilidad del Toque",
    "Sensibilidade do Toque": "Sensibilidad del Toque",
    "Toque %d/%d": "Toque %d/%d",
    "DESVIO": "DESV.",
    "Modo de Configuração": "Modo de Configuración",
    "8. Status do Sistema": "8. Estado del Sistema",
    "Status do Sistema": "Estado del Sistema",
    "9. Alinhamento da Tela": "9. Alineación de Pantalla",
    "Alinhamento da Tela": "Alineación de Pantalla",
    "Ajuste +/-4 px. Salvar reinicia calibração do touch.": "Ajuste +/-4 px. Guardar reinicia calibración del touch.",
    # @WEBDICT — mais usadas
    "Salvar e Reiniciar": "Guardar y Reiniciar",
    "Salvar e Entrar": "Guardar e Iniciar Sesión",
    "Mostrar senha": "Mostrar contraseña",
    "Usuário ou senha incorretos.": "Usuario o contraseña incorrectos.",
    "Limite de sessões atingido. Tente mais tarde.": "Límite de sesiones alcanzado. Intente más tarde.",
    "Bloqueado por {s}s. Tentativas excessivas.": "Bloqueado por {s}s. Intentos excesivos.",
    "Sinal Wi-Fi": "Señal Wi-Fi",
    "1 Hora": "1 Hora",
    "6 Horas": "6 Horas",
    "12 Horas": "12 Horas",
    "24 Horas": "24 Horas",
    "7 Dias": "7 Días",
    "Calendário": "Calendario",
    "Selecione uma data para carregar o gráfico.": "Seleccione una fecha para cargar el gráfico.",
    "Erro.": "Error.",
    "Conexão perdida.": "Conexión perdida.",
    "Clique em 'Carregar' para exibir os logs.": "Click en 'Cargar' para mostrar los logs.",
    "Concluído": "Completado",
    "Status da Conexão": "Estado de Conexión",
    "Conectado": "Conectado",
    "Desconectado": "Desconectado",
    "Wi-Fi": "Wi-Fi",
    "IP Estático": "IP Estático",
    "DNS Primário": "DNS Primario",
    "DNS Secundário": "DNS Secundario",
    "Servidor de Hora (NTP)": "Servidor de Hora (NTP)",
    "Endereço do Servidor": "Dirección del Servidor",
    "Deixe vazio para usar o padrão (pool.ntp.org)": "Deje vacío para usar el predeterminado (pool.ntp.org)",
    "Servidor Web": "Servidor Web",
    "Porta HTTP": "Puerto HTTP",
    "Padrão: 80. Após salvar, o navegador redireciona automaticamente para a nova porta.": "Predeterminado: 80. Después de guardar, el navegador redirige automáticamente al nuevo puerto.",
    "Configuração de DNS": "Configuración de DNS",
    "Obter DNS automaticamente (DHCP)": "Obtener DNS automáticamente (DHCP)",
    "NTP está desabilitado.": "NTP está deshabilitado.",
    "Habilite em Configurações do Sistema →": "Habilite en Configuración del Sistema →",
    "Sincronizar automaticamente via NTP": "Sincronizar automáticamente vía NTP",
    "Aplicar Agora": "Aplicar Ahora",
    "Usa fuso horário do dispositivo. Salve e Reinicie para persistir o toggle do NTP.": "Usa zona horaria del dispositivo. Guarde y Reinicie para persistir el toggle del NTP.",
    "Preencha data e hora.": "Complete fecha y hora.",
    "Hora aplicada.": "Hora aplicada.",
    "Falhou ao aplicar.": "Fallo al aplicar.",
    "Defina 0 para desabilitar a telemetria. Mínimo recomendado: 10000 (10s).": "Defina 0 para deshabilitar la telemetría. Mínimo recomendado: 10000 (10s).",
    "⚠ Telemetria desabilitada (Intervalo de Upload = 0). Defina um valor para ativar.": "⚠ Telemetría deshabilitada (Intervalo de Upload = 0). Defina un valor para activar.",
    "Display em uso. Tente novamente em alguns segundos.": "Pantalla en uso. Intente nuevamente en unos segundos.",
    "Erro de conexão.": "Error de conexión.",
    "Falha ao limpar logs.": "Fallo al borrar logs.",
    "Logs limpos.": "Logs borrados.",
    "Pasta criada.": "Carpeta creada.",
    "Falha ao criar pasta.": "Fallo al crear carpeta.",
    "Alguns arquivos não puderam ser excluídos.": "Algunos archivos no pudieron ser eliminados.",
    "Arquivos excluídos.": "Archivos eliminados.",
    "💾 Salvar e Reiniciar": "💾 Guardar y Reiniciar",
    "Isto salvará todas as alterações e reiniciará o sistema.\n\n⚠️ O dispositivo ficará offline por ~10 segundos.\nQualquer gravação de histórico/log em andamento será interrompida.\n\nContinuar?":
        "Esto guardará todos los cambios y reiniciará el sistema.\n\n⚠️ El dispositivo quedará offline por ~10 segundos.\nCualquier grabación de historial/log en curso será interrumpida.\n\n¿Continuar?",
    "Salvo! Reiniciando sistema...": "¡Guardado! Reiniciando sistema...",
    "Falha ao salvar.": "Fallo al guardar.",
    'Clique em "Salvar e Reiniciar" no topo para aplicar as alterações.': 'Click en "Guardar y Reiniciar" arriba para aplicar los cambios.',
    "Gestão de Acessos": "Gestión de Accesos",
    "Permissões": "Permisos",
    "Ações": "Acciones",
    "Adicionar": "Agregar",
    "Painel": "Panel",
    "Histórico": "Historial",
    "Logs": "Logs",
    "Sistema": "Sistema",
    "Rede": "Red",
    "Leitura": "Lectura",
    "Upload": "Upload",
    "Excluir": "Eliminar",
    "Usuários": "Usuarios",
    "Criar": "Crear",
    "Uma senha de uso único aparece após Salvar e Reiniciar. Copie — ela é mostrada só uma vez, e o usuário deve trocá-la no 1º login.": "Se muestra una contraseña de un solo uso tras Guardar y Reiniciar. Cópiela — solo se muestra una vez, y el usuario debe cambiarla en el primer inicio.",
    "Protegido": "Protegido",
    "Reset": "Reset",
    "Super": "Super",
    "Sistema de Arquivos": "Sistema de Archivos",
    "Baixar": "Descargar",
    "Enviar": "Enviar",
    "Tamanho": "Tamaño",
    "Nova Pasta": "Nueva Carpeta",
    "nome": "nombre",
    "Cancelar": "Cancelar",
    "Subir": "Subir",
    "Pasta": "Carpeta",
    "Vazio": "Vacío",
    "Inválido": "Inválido",
    "Selecione": "Seleccione",
    "Excluir N?": "¿Eliminar N?",
    "Baixar N?": "¿Descargar N?",
    "Excluir?": "¿Eliminar?",
    "Forçar reset?": "¿Forzar reset?",
    "Limpar logs?": "¿Borrar logs?",
    "Configuração de Sons": "Configuración de Sonidos",
    "Temp. Mín.": "Temp. Mín.",
    "Temp. Máx.": "Temp. Máx.",
    "Umid. Mín.": "Hum. Mín.",
    "Umid. Máx.": "Hum. Máx.",
    "Alarme Ativo": "Alarma Activa",
    "Salvo com sucesso!": "Guardado con éxito!",
    "Erro ao salvar.": "Error al guardar.",
    "Nenhum sensor configurado.": "Ningún sensor configurado.",
    "Toque": "Toque",
    "Erro": "Error",
    "Alarme": "Alarma",
    "Sons Web": "Sonidos Web",
    "Mudo Global": "Silencio Global",
    "Ligado": "Encendido",
    "Desligado": "Apagado",
    "Sensor Ambiente": "Sensor Ambiente",
    "Ascendente": "Ascendente",
    "Descendente": "Descendente",
    "Sirene": "Sirena",
    "📜 Licença": "📜 Licencia",
    "Licença de Software": "Licencia de Software",
    "Avisos de Terceiros": "Avisos de Terceros",
}


# ──────────────────────────────────────────────────────────────────────────
# Word-level — match com word boundaries. Aplicado depois das frases.
# ──────────────────────────────────────────────────────────────────────────
PT_TO_ES_WORDS: dict[str, str] = {
    # Verbos
    "Carregar": "Cargar",
    "Carregando": "Cargando",
    "Salvar": "Guardar",
    "Salvo": "Guardado",
    "Salva": "Guardada",
    "Apagar": "Borrar",
    "Apagado": "Borrado",
    "Apagou": "Borró",
    "Excluir": "Eliminar",
    "Excluídos": "Eliminados",
    "Sair": "Salir",
    "SAIR": "SALIR",
    "Voltar": "Volver",
    "Umidade": "Humedad", "umidade": "humedad", "UMIDADE": "HUMEDAD",
    "Umid.": "Hum.",
    "baixa": "baja", "Baixa": "Baja",
    "Varredura": "Escaneo",
    "obtido": "obtenido", "Obtido": "Obtenido",
    "rotacionado": "rotado",
    "Marco": "Marca",
    "uptime": "uptime",
    "storage": "almacenamiento", "Storage": "Almacenamiento",
    # NOTA: "no" PT (contração "em o") não traduzido para "en" — colide com
    # "no" ES (negação). Strings ficam ambíguas mas legíveis em contexto.
    "Atualizar": "Actualizar",
    "Limpar": "Borrar",
    "Limpos": "Borrados",
    "Aguardar": "Esperar",
    "Aguarde": "Espere",
    "Aguardando": "Esperando",
    "Procurando": "Buscando",
    "Selecionar": "Seleccionar",
    "Selecione": "Seleccione",
    "Resetar": "Restablecer",
    "Resetada": "Restablecida",
    "Resetado": "Restablecido",
    "Resetou": "Restableció",
    "Trocar": "Cambiar",
    "Alterar": "Cambiar",
    "Alteração": "Cambio",
    "Alterações": "Cambios",
    "Aplicar": "Aplicar",
    "Aplicada": "Aplicada",
    "Aplicado": "Aplicado",
    "Aplicando": "Aplicando",
    "Conectar": "Conectar",
    "Conectando": "Conectando",
    "Conectado": "Conectado",
    "Desconectado": "Desconectado",
    "Reiniciar": "Reiniciar",
    "Reiniciando": "Reiniciando",
    "Continuar": "Continuar",
    "Cancelado": "Cancelado",
    "Cancelar": "Cancelar",
    # Substantivos
    "Senha": "Contraseña",
    "Senhas": "Contraseñas",
    "Usuário": "Usuario",
    "Usuários": "Usuarios",
    "Sessão": "Sesión",
    "Sessões": "Sesiones",
    "Sessão expirada": "Sesión expirada",
    "Configuração": "Configuración",
    "Configurações": "Configuración",
    "Calibração": "Calibración",
    "Reinicialização": "Reinicio",
    "Autenticação": "Autenticación",
    "Confirmação": "Confirmación",
    "Sincronização": "Sincronización",
    "Atualização": "Actualización",
    "Operação": "Operación",
    "Migração": "Migración",
    "Conexão": "Conexión",
    "Conexões": "Conexiones",
    "Versão": "Versión",
    "Idioma": "Idioma",
    "Histórico": "Historial",
    "Gráfico": "Gráfico",
    "Gráficos": "Gráficos",
    "Memória": "Memoria",
    "Sistema": "Sistema",
    "Sensor": "Sensor",
    "Sensores": "Sensores",
    "Tela": "Pantalla",
    "Telas": "Pantallas",
    "Display": "Pantalla",
    "Tema": "Tema",
    "Temas": "Temas",
    "Volume": "Volumen",
    "Som": "Sonido",
    "Sons": "Sonidos",
    "Alarme": "Alarma",
    "Alarmes": "Alarmas",
    "Pasta": "Carpeta",
    "Pastas": "Carpetas",
    "Arquivo": "Archivo",
    "Arquivos": "Archivos",
    "Endereço": "Dirección",
    "Endereços": "Direcciones",
    "Rede": "Red",
    "Falha": "Fallo",
    "Falhou": "Falló",
    "Erro": "Error",
    "Erros": "Errores",
    "Eventos": "Eventos",
    "Tempo": "Tiempo",
    "Hora": "Hora",
    "Data": "Fecha",
    "Página": "Página",
    "Páginas": "Páginas",
    "Permissão": "Permiso",
    "Permissões": "Permisos",
    "Tentativas": "Intentos",
    "Limite": "Límite",
    "Limites": "Límites",
    "Status": "Estado",
    "Modo": "Modo",
    "Padrão": "Predeterminado",
    "Tentativas Excedidas": "Intentos Excedidos",
    "Acessos": "Accesos",
    "Acesso": "Acceso",
    "Histórico": "Historial",
    "Calendário": "Calendario",
    "Bem-vindo": "Bienvenido",
    "Boa": "Buena",
    "Bom": "Buen",
    "Lendo": "Leyendo",
    "Excessivas": "Excesivas",
    "Bloqueado": "Bloqueado",
    "Críticas": "Críticas",
    "Crítico": "Crítico",
    "Crítica": "Crítica",
    "Necessária": "Necesaria",
    "Necessário": "Necesario",
    "Inválido": "Inválido",
    "Inválida": "Inválida",
    "Concluído": "Completado",
    "Concluída": "Completada",
    "Recuperada": "Recuperada",
    "Migrada": "Migrada",
    "Pendente": "Pendiente",
    "Pendentes": "Pendientes",
    "Imprecisos": "Imprecisos",
    "Reset": "Reset",
    "Reboot": "Reboot",
    "Login": "Login",
    "Logout": "Logout",
    # Particles
    "Não": "No",
    "Sim": "Sí",
    "até": "hasta",
    "após": "después",
    "também": "también",
    "já": "ya",
    "só": "solo",
    "para": "para",
    "pelo": "por",
    "pela": "por",
    "do": "del",
    "da": "de la",
    "dos": "de los",
    "das": "de las",
    "com": "con",
    "sem": "sin",
    "ou": "o",
    "ao": "al",
    "aos": "a los",
    "minutos": "minutos",
    "segundo": "segundo",
    "segundos": "segundos",
    # Months / weekdays já em ES (alguns iguais)
    "Dom": "Dom",
    "Seg": "Lun",
    "Ter": "Mar",
    "Qua": "Mié",
    "Qui": "Jue",
    "Sex": "Vie",
    "Sáb": "Sáb",
    "Jan": "Ene",
    "Fev": "Feb",
    "Mar": "Mar",
    "Abr": "Abr",
    "Mai": "May",
    "Jun": "Jun",
    "Jul": "Jul",
    "Ago": "Ago",
    "Set": "Sep",
    "Out": "Oct",
    "Nov": "Nov",
    "Dez": "Dic",
}


def apply_translations(s: str) -> str:
    """Aplica frase-level (longest-first), depois word-level (word boundary)."""
    if not s:
        return s
    # 1) Frases inteiras — longest-first para evitar prefix-match
    for k in sorted(PT_TO_ES_PHRASES.keys(), key=len, reverse=True):
        if k in s:
            s = s.replace(k, PT_TO_ES_PHRASES[k])
    # 2) Word-level com boundaries
    for k in sorted(PT_TO_ES_WORDS.keys(), key=len, reverse=True):
        v = PT_TO_ES_WORDS[k]
        if k == v:
            continue
        # Word boundaries usam \b. Para palavras com chars não-ASCII (acentos),
        # \b pode falhar; usamos lookahead/lookbehind manuais:
        pattern = r"(?<![A-Za-zÀ-ÿ])" + re.escape(k) + r"(?![A-Za-zÀ-ÿ])"
        s = re.sub(pattern, lambda _: v, s)
    return s


# ──────────────────────────────────────────────────────────────────────────
# Parser do .lng — extrai cada seção como blob para retransformação
# ──────────────────────────────────────────────────────────────────────────
def parse_lng(text: str) -> dict[str, str]:
    """Retorna {sec_name: body} para cada @SEC encontrada."""
    sections: dict[str, str] = {}
    cur = None
    body_lines: list[str] = []
    for line in text.split("\n"):
        if line.startswith("@") and re.match(r"^@\w+", line):
            # Fecha seção anterior
            if cur is not None:
                sections[cur] = "\n".join(body_lines).rstrip("\n")
            # Parse novo header
            parts = line[1:].split(None, 1)
            cur = parts[0]
            # NAME e CODE têm valor inline, sem corpo
            if cur in ("NAME", "CODE") and len(parts) > 1:
                sections[cur] = parts[1].strip()
                cur = None
                body_lines = []
            else:
                body_lines = []
        elif cur is not None:
            body_lines.append(line)
    if cur is not None:
        sections[cur] = "\n".join(body_lines).rstrip("\n")
    return sections


# ──────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────
print(f"== F-LANGPACK: gerando {OUT.name} a partir de {SRC.name}")

if not SRC.exists():
    sys.exit(f"ERRO: {SRC} não existe — rode tools/build_lang_pack.py primeiro")

src_text = SRC.read_text(encoding="utf-8")
sections = parse_lng(src_text)

# Traduz cada seção
out_lines: list[str] = []
out_lines.append("# SIMUT Versa — language pack es-ES (gerado por build_lang_pack_es.py)")
out_lines.append("# Source: language_pt-BR.lng + PT->ES dict (best-effort).")
out_lines.append("# @HELP e @LICENSE omitidos — fallback EN no firmware.")
out_lines.append("")
out_lines.append("@NAME Espanol")
out_lines.append("@CODE es-ES")

# @DICT — uma linha por LangKey
if "DICT" in sections:
    out_lines.append("")
    out_lines.append("@DICT")
    for line in sections["DICT"].split("\n"):
        out_lines.append(apply_translations(line))

# @LOGCODES — formato "<id> <texto>"
if "LOGCODES" in sections:
    out_lines.append("")
    out_lines.append("@LOGCODES")
    for line in sections["LOGCODES"].split("\n"):
        if " " in line:
            code, txt = line.split(" ", 1)
            out_lines.append(f"{code} {apply_translations(txt)}")
        else:
            out_lines.append(line)

# @TRL — formato "<hex_hash> <texto>"
if "TRL" in sections:
    out_lines.append("")
    out_lines.append("@TRL")
    for line in sections["TRL"].split("\n"):
        if " " in line:
            h, txt = line.split(" ", 1)
            out_lines.append(f"{h} {apply_translations(txt)}")
        else:
            out_lines.append(line)

# @WEBDICT — JSON blob; aplica tradução em cada value
if "WEBDICT" in sections:
    out_lines.append("")
    out_lines.append("@WEBDICT")
    body = sections["WEBDICT"].strip()
    if body.startswith("{"):
        d = json.loads(body)
        for k in d:
            d[k] = apply_translations(d[k])
        out_lines.append(json.dumps(d, ensure_ascii=False, separators=(",", ":")))
    else:
        out_lines.append(body)

# @HELP / @LICENSE intencionalmente omitidos.

content = "\n".join(out_lines) + "\n"

# The checked-in language_es-ES.lng is the source of truth: it was validated on
# hardware and it carries @HELP/@LICENSE translated into Spanish, which THIS
# script omits (see the note above). Regenerating would therefore overwrite
# real translations with an English fallback — silently, since the file just
# gets smaller. Refuse to clobber an existing pack unless asked, and say why.
if OUT.exists() and "--force" not in sys.argv:
    if OUT.read_text(encoding="utf-8") != content:
        on_disk = OUT.stat().st_size
        gen = len(content.encode("utf-8"))
        sys.stderr.write(
            f"[build_lang_pack_es] {OUT.name} already exists and differs from what\n"
            f"  this script would produce ({on_disk} B on disk vs {gen} B generated).\n"
            f"  The checked-in pack is the HW-validated source of truth and carries\n"
            f"  Spanish @HELP/@LICENSE this generator drops. Refusing to overwrite.\n"
            f"  Re-run with --force only if you mean to regenerate from scratch and\n"
            f"  re-validate on hardware.\n")
        sys.exit(1)
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(content, encoding="utf-8")
print(f"[build_lang_pack_es] wrote {OUT} ({len(content)} B)")
size = len(content.encode("utf-8"))
print(f"\n✓ Escrito: {OUT}")
print(f"  Tamanho: {size} bytes ({size/1024:.1f} KB)")
print(f"  Frases traduzidas: {len(PT_TO_ES_PHRASES)}")
print(f"  Palavras traduzidas: {len(PT_TO_ES_WORDS)}")
print()
print("⚠ Tradução é best-effort. Strings sem entrada no dict ficam em PT.")
print("  Refine editando PT_TO_ES_PHRASES/WORDS aqui ou o .lng à mão.")
