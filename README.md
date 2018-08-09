# Blinds 1
Oprogramowanie rolety automatycznego domu. 

### Roleta
Całość zbudowana na WIFI D1 mini z modułem Data logger (RTC DS1307 + MicroSD) oraz silnikiem krokowym 28BYJ-48.

### Możliwości
Łączność z roletą odbywa się przez sieć Wi-Fi. 
Dane dostępowe do routera przechowywane są wraz z innymi informacjami na karcie pamięci. Pozwala to uniknąć przykrych niespodzianek po zaniku prądu i daje możliwość szybkiego kopiowania ustawień między urządzeniami. 
W przypadku braku informacji o sieci, urządzenie aktywuje wyszukiwania routera z wykorzystaniem funkcji WPS.
Roletę można ustawić w tryb online, wówczas będzie ona sprawdzać dedykowany Webservice i możliwe stanie się sterowanie przez Internet.

Roleta automatycznie łączy się z zaprogramowaną siecią Wi-Fi w przypadku utraty połączenia.

Zawiera czujnik światła wykorzystywany przez funkcje automatycznych ustawień. Dane z czujnika wysyłane są również do bliźniaczego urządzenia w pomieszczeniu jakim jest włącznik światła. Parowanie urządzeń odbywa się poprzez aplikację dedykowaną.

Roleta posiada opcję mierzenia wysokości okna, zmianę kierunku obrotów silnika oraz możliwość przesunięcie rolety o wskazaną wartość. Funkcja mierzenia okna wyklucza stosowanie niewygodnych ograniczników krańcowych, zapobiega pomyłkom błędnie podanych ręcznie wartości i ewentualnych różnic w silnikach w przypadku wartości zdefiniowanych programowo.

Roleta wykorzystuje RTC do wywoływania ustawień automatycznych. 
Ustawienia automatyczne obejmują opuszczanie i podnoszenie rolety o wybranej godzinie oraz opuszczanie po zapadnięciu zmroku i podnoszenie o świcie. Powtarzalność obejmuje okres jednego tygodnia, a ustawienia nie są ograniczone ilościowo. W celu zminimalizowania objętości wykorzystany został zapis tożsamy ze zmienną boolean, czyli dopiero wystąpienie znaku wskazuje na włączoną funkcję.

* 'w' cały tydzień - występuje tylko w zapisie aplikacji w celu zminimalizowania ilości przesyłanych danych
* 'o' poniedziałek, 'u' wtorek, 'e' środa, 'h' czwartek, 'r' piątek, 'a' sobota, 's' niedziela
* 'n' opuść po zmroku / podnieś o świcie
* '_' opuść o godzinie - jeśli znak występuje w zapisie, przed nim znajduje się godzina w zapisie czasu uniksowego
* '-' podnieś o godzinie - jeśli występuje w zapisie, po nim znajduje się godzina w zapisie czasu uniksowego

Przykład zapisu trzech ustawień automatycznych: 1140_bw-420,basn,bouehrn-300

Ustawienia automatyczne obejmują również włącznik światła, a obecność znaku 'b' wskazuje, że ustawienie dotyczy rolety.

### Sterowanie
Sterowanie roletą odbywa się poprzez wykorzystanie metod dostępnych w protokole HTTP. Sterować można z przeglądarki lub dedykowanej aplikacji. 

* "/hello" - Handshake wykorzystywany przez dedykowaną aplikację, służy do potwierdzenia tożsamości oraz przesłaniu wszystkich parametrów pracy rolety.

* "/set" - Pod ten adres przesyłane są ustawienia dla rolety, dane przesyłane w formacie JSON. Ustawić można strefę czasową ("offset"), czas RTC ("access"), IP bliźniaczego urządzenia ("twin"), ustawienia automatyzujące ("smart"), pozycję rolety na oknie ("coverage") oraz dokonać kalibracji pozycji ("calibrate").

* "/state" - Służy do regularnego odpytywania rolety o jej podstawowe stany, położenie rolety ("coverage"), wskazania czujnika oświetlenia ("twilight") oraz ilość kroków silnika w przypadku wykonywania pomiaru okna ("measure").

* "/reversed" - Odpytanie tego adresu zmienia kierunek obracania się silnika rolety.

* "/reset" - Ustawia wartość pozycji rolety na 0.

* "/measurement" - Służy do wykonania pomiaru wysokości okna.
