# Roleta iDom
Oprogramowanie rolety automatycznego domu.

### Budowa rolety
Mechanizm rolety został zbudowany na bazie ESP8266 wraz z modułem RTC DS1307 oraz fotorezystorem pełniącym funkcję czujnika światła. Uzupełnieniem jest silnik krokowy ze sterownikiem A4988.

### Możliwości
Łączność z roletą odbywa się przez sieć Wi-Fi.
Roleta automatycznie łączy się z zaprogramowaną siecią Wi-Fi w przypadku utraty połączenia.
W przypadku braku informacji o sieci lub braku łączności z zapamiętaną siecią, urządzenie aktywuje wyszukiwania routera z wykorzystaniem funkcji WPS.

Dane dostępowe do routera przechowywane są wraz z innymi informacjami w pamięci flash.

Zawiera czujnik światła wykorzystywany przez funkcje automatycznych ustawień. Dane z czujnika przesyłane są również do pozostałych urządzeń działających w systemie iDom będących w tej samej sieci Wi-Fi.

Urządzenie posiada opcję wykonania pomiaru wysokości okna, zmianę kierunku obrotów silnika oraz możliwość kalibracji położenia rolet. Funkcja pomiaru wysokości okna pozwala wykluczyć stosowanie ograniczników krańcowych.

Urządzenia mogą pracować w tandemie w celu podniesienia cięższych rolet.

Roleta ma możliwość reagowania na wschód i zachód słońca (po podaniu lokalizacji) oraz zmierzch i świt (dane otrzymane z czujnika światła).

Zegar czasu rzeczywistego wykorzystywany jest przez funkcję ustawień automatycznych. Czas jest synchronizowany z Internetu.

Ustawienia automatyczne obejmują opuszczanie, podnoszenie lub zaprogramowanie % położenia rolety o wybranej godzinie, reagowanie na zmierzch, świt, zachód czy wschód słońca.
Możliwe jest również, ustawienie wymogu spełnienia kilku warunków jednocześnie, np. "Podnieś o świcie, ale nie wcześniej niż o 6:00".
Powtarzalność ustawień automatycznych obejmuje okres jednego tygodnia, a ustawienia nie są ograniczone ilościowo.
W celu zminimalizowania objętości wykorzystany został zapis tożsamy ze zmienną boolean, czyli dopiero wystąpienie znaku wskazuje na włączoną funkcję.

* '1', '2', '3' przed symbolem "|" lub "&" (jeśli nie występuje "|") oznacza numer rolety, którą steruje urządzenie
* '4' to wszystkie rolety, którymi steruje urządzenie
* 'o' poniedziałek, 'u' wtorek, 'e' środa, 'h' czwartek, 'r' piątek, 'a' sobota, 's' niedziela
* Brak wskazania dnia wygodnia oznacza, że ustawienie obejmuje cały tydzień
* 'n' wyzwalacz o zachodzie słońca.
* 'd' wyzwalacz o wschodzie słońca
* '<' wyzwalacz o zmroku
* '>' wyzwalacz o świcie
* 'z' wyzwalacz reaguj na zachmurzenie (po zmroku oraz po świcie)
* Każdy z powyższych wyzwalaczy może zawierać dodatkowe parametry zawarte w nawiasach, jak opóźnienie czasowe lub własne ustawienie LDR.
* 'l()', 'b()', 't()', 'c()' to wyzwalacze związane bezpośrednio z urządzeniem.
* 'l()' włączenie/wyłączenie światła
* 'b()', 'c()' pozycja rolety lub okna
* 't()' osiągnięcie określonej temperatury na termostacie
* '_' o godzinie - jeśli znak występuje w zapisie, przed nim znajduje się godzina w zapisie czasu uniksowego
* 'h(-1;-1)' między godzinami, jeśli obie cyfry są różne od "-1" lub po godzinie, przed godziną. "-1" oznacza, że nie ma wskazanej godziny
* '/' wyłącz ustawienie - obecność znaku wskazuje, że ustawienie będzie ignorowane
* '&' wszystkie wyzwalacze muszą zostać spełnione by wykonać akcje
* cyfra między symbolami "|" i "|" (lub "&" jako drugi symbol, jeśli jest wskazanie na wszystkie wyzwalacze) oznacza akcje do wykonania
* Obecność znaku 'b' wskazuje, że ustawienie dotyczy rolety.
* 'r()' i 'r2()' w nawiasach zawierają warunki, które muszą być spełnione w chwili aktywacji wyzwalacza, aby wykonać akcje
* 'r()' to wymaganie określonego stanu świateł, pozycji rolety, okna lub stanu czy temperatury termostatu
* 'r2()' wymaganie dotyczące pozycji słońca: wschód, zmierzch, świt, zmrok

### Sterowanie
Sterowanie urządzeniem odbywa się poprzez wykorzystanie metod dostępnych w protokole HTTP. Sterować można z przeglądarki lub dedykowanej aplikacji.

* "/hello" - Handshake wykorzystywany przez dedykowaną aplikację, służy do potwierdzenia tożsamości oraz przesłaniu wszystkich parametrów pracy urządzenia.

* "/set" - Pod ten adres przesyłane są ustawienia dla rolety, dane przesyłane w formacie JSON. Ustawić można m.in. strefę czasową ("offset"), czas RTC ("time"), ustawienia automatyczne ("smart"), pozycję rolety na oknie ("val"), dokonać kalibracji pozycji, jak również zmienić ilość kroków czy wartość granicy dnia i nocy.

* "/state" - Służy do regularnego odpytywania urządzenia o jego podstawowe stany, położenie rolety i wskazania czujnika oświetlenia.

* "/reset" - Ustawia wartość pozycji rolet na 0.

* "/measurement" - Służy do wykonania pomiaru wysokości okna.

* "/basicdata" - Służy innym urządzeniom systemu iDom do samokontroli, urządzenia po uruchomieniu odpytują się wzajemnie m.in. o aktualny czas lub dane z czujników.

* "/log" - Pod tym adresem znajduje się dziennik aktywności urządzenia (domyślnie wyłączony).
