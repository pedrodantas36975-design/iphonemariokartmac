# 🏎️ Mario Kart 8 Deluxe no Mac (Apple Silicon) + Controles Wi-Fi para iPhone

Este projeto permite converter e rodar **Mario Kart 8 Deluxe** de forma nativa no macOS (Macs M1, M2, M3, M4 ou superiores) com suporte a até 4 controles remotos pelo navegador do iPhone ou Android via Wi-Fi!

---

## ⚠️ AVISO IMPORTANTE: Regras para o GitHub (DMCA e Copyright)

Se você for subir este projeto no GitHub:

- ✅ **PODE SUBIR:**
  - Todo o código-fonte deste repositório (`nxnative`, `build_mariokart.sh`, scripts, HTML/CSS dos controles).
  - É código aberto legítimo de engenharia reversa e tradução de shaders.

- ❌ **NUNCA SUBA NO GITHUB:**
  - O arquivo `prod.keys` ou `title.keys`.
  - Arquivos de jogos (`.nsz`, `.nsp`, `.xci`).
  - O aplicativo compilado `Mario Kart 8 Deluxe.app` com os arquivos de áudio/gráficos da Nintendo.
  - *Motivo:* A Nintendo possui rastreadores automáticos no GitHub. Qualquer repositório contendo chaves criptográficas ou ROMs é derrubado imediatamente por DMCA e a conta do GitHub é suspensa.

> 💡 O `.gitignore` deste projeto já está configurado para **bloquear automaticamente** o envio acidental de ROMs e chaves.

---

## 📋 Pré-requisitos

1. **Um Mac com processador Apple Silicon** (M1, M2, M3, M4 ou mais recente).
2. **Arquivos extraídos do seu próprio Nintendo Switch:**
   - `prod.keys` (extraído via Lockpick-RCM ou DBI).
   - Jogo base: **Mario Kart 8 Deluxe** (`[0100152000022000].nsz` ou `.nsp`).
   - Atualização (Update): recomendada a versão `v1507328` / 3.0.3 (`[0100152000022800].nsz` ou `.nsp`).
   - *(Opcional)* Booster Course Pass DLC (`[0100152000023001].nsp`).

---

## 🚀 Como Instalar e Compilar com 1 Comando

1. Abra o **Terminal** do macOS.
2. Navegue até a pasta deste repositório:
   ```bash
   cd ~/nxnative
   ```
3. Execute o instalador automatizado:
   ```bash
   ./build_mariokart.sh
   ```
4. O script verificará automaticamente as dependências (.NET, Python, nsz, Xcode), pedirá para você arrastar a pasta onde colocou os arquivos do jogo e fará todo o processo sozinho!

### O que o script faz por você automaticamente:
- Converte os arquivos NSZ para NSP e extrai os recursos.
- Traduz todos os shaders de vídeo para Apple Metal nativo.
- Compila o executável do jogo e o servidor de controles Web integrados.
- Cria 3 atalhos prontos na sua **Mesa (Desktop)**:
  - 🎮 **`Mario Kart 8 Deluxe.app`**: Inicia o jogo em tela cheia com áudio e 60 FPS.
  - 📱 **`Controles Mario Kart.app`**: Abre a tela com QR Code para os iPhones conectarem.
  - 🛑 **`Encerrar Jogo.app`**: Fecha o jogo e o servidor com 1 clique.
- **Limpeza automática de disco:** Apaga a pasta de extração temporária (~29 GB) após a montagem do app final, economizando seu SSD.

---

## 🎮 Como Usar os Controles no iPhone (1 a 4 Jogadores)

Não é necessário instalar nenhum aplicativo da App Store no iPhone. O controle funciona diretamente no Safari!

### Opção 1: Em casa (na mesma rede Wi-Fi)
1. Conecte o Mac e os iPhones na mesma rede Wi-Fi.
2. Inicie o jogo pelo atalho **`Mario Kart 8 Deluxe.app`** na Mesa.
3. Abra o **`Controles Mario Kart.app`** na Mesa e aponte a câmera dos iPhones para o QR Code na tela.
4. O controle abrirá no Safari:
   - Cada jogador seleciona seu slot (**P1**, **P2**, **P3**, **P4**).
   - O visual do controle muda de cor automaticamente para cada jogador (Vermelho, Azul, Roxo e Verde).
   - Toque em **⚡ Ergonômico** para o layout moderno onde os botões de item e drift ficam juntos na mão direita e o polegar esquerdo pilota livremente!

### Opção 2: Jogando fora de casa / Sem Wi-Fi fixo (100% Offline)
Se estiver na faculdade, em uma viagem ou sem roteador por perto:
1. No seu iPhone, vá em **Ajustes > Acesso Pessoal (Hotspot)** e ative **"Permitir Outros"**.
2. Conecte o Mac e os iPhones dos seus amigos a essa rede Wi-Fi do seu iPhone.
3. Abra o jogo no Mac e os controles nos celulares via QR Code.
4. *Observação:* Todo o tráfego dos controles é estritamente local entre os celulares e o Mac, sem gastar seus dados móveis!
