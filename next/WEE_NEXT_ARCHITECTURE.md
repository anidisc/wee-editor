# Wee Editor Next - Architectural Blueprint & Roadmap

## 1. Visione e Obiettivi
L'obiettivo di "Wee Editor Next" è ricostruire da zero un editor di testo da terminale basato su un'architettura di livello enterprise. L'esperienza pregressa ha dimostrato che un approccio "ingenuo" basato su array di righe (`erow`) riallocati dinamicamente collassa rapidamente su file di grandi dimensioni, portando a latenze inaccettabili e corruzione della memoria (heap corruption, double free, dangling pointers). 

La nuova iterazione deve essere **veloce, memory-safe, nativamente UTF-8 e capace di aprire file di svariati Gigabyte istantaneamente**.

## 2. Core Architecture (I Pilastri)

### 2.1. Struttura Dati: La Piece Table
Abbandonare completamente l'array di righe come fonte di verità.
*   **Original Buffer**: Buffer in sola lettura mappato in memoria (`mmap`) per caricamento istantaneo di file immensi.
*   **Add Buffer**: Buffer append-only per le modifiche dell'utente.
*   **Piece List**: Lista doppiamente concatenata o albero bilanciato (Splay Tree / Red-Black Tree) che descrive la sequenza logica del testo puntando ai due buffer.
*   *Vantaggio*: Inserimenti e cancellazioni sono $O(1)$ o $O(\log N)$ indipendentemente dalla dimensione del file.

### 2.2. Gestione delle Righe: Line Index Incrementale
Il terminale lavora in 2D (x, y), ma la Piece Table è 1D. 
*   **Line Index**: Un array dinamico che mappa l'indice della riga `Y` all'offset assoluto (byte) all'interno della Piece Table.
*   **Aggiornamento Matematico**: Quando l'utente digita, gli offset successivi vengono scalati matematicamente (+1 o -1) senza mai scansionare l'intero file. Il ricalcolo avviene solo quando si preme `Invio` o si incolla testo multilinea.

### 2.3. Sincronizzazione e Cache Visiva (Render Layer)
Separare nettamente il modello dati dalla vista (MVC).
*   **Viewport Cache**: L'editor mantiene una cache (`erow`) *esclusivamente* per le righe attualmente visibili sullo schermo (es. 50 righe), più un piccolo margine di pre-fetching.
*   Non esiste più un array `E.row` grande quanto l'intero file. Questo elimina alla radice i problemi di Out-Of-Memory e le latenze di ricalcolo.

### 2.4. Rendering Intelligente & Dirty Flags
*   **Ridisegno Selettivo**: Ogni riga visibile ha un flag `dirty`. Il loop di rendering invia al terminale solo le stringhe ANSI per le righe sporche, usando il comando di pulizia linea (`\x1b[K`).
*   **Flicker-Free**: Aggiornamento puntuale del cursore (`\x1b[Y;XH`) prima di disegnare ogni riga modificata, evitando di ripulire l'intero schermo (`\x1b[2J`).

### 2.5. UTF-8 Nativo
*   Gli indici interni (`rx`, `cx`) non devono assumere che 1 byte = 1 colonna visiva.
*   Uso rigoroso di decodifica UTF-8 per misurare la lunghezza in byte di un carattere e `wcwidth()` per calcolare la larghezza visiva sul terminale (gestendo correttamente emoji, tabulazioni e caratteri CJK).

### 2.6. Undo/Redo Incrementale
*   Nessuno snapshot. Il sistema memorizza azioni atomiche logiche (`INSERT_TEXT`, `DELETE_TEXT`) con l'offset assoluto e il testo modificato.
*   Questo garantisce un consumo di memoria trascurabile anche per sessioni di editing prolungate.

---

## 3. Roadmap di Sviluppo (Da Zero)

**Fase 1: Fondamenta e Strutture Dati (Headless) [COMPLETATA]**
*   [x] Setup del progetto (C11/C17, Makefile, testing unitario).
*   [x] Implementazione della **Piece Table** pura. Test di inserimento, split e join (Fuzz tested).
*   [x] Implementazione del **Line Index** dinamico.
*   [x] Implementazione I/O tramite `mmap` per la lettura.

**Fase 2: Terminale e UTF-8 [COMPLETATA]**
*   [x] Setup della modalità Raw del terminale (termios).
*   [x] Motore di parsing input e decodifica UTF-8.
*   [x] Logica di posizionamento cursore visivo (`rx`) vs logico (`cx`).

**Fase 3: Viewport e Rendering [COMPLETATA]**
*   [x] Costruzione del Render Layer: Viewport Cache (`ViewLine`) per distacco MVC.
*   [x] Implementazione del rendering flicker-free tramite buffer `abuf`.

**Fase 4: Editing e Undo/Redo [IN CORSO]**
*   [x] Integrazione dell'input utente con le API della Piece Table.
*   [x] Implementazione dello stack di Undo/Redo basato su azioni logiche.
*   [ ] Gestione della selezione (Shift+Frecce) e Copy/Paste.

**Fase 5: Sintassi e UI Avanzata [IN CORSO]**
*   [x] Caricamento configurazioni JSON (tramite cJSON).
*   [x] Motore di Highlighting limitato al Viewport.
*   [x] Status bar con coordinate e indicatori.
* [x] Modal Search (Find) bidirezionale con navigazione e contatori.
* [x] Funzione Replace (Cerca e Sostituisci).
* [x] File Browser con navigazione directory e caricamento file.

