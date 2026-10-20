#!/bin/bash

set -e

# ==========================================
# KONFIGURATION
# ==========================================
# Region: eu-central (Frankfurt) - ideal für niedrige Latenz in DE
REGION="eu-central"
# Typ: g6-nanode-1 (1 vCPU, 1 GB RAM - $0.0075/Std)
TYPE="g6-nanode-1"
# Image: Ubuntu 24.04 LTS
IMAGE="linode/ubuntu24.04"
# Name/Label für die Linode
LABEL="jamulus-server-$$"
# Jamulus Server Name / Serverinfo
SERVER_INFO="Linode Server;FFM;82"
# Central Server URL
CENTRAL_SERVER="jamulus.fischvolk.de:11214"
# Root-Passwort temporär festlegen (wird für die Erstellung benötigt)
ROOT_PASS="$(openssl rand -base64 16)!"
# SSH Key ID oder Pfad (Standardmäßig wird der bei Linode hinterlegte Key genutzt)
# ==========================================

# 1. Prüfen, ob linode-cli installiert ist
if ! command -v linode-cli &> /dev/null; then
    echo "❌ Fehler: 'linode-cli' ist nicht installiert."
    echo "Installiere es mit: pip install linode-cli && linode-cli configure"
    exit 1
fi

echo "🚀 Erstelle neue Linode ($LABEL in$REGION)..."

# 2. Linode erstellen und JSON-Antwort parsen
# HINWEIS: Es wird empfohlen, zuvor bei Linode deinen SSH-Public-Key zu hinterlegen
# (`linode-cli sshkeys list` zeigt verknüpfte Keys)
CREATE_OUTPUT=$(linode-cli linodes create \
  --region "$REGION" \
  --type "$TYPE" \
  --image "$IMAGE" \
  --label "$LABEL" \
  --root_pass "$ROOT_PASS" \
  --authorized_users "$(linode-cli profile get --json | grep -o '"username": "[^"]*' | cut -d'"' -f4)" \
  --json)

# ID und IP-Adresse extrahieren
LINODE_ID=$(echo "$CREATE_OUTPUT" | grep -o '"id": [0-9]*' | head -1 | awk '{print $2}')
IP_ADDRESS=$(echo "$CREATE_OUTPUT" | grep -o '"ipv4": \["[^"]*' | head -1 | cut -d'"' -f4)

if [ -z "$LINODE_ID" ] \vert{}\vert{} [ -z "$IP_ADDRESS" ]; then
    echo "❌ Fehler beim Erstellen der Linode."
    exit 1
fi

echo "✅ Linode erstellt! (ID: $LINODE_ID \vert{} IP:$IP_ADDRESS)"

# Sicherstellen, dass die Linode beim Beenden des Skripts (auch bei Abbruch/Fehler) gelöscht wird
cleanup() {
    echo ""
    echo "🗑️  Lösche Linode (ID: $LINODE_ID)..."
    linode-cli linodes delete "$LINODE_ID"
    echo "✅ Linode gelöscht. Keine weiteren Kosten!"
}
trap cleanup EXIT

# 3. Warten, bis SSH erreichbar ist
echo "⏳ Warte darauf, dass der Server bootet und SSH bereit ist..."
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5"

until ssh $SSH_OPTS root@"$IP_ADDRESS" "echo ready" &>/dev/null; do
    sleep 3
    echo -n "."
done
echo ""
echo "✅ SSH-Verbindung hergestellt!"

# 4. Jamulus installieren und ausführen
echo "🛠️  Installiere Abhängigkeiten und kompiliere Jamulus (kann 1-2 Minuten dauern)..."

ssh $SSH_OPTS root@"$IP_ADDRESS" bash -c "'
export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y git build-essential libjack-jackd2-dev qtbase5-dev qttools5-dev-tools qtmultimedia5-dev

git clone https://github.com/jamulussoftware/jamulus.git
cd jamulus
git checkout r3_9_0
qmake \"CONFIG+=nojsonrpc headless serveronly\"
make

echo \"🎵 Starte Jamulus Server...\"
/usr/bin/ionice -c1 /usr/bin/nice -n -20 ./Jamulus -s -n -F -u 30 -e \"$CENTRAL_SERVER\" -o \"$SERVER_INFO\" > /dev/null 2>&1 &
'"

echo ""
echo "======================================================="
echo "🎉 Jamulus Server läuft auf IP: $IP_ADDRESS"
echo "======================================================="
echo ""
read -p "Press [ENTER] um die Session zu beenden und den Server zu LÖSCHEN..."

# Beim Beenden greift automatisch die 'trap cleanup' Funktion.
