# Wee Editor Refactoring Roadmap

## 1. Fondamenta ed Efficienza
- [x] **Undo/Redo Incrementale**: Passaggio da snapshot completi a registrazione di azioni atomiche (diff-based).
- [x] **Supporto UTF-8**: Gestione corretta dei caratteri multibyte per navigazione e rendering.
- [ ] **Ottimizzazione Struttura Dati Righe**: Valutazione Piece Table / Gap Buffer per inserimenti $O(1)$.

## 2. Performance e Rendering
- [ ] **Rendering Intelligente**: Ridisegno parziale delle sole righe modificate.
- [ ] **Sintassi Incrementale**: Ottimizzazione del ricalcolo dell'evidenziazione.
- [ ] **Memory Mapping (mmap)**: Caricamento efficiente di file di grandi dimensioni.

## 3. UX e Funzionalità Avanzate
- [ ] **Configurazione Esterna**: Spostamento delle impostazioni in un file config dedicato.
- [ ] **Ricerca Regex**: Integrazione libreria per ricerche avanzate.
- [ ] **LSP Lite**: Supporto base per Language Server Protocol.
