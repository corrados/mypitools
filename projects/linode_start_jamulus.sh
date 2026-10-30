#!/bin/bash

set -e

# ==========================================
# KONFIGURATION
# ==========================================
REGION="eu-central"                 # Region Frankfurt (niedrige Latenz)
TYPE="g6-nanode-1"                  # Kleinster Tarif (1 vCPU, 1 GB RAM - $0.0075/Std)
IMAGE="linode/ubuntu24.04"          # Betriebssystem Ubuntu 24.04 LTS
LABEL="jamulus-server-$$"           # Eindeutiger Name für die Linode (mit Prozess-ID)
JAMULUS_VERSION="r3_12_5"           # Gewünschte Jamulus-Version (Git Tag/Branch)
SERVER_INFO="Linode Server;FFM;82"  # Name und Standort im Central Server
CENTRAL_SERVER="jamulus.fischvolk.de:11214" # Adresse des Central Servers
ROOT_PASS="$(openssl rand -base64 16)!" # Zufälliges Root-Passwort generieren
# ==========================================

# Prüfen, ob sshpass installiert ist
if ! command -v sshpass &> /dev/null; then
    echo "❌ Fehler: 'sshpass' ist nicht installiert."
    echo "Bitte installieren mit: sudo apt-get install sshpass"
    exit 1
fi

# 1. Lokale Python Virtual Environment (.venv) im Skript-Verzeichnis einrichten
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"

if [ ! -d "$VENV_DIR" ]; then
    echo "📦 Erstelle lokale Python Virtual Environment (.venv)..."
    python3 -m venv "$VENV_DIR"
    "$VENV_DIR/bin/pip" install --upgrade pip > /dev/null
    "$VENV_DIR/bin/pip" install linode-cli > /dev/null
    echo "⚙️ Konfiguriere linode-cli (bitte Zugangsdaten eingeben):"
    "$VENV_DIR/bin/linode-cli" configure
fi

# Variable für den linode-cli Pfad in der venv
LINODE_CMD="$VENV_DIR/bin/linode-cli"

echo "🚀 Erstelle neue Linode ($LABEL in $REGION)..."

# 2. Linode über die CLI erstellen und JSON-Antwort parsen
CREATE_OUTPUT=$($LINODE_CMD linodes create \
  --region "$REGION" \
  --type "$TYPE" \
  --image "$IMAGE" \
  --label "$LABEL" \
  --root_pass "$ROOT_PASS" \
  --no-defaults \
  --json)

# ID und IP-Adresse aus der JSON-Antwort extrahieren
LINODE_ID=$(echo "$CREATE_OUTPUT" | grep -o '"id": [0-9]*' | head -1 | awk '{print $2}')
IP_ADDRESS=$(echo "$CREATE_OUTPUT" | grep -o '"ipv4": \["[^"]*' | head -1 | cut -d'"' -f4)

if [ -z "$LINODE_ID" ] || [ -z "$IP_ADDRESS" ]; then
    echo "❌ Fehler beim Erstellen der Linode."
    exit 1
fi

echo "✅ Linode erstellt! (ID: $LINODE_ID | IP: $IP_ADDRESS)"

# WICHTIG: Cleanup-Funktion - Löscht den Server garantiert bei Skript-Ende oder Abbruch (Strg+C)
cleanup() {
    echo ""
    echo "🗑️  Lösche Linode (ID: $LINODE_ID)..."
    $LINODE_CMD linodes delete "$LINODE_ID"
    echo "✅ Linode gelöscht. Keine weiteren Kosten!"
}
trap cleanup EXIT

# 3. Warten, bis der Server gebootet hat und per SSH erreichbar ist
echo "⏳ Warte darauf, dass der Server bootet und SSH bereit ist..."
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectTimeout=5"

until sshpass -p "$ROOT_PASS" ssh $SSH_OPTS root@"$IP_ADDRESS" "echo ready" &>/dev/null; do
    sleep 3
    echo -n "."
done
echo ""
echo "✅ SSH-Verbindung hergestellt!"

# 4. Jamulus auf dem Remote-Server installieren und im Hintergrund starten
echo "🛠️  Installiere Abhängigkeiten und kompiliere Jamulus Version $JAMULUS_VERSION..."

sshpass -p "$ROOT_PASS" ssh $SSH_OPTS root@"$IP_ADDRESS" bash -c "'
export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y git build-essential libjack-jackd2-dev qtbase5-dev qttools5-dev-tools qtmultimedia5-dev

git clone https://github.com/jamulussoftware/jamulus.git
cd jamulus
git checkout $JAMULUS_VERSION
qmake \"CONFIG+=nojsonrpc headless serveronly\"
make

echo \"🎵 Starte Jamulus Server mit hoher Priorität...\"
/usr/bin/ionice -c1 /usr/bin/nice -n -20 ./Jamulus -s -n -F -u 30 -e \"$CENTRAL_SERVER\" -o \"$SERVER_INFO\" > /dev/null 2>&1 &
'"

echo ""
echo "======================================================="
echo "🎉 Jamulus Server ($JAMULUS_VERSION) läuft auf IP: $IP_ADDRESS"
echo "======================================================="
echo ""
read -p "Press [ENTER] um die Session zu beenden und den Server zu LÖSCHEN..."
