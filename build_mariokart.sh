#!/usr/bin/env bash
# ==============================================================================
# 🏎️ Mario Kart 8 Deluxe - Instalador e Compilador Automatizado para Mac
# ==============================================================================
# Este script automatiza 100% do processo:
# 1. Verifica e instala dependências necessárias (Python, nsz, .NET SDK, etc.)
# 2. Compila os arquivos extraídos do Nintendo Switch (.nsz / .nsp) em um App nativo de Mac
# 3. Compila e injeta o servidor dos Controles Wi-Fi (NetPad) no executável do jogo
# 4. Gera atalhos na Mesa: "Mario Kart 8 Deluxe", "Controles Mario Kart" e "Encerrar Jogo"
# 5. Limpa automaticamente arquivos temporários de build (~29 GB) para economizar SSD
# ==============================================================================

set -e

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${BLUE}${BOLD}"
echo "=================================================================="
echo "    🏎️  MARIO KART 8 DELUXE - INSTALADOR NATIVO PARA MAC"
echo "        + Sistema de Controles Wi-Fi (iPhone / Android)           "
echo "=================================================================="
echo -e "${NC}"

# ------------------------------------------------------------------------------
# 1. VERIFICAÇÃO DE PRÉ-REQUISITOS DO MAC
# ------------------------------------------------------------------------------
echo -e "${YELLOW}🔍 [1/6] Verificando compatibilidade do sistema...${NC}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo -e "${RED}❌ Erro: Este instalador só funciona no macOS.${NC}"
    exit 1
fi

ARCH="$(uname -m)"
if [[ "$ARCH" != "arm64" ]]; then
    echo -e "${RED}❌ Erro: O jogo roda nativamente apenas em Macs com processador Apple Silicon (M1, M2, M3, M4 ou superior).${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Processador Apple Silicon detectado (${ARCH}).${NC}"

# Xcode Command Line Tools
if ! xcode-select -p &>/dev/null; then
    echo -e "${YELLOW}Instalando Ferramentas de Linha de Comando do Xcode (necessário para compilação)...${NC}"
    xcode-select --install
    echo -e "${BLUE}Por favor, aguarde o instalador do macOS concluir e execute este script novamente.${NC}"
    exit 0
fi
echo -e "${GREEN}✓ Ferramentas de compilação (clang / make) disponíveis.${NC}"

# Python 3
if ! command -v python3 &>/dev/null; then
    echo -e "${RED}❌ Erro: Python 3 não encontrado. Instale o Python 3 antes de continuar.${NC}"
    exit 1
fi

# Dependências Python (nsz para descompactar ROMs, qrcode para os controles)
echo -e "${YELLOW}Verificando bibliotecas Python (nsz, qrcode, pillow)...${NC}"
python3 -m pip install --quiet --upgrade pip 2>/dev/null || true
python3 -m pip install --quiet nsz "qrcode[pil]" pillow pycryptodome zstandard enlighten 2>/dev/null || true
echo -e "${GREEN}✓ Dependências Python prontas.${NC}"

# .NET SDK (necessário para o extrator nx_extract)
if ! command -v dotnet &>/dev/null && [ ! -f "$HOME/.dotnet/dotnet" ]; then
    echo -e "${YELLOW}Baixando e configurando .NET SDK (necessário para extração)...${NC}"
    curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 10.0
fi
if [ -f "$HOME/.dotnet/dotnet" ]; then
    export PATH="$HOME/.dotnet:$PATH"
fi
echo -e "${GREEN}✓ .NET SDK disponível.${NC}"

# ------------------------------------------------------------------------------
# 2. LOCALIZAÇÃO DOS ARQUIVOS DO NINTENDO SWITCH
# ------------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}📁 [2/6] Localizando arquivos do jogo extraídos do Switch...${NC}"

GAME_DIR="$1"
if [ -z "$GAME_DIR" ]; then
    # Tenta autodetectar em locais comuns
    if [ -d "$HOME/Downloads/Mario Kart 8 Deluxe [NSZ]" ]; then
        GAME_DIR="$HOME/Downloads/Mario Kart 8 Deluxe [NSZ]"
        echo -e "${BLUE}Pasta detectada automaticamente: ${GAME_DIR}${NC}"
    elif [ -d "$HOME/Downloads/Mario Kart 8 Deluxe" ]; then
        GAME_DIR="$HOME/Downloads/Mario Kart 8 Deluxe"
        echo -e "${BLUE}Pasta detectada automaticamente: ${GAME_DIR}${NC}"
    fi
fi

if [ -z "$GAME_DIR" ] || [ ! -d "$GAME_DIR" ]; then
    echo ""
    echo -e "${BOLD}Arraste a pasta onde estão os arquivos extraídos do seu Switch (.nsz/.nsp e prod.keys) e pressione ENTER:${NC}"
    read -r -p "Caminho da pasta: " INPUT_PATH
    GAME_DIR="${INPUT_PATH%\"}"
    GAME_DIR="${GAME_DIR#\"}"
    GAME_DIR="${GAME_DIR%\'}"
    GAME_DIR="${GAME_DIR#\'}"
    eval GAME_DIR="$GAME_DIR"
fi

if [ ! -d "$GAME_DIR" ]; then
    echo -e "${RED}❌ Pasta não encontrada: $GAME_DIR${NC}"
    exit 1
fi

# Busca prod.keys
KEYS_FILE=""
if [ -f "$GAME_DIR/prod.keys" ]; then
    KEYS_FILE="$GAME_DIR/prod.keys"
elif [ -f "$HOME/keys/prod.keys" ]; then
    KEYS_FILE="$HOME/keys/prod.keys"
elif [ -f "$HOME/.switch/prod.keys" ]; then
    KEYS_FILE="$HOME/.switch/prod.keys"
else
    # Procura qualquer .keys na pasta
    KEYS_FILE=$(find "$GAME_DIR" -maxdepth 2 -name "*prod*.keys" -o -name "prod.keys" | head -n 1)
fi

if [ -z "$KEYS_FILE" ] || [ ! -f "$KEYS_FILE" ]; then
    echo -e "${RED}❌ Arquivo 'prod.keys' não encontrado!${NC}"
    echo "O arquivo 'prod.keys' deve ser extraído do seu próprio Nintendo Switch (via Lockpick-RCM ou DBI)."
    echo "Coloque o arquivo 'prod.keys' dentro da pasta: $GAME_DIR"
    exit 1
fi
echo -e "${GREEN}✓ Chaves encontradas: ${KEYS_FILE}${NC}"

# Verifica se há ROMs do Mario Kart 8 Deluxe
ROM_COUNT=$(find "$GAME_DIR" -maxdepth 2 \( -name "*.nsz" -o -name "*.nsp" -o -name "*.xci" \) | wc -l | tr -d ' ')
if [ "$ROM_COUNT" -eq 0 ]; then
    echo -e "${RED}❌ Nenhum arquivo de jogo (.nsz / .nsp) encontrado na pasta: $GAME_DIR${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Arquivos do jogo detectados (${ROM_COUNT} arquivo(s)).${NC}"

# ------------------------------------------------------------------------------
# 3. COMPILAÇÃO DO JOGO NATIVO (.APP)
# ------------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}⚙️  [3/6] Compilando e convertendo Mario Kart 8 Deluxe para Mac App...${NC}"
echo -e "${BLUE}(Esse processo traduz os shaders, descompacta os recursos e monta o app nativo. Pode levar alguns minutos.)${NC}"

python3 scripts/build_ipa_from_nsp.py --platform mac-app "$GAME_DIR" -k "$KEYS_FILE"

APP_PATH="$GAME_DIR/Mario Kart 8 Deluxe.app"
if [ ! -d "$APP_PATH" ]; then
    # Busca caso o nome seja ligeiramente diferente
    APP_PATH=$(find "$GAME_DIR" -maxdepth 1 -name "*.app" | head -n 1)
fi

if [ ! -d "$APP_PATH" ]; then
    echo -e "${RED}❌ Falha ao encontrar o aplicativo compilado em $GAME_DIR.${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Mario Kart 8 Deluxe.app compilado com sucesso!${NC}"

# ------------------------------------------------------------------------------
# 4. COMPILAÇÃO E INJEÇÃO DOS CONTROLES WI-FI (DMHOST NETPAD)
# ------------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}🎮 [4/6] Compilando e integrando o sistema de Controles Wi-Fi (NetPad)...${NC}"

NCPU=$(sysctl -n hw.ncpu 2>/dev/null || echo 8)
make -C dm/host GAME=0100152000022000 -j"$NCPU"

cp -f "dm/host/dmhost" "$APP_PATH/Contents/MacOS/dmhost"
codesign -f -s - "$APP_PATH/Contents/MacOS/dmhost"
codesign -f -s - "$APP_PATH"
echo -e "${GREEN}✓ Servidor de Controles Wi-Fi embutido e assinado com sucesso.${NC}"

# ------------------------------------------------------------------------------
# 5. CRIAÇÃO DE ATALHOS NA MESA
# ------------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}🖥️  [5/6] Criando atalhos na Mesa...${NC}"

DESKTOP_DIR="$HOME/Desktop"
mkdir -p "$DESKTOP_DIR"

# 1. Atalho do Jogo
ln -sf "$APP_PATH" "$DESKTOP_DIR/Mario Kart 8 Deluxe.app"
echo -e "${GREEN}✓ Atalho do jogo criado na Mesa.${NC}"

# 2. App de Controles (com QR Code dinâmico)
CONTROLS_APP="$DESKTOP_DIR/Controles Mario Kart.app"
rm -rf "$CONTROLS_APP"
osacompile -o "$CONTROLS_APP" << 'EOF'
set isRunning to false
try
	do shell script "pgrep -x dmhost"
	set isRunning to true
end try

if not isRunning then
	do shell script "open -a 'Mario Kart 8 Deluxe'"
	repeat with i from 1 to 10
		delay 0.5
		try
			do shell script "lsof -i :8080 | grep LISTEN"
			exit repeat
		end try
	end repeat
end if

set wifiIP to do shell script "ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo '127.0.0.1'"
set localHost to do shell script "scutil --get LocalHostName 2>/dev/null || echo ''"

do shell script "python3 -c \"
import subprocess, os, base64, io, qrcode

ip = '" & wifiIP & "'
host = '" & localHost & "'
http_url = 'http://' + ip + ':8080'
bonjour_url = 'http://' + host + '.local:8080' if host else http_url

qr = qrcode.QRCode(box_size=10, border=2)
qr.add_data(http_url)
qr.make(fit=True)
img = qr.make_image(fill_color='#111827', back_color='#ffffff')
buf = io.BytesIO()
img.save(buf, format='PNG')
qr_b64 = base64.b64encode(buf.getvalue()).decode()

html_content = f'''<!DOCTYPE html>
<html lang=\\\"pt-BR\\\">
<head>
<meta charset=\\\"utf-8\\\">
<title>Controles Mario Kart 8 Deluxe - NetPad</title>
<style>
  body {{
    font-family: -apple-system, BlinkMacSystemFont, 'SF Pro Display', 'Segoe UI', Roboto, sans-serif;
    background: linear-gradient(135deg, #0b0f19 0%, #1a162b 100%);
    color: #f8fafc;
    margin: 0;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 24px;
    box-sizing: border-box;
  }}
  .card {{
    background: rgba(26, 32, 48, 0.9);
    backdrop-filter: blur(24px);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 28px;
    padding: 36px 40px;
    text-align: center;
    max-width: 480px;
    width: 100%;
    box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.6);
  }}
  .badge {{
    display: inline-flex;
    align-items: center;
    gap: 8px;
    background: rgba(34, 197, 94, 0.15);
    border: 1px solid rgba(34, 197, 94, 0.4);
    color: #4ade80;
    padding: 6px 14px;
    border-radius: 9999px;
    font-size: 13px;
    font-weight: 700;
    margin-bottom: 16px;
  }}
  .badge-dot {{
    width: 8px;
    height: 8px;
    background: #22c55e;
    border-radius: 50%;
    box-shadow: 0 0 10px #22c55e;
  }}
  h1 {{
    font-size: 26px;
    font-weight: 800;
    margin: 0 0 6px 0;
    background: linear-gradient(135deg, #e11d48, #fb7185, #fb923c);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
  }}
  p.subtitle {{
    color: #94a3b8;
    font-size: 14px;
    margin: 0 0 20px 0;
  }}
  .qr-box {{
    background: white;
    padding: 16px;
    border-radius: 22px;
    display: inline-block;
    box-shadow: 0 12px 30px rgba(0, 0, 0, 0.4);
    margin-bottom: 18px;
  }}
  .qr-box img {{
    display: block;
    width: 220px;
    height: 220px;
  }}
  .ip-pill {{
    background: rgba(15, 23, 42, 0.8);
    border: 1px solid rgba(56, 189, 248, 0.3);
    border-radius: 12px;
    padding: 12px 18px;
    font-size: 17px;
    font-family: ui-monospace, Menlo, Monaco, monospace;
    color: #38bdf8;
    font-weight: 700;
    margin-bottom: 8px;
    user-select: all;
  }}
  .alt-url {{
    font-size: 12px;
    color: #64748b;
    margin-bottom: 18px;
    font-family: ui-monospace, Menlo, Monaco, monospace;
  }}
  .instructions {{
    font-size: 13px;
    color: #cbd5e1;
    line-height: 1.6;
    margin-bottom: 22px;
    text-align: left;
    background: rgba(15, 23, 42, 0.5);
    padding: 14px 18px;
    border-radius: 14px;
  }}
  .instructions li {{
    margin-bottom: 4px;
  }}
  .btn {{
    display: inline-block;
    background: linear-gradient(135deg, #e11d48, #be123c);
    color: white;
    font-size: 14px;
    font-weight: 700;
    padding: 12px 24px;
    border-radius: 12px;
    text-decoration: none;
    box-shadow: 0 8px 16px -4px rgba(225, 29, 72, 0.5);
  }}
</style>
</head>
<body>
<div class=\\\"card\\\">
  <div class=\\\"badge\\\"><span class=\\\"badge-dot\\\"></span> Servidor de Controles Online</div>
  <h1>Mario Kart 8 Deluxe</h1>
  <p class=\\\"subtitle\\\">Controle Wi-Fi para iPhone e Android</p>
  
  <div class=\\\"qr-box\\\">
    <img src=\\\"data:image/png;base64,{qr_b64}\\\" alt=\\\"QR Code Controles\\\">
  </div>

  <div class=\\\"ip-pill\\\">{http_url}</div>
  <div class=\\\"alt-url\\\">iPhone / Mac: {bonjour_url}</div>

  <div class=\\\"instructions\\\">
    <ol>
      <li>Conecte os celulares na <strong>mesma rede Wi-Fi</strong> do Mac (ou no <strong>Acesso Pessoal</strong> do iPhone).</li>
      <li>Aponte a <strong>câmera</strong> do celular para o QR Code acima.</li>
      <li>Toque em <strong>⚡ Ergonômico</strong> para botões na direita e pilotagem fácil com a mão esquerda!</li>
    </ol>
  </div>

  <a class=\\\"btn\\\" href=\\\"{http_url}\\\" target=\\\"_blank\\\">Testar no Mac</a>
</div>
</body>
</html>'''

out_path = os.path.expanduser('~/Library/Application Support/nxnative/netpad/connect.html')
os.makedirs(os.path.dirname(out_path), exist_ok=True)
with open(out_path, 'w', encoding='utf-8') as f:
    f.write(html_content)
\""
do shell script "open ~/Library/Application\\ Support/nxnative/netpad/connect.html"
EOF

# Copia ícone para Controles Mario Kart se disponível
if [ -f "$APP_PATH/Contents/Resources/AppIcon.icns" ]; then
    cp "$APP_PATH/Contents/Resources/AppIcon.icns" "$CONTROLS_APP/Contents/Resources/applet.icns" 2>/dev/null || true
fi
codesign -f -s - "$CONTROLS_APP"
echo -e "${GREEN}✓ App 'Controles Mario Kart.app' criado na Mesa.${NC}"

# 3. App de Encerrar Jogo
KILL_APP="$DESKTOP_DIR/Encerrar Jogo.app"
rm -rf "$KILL_APP"
osacompile -o "$KILL_APP" << 'EOF'
try
	do shell script "killall -9 dmhost 2>/dev/null || true; killall -9 nxnative 2>/dev/null || true; pkill -9 -f 'Mario Kart 8 Deluxe' 2>/dev/null || true"
end try
display notification "Mario Kart 8 Deluxe e os controles foram encerrados." with title "Jogo Encerrado"
EOF
codesign -f -s - "$KILL_APP"
echo -e "${GREEN}✓ App 'Encerrar Jogo.app' criado na Mesa.${NC}"

# ------------------------------------------------------------------------------
# 6. LIMPEZA AUTOMÁTICA DE ARQUIVOS DE BUILD (~29 GB)
# ------------------------------------------------------------------------------
echo ""
echo -e "${YELLOW}🧹 [6/6] Verificando limpeza de arquivos temporários de build...${NC}"

BUILD_DIR="$GAME_DIR/0100152000022000_build"
if [ -d "$BUILD_DIR" ]; then
    echo -e "${BLUE}Removendo pasta de extração temporária (~29 GB) para liberar espaço no disco...${NC}"
    rm -rf "$BUILD_DIR"
    echo -e "${GREEN}✓ Pasta temporária de build removida com sucesso!${NC}"
fi

# ------------------------------------------------------------------------------
# CONCLUSÃO
# ------------------------------------------------------------------------------
echo ""
echo -e "${GREEN}${BOLD}==================================================================${NC}"
echo -e "${GREEN}${BOLD}🎉  INSTALAÇÃO CONCLUÍDA COM SUCESSO!                             ${NC}"
echo -e "${GREEN}${BOLD}==================================================================${NC}"
echo ""
echo -e "Você já pode jogar! Encontre os atalhos criados na sua Mesa:"
echo -e "  🎮 ${BOLD}Mario Kart 8 Deluxe.app${NC} : Inicia o jogo nativamente."
echo -e "  📱 ${BOLD}Controles Mario Kart.app${NC}: Abre a tela de conexão com QR Code para os celulares."
echo -e "  🛑 ${BOLD}Encerrar Jogo.app${NC}       : Fecha o jogo e o servidor de controles com 1 clique."
echo ""
echo -e "${YELLOW}Como jogar com os amigos pelo iPhone (1 a 4 jogadores):${NC}"
echo "1. Conecte o Mac e os iPhones na mesma rede Wi-Fi."
echo "   (Se estiver fora de casa sem Wi-Fi: ative o 'Acesso Pessoal' / Hotspot no seu iPhone e conecte o Mac e os amigos nele!)"
echo "2. Abra 'Controles Mario Kart.app' na Mesa e aponte a câmera dos iPhones para o QR Code."
echo "3. Pronto! Cada celular escolhe seu jogador (P1, P2, P3, P4) e joga em tela cheia."
echo ""
