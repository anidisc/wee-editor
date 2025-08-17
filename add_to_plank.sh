#!/bin/bash

# Script per aggiungere programmi a Plank dock
# Colori per output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PLANK_DIR="$HOME/.config/plank/dock1/launchers"
APPS_DIR="/usr/share/applications"


# Verifica se plank è installato
if ! command -v plank &> /dev/null; then
    echo -e "${RED}Errore: Plank non è installato!${NC}"
    exit 1
fi

# Verifica se la directory di plank esiste
if [ ! -d "$PLANK_DIR" ]; then
    echo -e "${RED}Errore: Directory Plank non trovata: $PLANK_DIR${NC}"
    exit 1
fi

# Funzione per riavviare plank
restart_plank() {
    echo -e "${YELLOW}Riavvio Plank...${NC}"
    
    # Prova a killare tutti i processi plank dell'utente corrente
    pkill -u "$USER" plank 2>/dev/null || killall plank 2>/dev/null
    
    # Aspetta che tutti i processi plank si chiudano
    local count=0
    while pgrep -u "$USER" plank >/dev/null && [ $count -lt 10 ]; do
        sleep 0.5
        ((count++))
    done
    
    if pgrep -u "$USER" plank >/dev/null; then
        echo -e "${RED}Attenzione: Non riesco a chiudere completamente Plank${NC}"
        echo -e "${YELLOW}Prova a chiudere Plank manualmente e rilanciarlo${NC}"
        return 1
    fi
    
    # Riavvia plank
    sleep 1
    nohup plank >/dev/null 2>&1 &
    disown
    
    # Verifica che plank sia partito
    sleep 2
    if pgrep -u "$USER" plank >/dev/null; then
        echo -e "${GREEN}✓ Plank riavviato correttamente${NC}"
    else
        echo -e "${RED}Errore: Plank non si è riavviato${NC}"
        echo -e "${YELLOW}Riavvialo manualmente con: plank &${NC}"
    fi
}

# Funzione per aggiungere app a plank
add_to_plank() {
    local desktop_file="$1"
    local app_name="$2"
    local dockitem_name="${desktop_file%.desktop}.dockitem"
    local dockitem_path="$PLANK_DIR/$dockitem_name"
    
    if [ -f "$dockitem_path" ]; then
        echo -e "${YELLOW}  → $app_name è già nella dock${NC}"
        return 1
    fi
    
    # Ferma Plank temporaneamente per evitare che cancelli il file
    local plank_was_running=false
    if pgrep -u "$USER" plank >/dev/null; then
        plank_was_running=true
        # Usa pkill con nome specifico per evitare di killare lo script
        pkill -f "^plank$" 2>/dev/null || pkill -x plank 2>/dev/null
        # Aspetta che plank si chiuda
        local count=0
        while pgrep -u "$USER" plank >/dev/null && [ $count -lt 5 ]; do
            sleep 0.2
            ((count++))
        done
    fi
    
    # Crea il file
    cat > "$dockitem_path" << EOF
[PlankDockItemPreferences]
Launcher=file://$APPS_DIR/$desktop_file
EOF
    
    # Riavvia Plank se era in esecuzione
    if [ "$plank_was_running" = true ]; then
        sleep 0.5
        nohup plank >/dev/null 2>&1 &
        disown
    fi
    
    echo -e "${GREEN}  ✓ $app_name aggiunto alla dock${NC}"
    return 0
}

# Funzione per mostrare app già in plank
show_current_apps() {
    echo -e "${BLUE}App attualmente nella dock:${NC}"
    if [ -z "$(ls -A "$PLANK_DIR")" ]; then
        echo "  Nessuna app nella dock"
    else
        for dockitem in "$PLANK_DIR"/*.dockitem; do
            if [ -f "$dockitem" ]; then
            # Estrae il nome del file .desktop dal dockitem
                launcher_path=$(grep "Launcher=" "$dockitem" | cut -d'=' -f2 | sed 's/file:\/\///')
                desktop_file=$(basename "$launcher_path")
                if [ -f "$launcher_path" ]; then
                    app_name=$(grep "^Name=" "$launcher_path" | head -1 | cut -d'=' -f2)
                    echo "  • $app_name"
                fi
            fi
        done
    fi
    echo
}

# Funzione per cercare app per nome
search_apps() {
    echo -e "${BLUE}Cerca un'applicazione (o premi ENTER per vedere tutte):${NC}"
    read -p "> " search_term
    echo
    
    if [ -z "$search_term" ]; then
        pattern="*"
    else
        pattern="*${search_term,,}*"  # Converte in minuscolo per ricerca case-insensitive
    fi
    
    # Array per memorizzare le app trovate
    declare -a found_apps=()
    declare -a found_files=()
    
    echo -e "${YELLOW}Scansione applicazioni...${NC}"
    
    for desktop_file in "$APPS_DIR"/*.desktop; do
        if [ -f "$desktop_file" ]; then
            # Legge il nome dell'app
            app_name=$(grep "^Name=" "$desktop_file" 2>/dev/null | head -1 | cut -d'=' -f2)
            desktop_filename=$(basename "$desktop_file")
            
            # Verifica se nascosta o di sistema
            if grep -q "^NoDisplay=true" "$desktop_file" 2>/dev/null; then
                continue
            fi
            
            # Filtra per termine di ricerca
            if [[ "${app_name,,}" == $pattern || "${desktop_filename,,}" == $pattern ]]; then
                found_apps+=("$app_name")
                found_files+=("$desktop_filename")
            fi
        fi
    done
    
    if [ ${#found_apps[@]} -eq 0 ]; then
        echo -e "${RED}Nessuna applicazione trovata per '$search_term'${NC}"
        return
    fi
    
    echo -e "${GREEN}Trovate ${#found_apps[@]} applicazioni:${NC}"
    echo
    
    local added_count=0
    
    # Mostra le app trovate e chiedi se aggiungerle
    for i in "${!found_apps[@]}"; do
        app_name="${found_apps[$i]}"
        desktop_file="${found_files[$i]}"
        
        # Verifica se già presente
        dockitem_name="${desktop_file%.desktop}.dockitem"
        if [ -f "$PLANK_DIR/$dockitem_name" ]; then
            echo -e "${YELLOW}[$((i+1))/${#found_apps[@]}] $app_name ${YELLOW}(già presente)${NC}"
            continue
        fi
        
        echo -e "${BLUE}[$((i+1))/${#found_apps[@]}] $app_name${NC}"
        echo -n "  Aggiungere alla dock? (s/n/q per uscire): "
        
        read -n 1 -r response
        echo
        
        case $response in
            [sS]|[yY])
                if add_to_plank "$desktop_file" "$app_name"; then
                    ((added_count++))
                fi
                ;;
            [qQ])
                echo -e "${YELLOW}Uscita...${NC}"
                break
                ;;
            *)
                echo "  Saltato"
                ;;
        esac
        echo
    done
    
    if [ $added_count -gt 0 ]; then
        echo -e "${GREEN}Aggiunte $added_count applicazioni!${NC}"
        echo -e "${BLUE}Plank è stato riavviato automaticamente${NC}"
    fi
}

# Menu principale
main_menu() {
    while true; do
        clear
        echo -e "${BLUE}=== GESTORE PLANK DOCK ===${NC}"
        echo
        show_current_apps
        echo "Opzioni:"
        echo "1) Cerca e aggiungi applicazioni"
        echo "2) Mostra app nella dock"
        echo "3) Rimuovi app dalla dock"
        echo "4) Riavvia Plank"
        echo "5) Esci"
        echo
        read -p "Scegli un'opzione (1-5): " choice
        
        case $choice in
            1)
                clear
                search_apps
                read -p "Premi ENTER per continuare..." -r
                ;;
            2)
                clear
                show_current_apps
                read -p "Premi ENTER per continuare..." -r
                ;;
            3)
                clear
                remove_apps
                read -p "Premi ENTER per continuare..." -r
                ;;
            4)
                restart_plank
                echo -e "${GREEN}Plank riavviato!${NC}"
                sleep 2
                ;;
            5)
                echo -e "${GREEN}Arrivederci!${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}Opzione non valida!${NC}"
                sleep 1
                ;;
        esac
    done
}

# Funzione per rimuovere app
remove_apps() {
    echo -e "${BLUE}Rimuovi applicazioni dalla dock:${NC}"
    echo
    
    if [ -z "$(ls -A "$PLANK_DIR" 2>/dev/null)" ]; then
        echo -e "${YELLOW}Nessuna app nella dock${NC}"
        return
    fi
    
    declare -a dock_apps=()
    declare -a dock_files=()
    
    for dockitem in "$PLANK_DIR"/*.dockitem; do
        if [ -f "$dockitem" ]; then
            launcher_path=$(grep "Launcher=" "$dockitem" | cut -d'=' -f2 | sed 's/file:\/\///')
            if [ -f "$launcher_path" ]; then
                app_name=$(grep "^Name=" "$launcher_path" | head -1 | cut -d'=' -f2)
                dock_apps+=("$app_name")
                dock_files+=("$(basename "$dockitem")")
            fi
        fi
    done
    
    if [ ${#dock_apps[@]} -eq 0 ]; then
        echo -e "${YELLOW}Nessuna app valida nella dock${NC}"
        return
    fi
    
    for i in "${!dock_apps[@]}"; do
        app_name="${dock_apps[$i]}"
        dockitem_file="${dock_files[$i]}"
        
        echo -e "${BLUE}[$((i+1))/${#dock_apps[@]}] $app_name${NC}"
        echo -n "  Rimuovere dalla dock? (s/n/q per uscire): "
        
        read -n 1 -r response
        echo
        
        case $response in
            [sS]|[yY])
                rm -f "$PLANK_DIR/$dockitem_file"
                echo -e "${GREEN}  ✓ $app_name rimosso dalla dock${NC}"
                ;;
            [qQ])
                echo -e "${YELLOW}Uscita...${NC}"
                break
                ;;
            *)
                echo "  Saltato"
                ;;
        esac
        echo
    done
    
    restart_plank
}

# Avvia il menu principale solo se lo script viene eseguito direttamente
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main_menu
fi
