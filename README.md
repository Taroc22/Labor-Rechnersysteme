# TRDP-Temperaturüberwachung der Drehgestelle

## Information

Die Formatierung der Markdown Datei wurde mit KI erstellt, der Inhalt ist in Eigenleistung geschrieben.

## Funktion

Der Receiver empfängt die Temperaturwerte der vier Drehgestelle als TRDP
Process Data (PD) über das Loopback-Interface `127.0.0.1`:

| ComID | Drehgestell |
|-------|-------------|
| 1100  | 1           |
| 1101  | 2           |
| 1102  | 3           |
| 1103  | 4           |

Jedes Telegramm enthält die Temperatur als 1 Byte in °C. Die Drehgestellnummer
wird aus der ComID abgeleitet.

Jeder empfangene Wert wird mit dem Grenzwert von **100 °C** verglichen.
Als Überschreitung gilt ein Wert **größer als 100 °C**.

## Aufbau

- **Eine TRDP-Session:** `tlc_init()` gilt prozessweit, deshalb kann der
  PD-Wrapper nur einmal pro Prozess geöffnet werden. Auf dieser einen Session
  werden vier Subscriber für die ComIDs 1100–1103 angelegt.
- **Main-Thread:** Er ruft zyklisch `pd_comm_process()` auf. Damit werden
  eingehende Telegramme empfangen und den Subscribern zugeordnet sowie
  ausgehende MD-Nachrichten versendet.
- **Vier Worker-Threads:** Pro ComID ein Thread (`vos_threadCreate`). Er liest
  den aktuellen Wert, prüft den Grenzwert, protokolliert und löst bei Bedarf
  die Ereignismeldung aus.
- **Mutex:** Sorgt dafür, dass immer nur ein Thread auf die gemeinsame
  Session zugreift.

## Ereignismeldung

Bei einer Grenzwertüberschreitung wird eine **TRDP Message Notification (MN)**
mit ComID **1200** gesendet. Die Meldung wird nur beim Übergang von „OK“ zu
„überschritten“ gesendet und nicht bei jedem weiteren Telegramm. Sinkt der Wert
wieder auf ≤ 100 °C, wird der Zustand zurückgesetzt.

### Begründung: Message Notification statt Message Data Request

- Eine Grenzwertüberschreitung ist eine **einseitige Ereignismeldung**. Der
  Sender braucht keine Antwort, um seine Aufgabe zu erfüllen.
- Ein **Message Data Request (MR)** erwartet eine Antwort. Bleibt sie aus,
  läuft der Request in einen Timeout. Da laut Aufgabenstellung kein Empfänger
  implementiert wird, würde jeder MR fehlschlagen.

### Aufbau der MN-Nutzdaten (12 Byte)

| Byte  | Inhalt                                |
|-------|---------------------------------------|
| 0     | Drehgestellnummer                     |
| 1     | Temperatur in °C                      |
| 2     | Grenzwert in °C                       |
| 3     | reserviert                            |
| 4–7   | Zeitpunkt, Unix-Sekunden              |
| 8–11  | Millisekunden                         |

## Logging

Alle Meldungen werden mit Zeitstempel auf der Konsole ausgegeben:

Protokolliert werden Drehgestell, aktuelle Temperatur, Zeitpunkt
der Überschreitung und die ausgelöste TRDP-Kommunikation.