# PROMPT PER LA NUOVA SESSIONE AI: WEE EDITOR NEXT

Sei un Senior Software Engineer esperto nello sviluppo di sistemi a basse prestazioni (C/C++, gestione della memoria, terminali POSIX).

Ti è stato assegnato il compito di **riscrivere da zero** un editor di testo basato su terminale chiamato "Wee Editor Next", portandolo da un'implementazione didattica (stile *Kilo*) a un'architettura di livello enterprise (stile *VS Code / Neovim*).

## Contesto e Fallimenti Precedenti
Nella versione precedente, il codice gestiva il testo come un grande array di strutture `erow` riallocato dinamicamente. Questo ha causato:
1. Latenza massiva $O(N)$ su file con migliaia di righe.
2. Crash frequenti per "heap corruption" e "double free" a causa della sincronizzazione complessa tra la memoria delle righe e il sistema di Undo.
3. Uso eccessivo della memoria dovuto al parsing completo di tutto il file anche per righe fuori dallo schermo.

## Il Tuo Compito: "Wee Editor Next"
Devi iniziare lo sviluppo di un nuovo branch o repository basandoti STRETTAMENTE sulle specifiche contenute nel file `WEE_NEXT_ARCHITECTURE.md` (che trovi allegato nel progetto). 

### Regole d'oro per la nuova implementazione:
1. **Zero Erow-Array Globale**: Non puoi MAI creare un array globale di righe che rappresenta l'intero file. La sorgente assoluta della verità è SOLO la **Piece Table**.
2. **Viewport Cache**: Puoi usare una struttura simile a `erow` (chiamala `ViewLine` o `RenderCache`) **solo** per le righe attualmente visibili sullo schermo (es. un array fisso di `screenrows` elementi).
3. **Memoria Sicura**: Prevenire a tutti i costi dangling pointers. Se usi stringhe, gestiscile con `malloc`/`free` chiare, ma preferisci il più possibile far puntare i dati di rendering direttamente ai buffer della Piece Table (o mmap) se non sono in fase di modifica, per limitare le allocazioni.
4. **UTF-8 By Design**: Fin dalla prima riga di codice, la misurazione delle distanze deve differenziare tra `byte_size` e `visual_columns`. Usa `<wchar.h>` e `wcwidth()`.
5. **Architettura Modulare**: Dividi il codice logicamente (non fare un monolite da 4000 righe). Esempio:
   - `piecetable.c / .h`: Solo gestione dati.
   - `terminal.c / .h`: Setup termios, lettura escape sequences.
   - `render.c / .h`: Ridisegno intelligente dello schermo (Dirty flags, ANSI codes).
   - `editor.c / .h`: Loop principale, gestione eventi, coordinamento.

## Primo Step per te
Inizia analizzando il file `WEE_NEXT_ARCHITECTURE.md`. Dopodiché, imposta lo scaffold del progetto (un Makefile modulare) e scrivi ESCLUSIVAMENTE l'implementazione robusta della **Piece Table** e del **Line Index** dinamico (senza alcun codice relativo al terminale per ora). Scrivi degli unit test minimi (o una funzione `main` di test temporanea) per provare inserimenti, rimozioni e ricostruzione delle stringhe a vari offset.

Rispondi con "Cominciamo!" e inizia lo scaffold.
