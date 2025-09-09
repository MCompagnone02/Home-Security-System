# Home-Security-System
Sistema sicurezza distribuito Raspberry Pi Pico + Windows Host.  Embedded: FreeRTOS multi-task, sensori PIR/vibrazione, comunicazione USB. Desktop: Serial auto-detection, SQLite database, web dashboard real-time, pattern recognition. Architettura embedded-host con fallback simulato per demo.

**Architettura**
_Sistema Embedded (Raspberry Pi Pico)_
Microcontroller: RP2040 dual-core ARM Cortex-M0+ @ 133MHz
RTOS: FreeRTOS con 3 task concorrenti
Sensori: PIR motion detection + sensore vibrazione
Comunicazione: USB Serial

_Sistema Host (Windows)_
Linguaggio: C++ con librerie native Windows
Database: SQLite3 per persistenza eventi
Web Server: HTTP server custom su porta 8080
Analytics: Pattern recognition e threat assessment
Threading: 3 thread specializzati (serial, web, analytics)

**Funzionalità**
_Embedded Features_
Multi-sensor fusion: Combinazione PIR + vibrazione per ridurre falsi positivi
State machine: Gestione stati sicurezza con timeout automatici
Edge detection: Algoritmo ottimizzato per rilevamento transizioni
Real-time feedback: LED tri-colore per indicazione stato sistema
Memory optimization: Stack sizing calibrato per RP2040

_Host Features_
Auto-detection: Rilevamento automatico porta COM del Pico
Fallback mode: Modalità simulata per demo senza hardware
Real-time dashboard: Interface web responsive con aggiornamenti live
Analisi clustering temporale e confidence boosting
Database logging: Storico completo eventi e allarmi

**Dashboard Web**
Interfaccia web accessibile su http://localhost:8080 con:
Stato sistema: Threat level globale, statistiche eventi
Zone monitoring: Threat level per singola location
Eventi real-time: Tabella ultimi eventi con timestamp e confidence
Visual feedback: Colori dinamici basati su livelli minaccia
Auto-refresh
