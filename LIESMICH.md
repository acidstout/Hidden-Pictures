# Hidden Pictures

Verwandelt jedes Bild in zwei Blätter mit zufällig wirkendem Schwarzweiß-Rauschen. Jedes Blatt für sich ist unlesbares Kauderwelsch. Beide auf Klarsichtfolie (oder dünnes Papier) drucken, auseinanderschneiden, übereinanderlegen und gegen das Licht halten, und das versteckte Bild erscheint.

Das ist der klassische Trick der *visuellen Kryptografie* (Naor–Shamir), verpackt in eine kleine, moderne Windows-App.

- Native Win32-Anwendung in C17, ohne Laufzeitumgebung und ohne Drittanbieter-Abhängigkeiten
- Windows 10 / 11, Per-Monitor-DPI-fähig, helles und dunkles Design
- Deutsche und englische Benutzeroberfläche (folgt der Windows-Anzeigesprache)
- Eine einzelne `.exe` von etwa 250 KB
- Freie Software unter der [GPL-3.0](LICENSE)

## So funktioniert es

1. Der gewählte Bildausschnitt wird in ein Raster aus quadratischen Zellen zerlegt (Einstellung **Detailgrad**).
2. Die Helligkeit der Zellen wird automatisch im Kontrast gestreckt und mit Floyd-Steinberg-Dithering in Schwarzweiß umgewandelt.
3. **Blatt A** ist reines Zufallsrauschen.
4. **Blatt B** wird aus A und dem Bild abgeleitet: Wo das Bild weiß ist, ist B gleich A; wo das Bild schwarz ist, ist B das Gegenteil von A.
5. Übereinandergelegt gewinnt Schwarz. Weiße Bildbereiche zeigen etwa 50 % Rauschen, schwarze Bildbereiche sind vollständig schwarz. Das Bild erscheint dunkel auf gesprenkeltem Hintergrund.

Kein einzelnes Blatt enthält irgendeine Information über das Bild.

## Bedienung

1. **Bild öffnen**: Button anklicken, auf die linke Fläche klicken oder eine Datei auf das Fenster ziehen.
   Unterstützt wird alles, was die Windows Imaging Component dekodieren kann, etwa JPEG, PNG, GIF, BMP, TIFF, JPEG XR, ICO sowie HEIC/WebP/AVIF, wenn der passende Codec installiert ist. Die EXIF-Ausrichtung wird berücksichtigt.
2. **Ausschnitt wählen**: Ein Rechteck über das Bild ziehen. Kanten, Ecken oder das Innere ziehen, um es anzupassen oder zu verschieben.
3. **Zaubern**: Erzeugt die beiden Blätter. Sie fahren zusammen und zeigen das enthüllte Bild.
4. **Ergebnis erkunden**: siehe unten.
5. Als PNG **speichern** oder **drucken**.

### Ausschnitt wählen

| Aktion | Ergebnis |
| --- | --- |
| Außerhalb der Auswahl ziehen | Zeichnet eine neue Auswahl |
| An Ecken / Kanten ziehen | Ändert die Größe der Auswahl |
| Innerhalb der Auswahl ziehen | Verschiebt die Auswahl |
| Klicken (ohne zu ziehen) außerhalb der Auswahl oder auf die leere Fläche | Öffnet den Datei-Dialog |

### Ergebnisfläche

Die rechte Fläche zeigt nur die beiden erzeugten Blätter.

- **Regler** (*Übereinander ↔ Getrennt*): Schiebt die Blätter übereinander oder auseinander. Nach dem Erzeugen liegen die Blätter zunächst übereinander, sodass du das Ergebnis sofort siehst.
- **Blatt mit der Maus ziehen**: Die Blätter bewegen sich in ganzen Zellenschritten. Liegt ein Blatt höchstens eine Zelle neben der perfekten Deckung, rastet es ein und das Bild erscheint. Ist es um eine Zelle verschoben, sieht man wieder nur Rauschen. Das zeigt, wie genau das Übereinanderlegen auf Papier sein muss.
- **Tastatur**: Die Pfeiltasten verschieben Blatt B um eine Zelle, `Pos1` legt die Blätter übereinander, `Ende` trennt sie.

### Einstellungen

| Einstellung | Beschreibung |
| --- | --- |
| **Blattgröße** | Kantenlänge eines Blatts in Zentimetern (bei nicht quadratischen Ausschnitten die längere Seite). Begrenzt durch das Papierformat des gewählten Druckers, siehe unten. |
| **Detailgrad** | Anzahl der Zellen entlang der längeren Seite, 48 bis 384 in Schritten von 16 (Standard 160). Mehr Detail bedeutet ein feineres Bild, aber schwierigeres Ausrichten. Für den ersten Druck sind 128 bis 160 ein guter Ausgangspunkt. |
| **Drucker** | Zeigt den aktuellen Drucker und das Papierformat. Ein Klick öffnet die Windows-Druckeinrichtung (Drucker, Eigenschaften, Papierformat, Ausrichtung). |
| **Heller / Dunkler Modus** | Umschalter oben rechts. Folgt Windows, bis du selbst wählst. Deine Wahl wird dann gespeichert. |

Wenn du Blattgröße oder Detailgrad nach dem Erzeugen änderst, gilt das Ergebnis als veraltet. Drücke erneut **Zaubern**, um die Änderung anzuwenden.

### Drucker und Papierformat

Die maximale Blattgröße ergibt sich aus dem Papier des gewählten Druckers. Beide Blätter werden untereinander gedruckt, mit 10 mm Rand und 12 mm Abstand und einer gestrichelten Schnittlinie dazwischen.

- Ohne ausdrückliche Wahl folgt die App dem **Windows-Standarddrucker**. Sie liest ihn jedes Mal neu ein, wenn das Fenster aktiviert wird. Änderungen in Windows werden also ohne Neustart übernommen.
- Ein in der App gewählter Drucker (über den Drucker-Button oder den Druckdialog) wird **über Neustarts hinweg gespeichert**, einschließlich Papierformat und Ausrichtung. Ist dieser Drucker nicht mehr installiert, fällt die App auf den Standarddrucker zurück.
- Ist kein Drucker vorhanden, wird A4 angenommen.

### Speichern

**Speichern** fragt nach dem Ziel und schreibt ein Bild, das beide Blätter untereinander enthält, getrennt durch eine gestrichelte Schnittlinie. Das Dateiformat wählst du im Speichern-Dialog (oder durch Eintippen der Endung). Wechselst du dort den Dateityp, passt sich auch die Endung im Dateinamen an.

| Format | Hinweise |
| --- | --- |
| **PNG** (Standard) | 1 Bit Schwarzweiß, verlustfrei, kleinste Dateien |
| **TIFF** | 1 Bit Schwarzweiß, verlustfrei. Die App kodiert das Bild mit **LZW** und mit **PackBits** und behält die jeweils kleinere Variante |
| **BMP** | 1 Bit mit Schwarzweiß-Palette, unkomprimiert |
| **GIF** | Palettenbild (Schwarzweiß), vom Format selbst LZW-komprimiert |

- Die Auflösung liegt in der Größenordnung von 600 dpi. PNG, TIFF und BMP speichern die dpi-Angabe, sodass die Datei in genau der gewählten Blattgröße gedruckt wird (GIF kennt keine dpi-Angabe).
- Alle Formate enthalten exakt dieselben Pixel.
- Standard-Dateiname: `<Quelle>_hidden.png`

### Drucken

**Drucken** öffnet den Standard-Druckdialog von Windows und druckt beide Blätter in der gewählten physischen Größe. Nur wenn sie nicht auf die Seite passen, werden sie verkleinert.

Tipps für gute Ergebnisse:

- Für die beste Wirkung auf **Klarsichtfolie** drucken (Tintenstrahl oder Laser, passend zum Drucker). Dünnes Papier vor einem hellen Fenster funktioniert ebenfalls.
- Mit höchster Qualität drucken, **Skalierung auf 100 %** und ohne "An Seite anpassen" oder Tintensparmodus im Druckertreiber.
- Entlang der gestrichelten Linie schneiden, die Zellraster genau ausrichten und den Stapel dann gegen das Licht halten.

## Fenster, Design und Speicherort der Einstellungen

- Beim ersten Start öffnet sich das Fenster zentriert. Größe, Position und der Zustand "maximiert" werden beim nächsten Start wiederhergestellt.
- Die Einstellungen liegen pro Benutzer unter `HKCU\Software\Rekow IT\Hidden Pictures`: Fensterposition, Designwahl, Druckername und Druckereinstellungen. Lösche diesen Schlüssel, um alles zurückzusetzen.
- Der Info-Dialog (Klick auf die Version in der Statuszeile) öffnet sich immer zentriert über dem App-Fenster.

## Erstellen (Build)

Voraussetzungen: Visual Studio 2022 Build Tools (MSVC) und das Windows SDK.

```bat
build.bat
```

Das Skript kompiliert `app.rc` (Zeichenfolgentabellen für Englisch und Deutsch, Icon, Manifest, `VERSIONINFO`) und `main.c` mit `/std:c17`, `/W4` und aktiviertem Control Flow Guard und erzeugt `HiddenPictures.exe`.

| Datei | Zweck |
| --- | --- |
| `main.c` | Die gesamte Anwendung |
| `app.rc`, `resource.h` | Texte (EN/DE), Versionsinformationen, Verweise auf Icon und Manifest |
| `app.manifest` | Common Controls v6, Windows 10/11, Per-Monitor-DPI v2 |
| `build.bat` | Build-Skript |

## Kommandozeile

```
HiddenPictures.exe [Bilddatei]
```

Öffnet die angegebene Bilddatei beim Start.

## Lizenz und Copyright

Copyright © 2026 Rekow IT

Hidden Pictures ist freie Software: Du kannst sie unter den Bedingungen der GNU General Public License, wie von der Free Software Foundation veröffentlicht, weitergeben und/oder verändern, entweder gemäß Version 3 der Lizenz oder (nach deiner Wahl) jeder späteren Version.

Sie wird in der Hoffnung verbreitet, dass sie nützlich ist, aber OHNE JEDE GEWÄHRLEISTUNG, sogar ohne die implizite Gewährleistung der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK. Weitere Einzelheiten findest du in der [GNU General Public License](LICENSE).

Dies ist eine inoffizielle Übersetzung des englischen Lizenzhinweises. Maßgeblich ist der englische Originaltext in der Datei [LICENSE](LICENSE).

SPDX-License-Identifier: GPL-3.0-or-later
