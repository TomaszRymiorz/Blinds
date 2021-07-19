# Blinds
Oprogramowanie rolety automatycznego domu.

### Budowa rolety
Mechanizm rolety został zbudowany na bazie ESP8266 wraz z modułem RTC DS1307 oraz fotorezystor pełniący funkcję czujnika światła. Uzupełnieniem jest silnik krokowy ze sterownikiem A4988 .

### Możliwości
Łączność z roletą odbywa się przez sieć Wi-Fi.
Dane dostępowe do routera przechowywane są wraz z innymi informacjami w pamięci flash.
W przypadku braku informacji o sieci, urządzenie aktywuje wyszukiwania routera z wykorzystaniem funkcji WPS.

Roleta automatycznie łączy się z zaprogramowaną siecią Wi-Fi w przypadku utraty połączenia.

Zawiera czujnik światła wykorzystywany przez funkcje automatycznych ustawień. Dane z czujnika przesyłane są również do pozostałych urządzeń działających w systemie iDom będących w tej samej sieci Wi-Fi.

Urządzenie posiada opcję wykonania pomiaru wysokości okna, zmianę kierunku obrotów silnika oraz możliwość kalibracji rolet. Funkcja pomiaru okna wyklucza stosowanie ograniczników krańcowych.

Urządzenia mogą pracować w tandemie w celu podniesienia cięższych rolet.

Rolecie można przypisać lokalizację przez co będzie ona automatycznie pobierać czasy wschodów i zachodów słońca z Internetu, stanie się niezależna od czujnika światła, który może informować tylko o zachmurzeniu. Czujnik jednak w dalszym ciągu będzie przesyłał informacje o do innych urządzeń jak robił to przed ustawieniem lokalizacji. Tworzy to możliwość podwójnego reagowania na zachodzące słońce; mamy czas pobrany z Internetu oraz wskazania czujnika światła z ustawioną granicą zmierzchu.

Zegar czasu rzeczywistego wykorzystywany jest przez funkcję ustawień automatycznych.
Ustawienia automatyczne obejmują opuszczanie i podnoszenie rolety o wybranej godzinie lub opuszczanie po zmroku / podnoszenie o świcie.
Możliwe jest również, że roleta opuści / podniesie się dopiero po spełnieniu obydwu warunków, np. "Podnieś o świcie, ale nie wcześniej niż o 6:00".
Powtarzalność obejmuje okres jednego tygodnia, a ustawienia nie są ograniczone ilościowo. W celu zminimalizowania objętości wykorzystany został zapis tożsamy ze zmienną boolean, czyli dopiero wystąpienie znaku wskazuje na włączoną funkcję.

* '1', '2', '3' numer rolety, którą steruje urządzenie
* '4' wszystkie rolety, którymi steruje urządzenie
* 'w' cały tydzień
* 'o' poniedziałek, 'u' wtorek, 'e' środa, 'h' czwartek, 'r' piątek, 'a' sobota, 's' niedziela
* 'n' opuść po zmroku
* 'd' podnieś o świcie
* 'n&' opuść po zmroku oraz wyznaczonej godzinie (oba warunki muszą zostać spełnione)
* 'd&' podnieś o świcie oraz wyznaczonej godzinie (oba warunki muszą zostać spełnione)
* 'z' reaguj na zachmurzenie
* '_' opuść o godzinie - jeśli znak występuje w zapisie, przed nim znajduje się godzina w zapisie czasu uniksowego
* '-' podnieś o godzinie - jeśli występuje w zapisie, po nim znajduje się godzina w zapisie czasu uniksowego
* '/' wyłącz ustawienie - obecność znaku wskazuje, że ustawienie będzie ignorowane
* 'b&' wszystkie wyzwalacze muszą zostać spełnione by wykonać czynność
* cyfra bezpośrednio przed 'b', ale nie przed "_" oznacza procentową pozycję rolety

Obecność znaku 'b' wskazuje, że ustawienie dotyczy rolety.

Przykład zapisu trzech różnych ustawień automatycznych: 1140_b3w-420,b4asn,/b12ouehrn-300

### Sterowanie
Sterowanie urządzeniem odbywa się poprzez wykorzystanie metod dostępnych w protokole HTTP. Sterować można z przeglądarki lub dedykowanej aplikacji.

* "/hello" - Handshake wykorzystywany przez dedykowaną aplikację, służy do potwierdzenia tożsamości oraz przesłaniu wszystkich parametrów pracy urządzenia.

* "/set" - Pod ten adres przesyłane są ustawienia dla rolety, dane przesyłane w formacie JSON. Ustawić można m.in. strefę czasową ("offset"), czas RTC ("time"), ustawienia automatyczne ("smart"), pozycję rolety na oknie ("val"), dokonać kalibracji pozycji, jak również zmienić ilość kroków czy wartość granicy dnia i nocy.

* "/state" - Służy do regularnego odpytywania urządzenia o jego podstawowe stany, położenie rolety i wskazania czujnika oświetlenia.

* "/reset" - Ustawia wartość pozycji rolet na 0.

* "/measurement" - Służy do wykonania pomiaru wysokości okna.

* "/basicdata" - Służy innym urządzeniom systemu iDom do samokontroli, urządzenia po uruchomieniu odpytują się wzajemnie o aktualny czas lub dane z czujników.

* "/log" - Pod tym adresem znajduje się dziennik aktywności urządzenia (domyślnie wyłączony).
