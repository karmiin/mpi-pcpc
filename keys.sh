#!/bin/bash
SSH_PASS="password"

# Percorsi per la chiave
KEY="$HOME/.ssh/id_rsa"
PUBKEY="$HOME/.ssh/id_rsa.pub"

# Verifica e genera la chiave SSH se non esiste
if [ ! -f "$KEY" ]; then
    echo "Chiave SSH non trovata. Generazione della coppia..."
    mkdir -p "$HOME/.ssh"
    ssh-keygen -t rsa -b 2048 -N "" -f "$KEY"
fi

# Copia della chiave pubblica sugli host remoti passati come argomento
for host in "$@"; do
    echo "Invio della chiave su $host..."
    sshpass -p "$SSH_PASS" ssh -o StrictHostKeyChecking=no root@"$host" "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys" < "$PUBKEY"
    if [ $? -eq 0 ]; then
        echo "Chiave trasferita con successo su $host."
    else
        echo "Errore nel trasferimento della chiave su $host."
    fi
done