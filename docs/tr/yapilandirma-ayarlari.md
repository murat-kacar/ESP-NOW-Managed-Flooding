# Yapılandırma Ayarları

Flaşlama programının (GUI) **Configuration** bölümündeki ayarların ne işe yaradığı. Değerler firmware'e derleme
sırasında gömülür. Bir ayarı değiştirdikten sonra firmware'i yeniden derleyip cihazlara yükleyin.

- **Varsayılan** sütunu, hiçbir şey değiştirilmediğinde kullanılan değerdir.
- Bazı varsayılanlar henüz tezgahta veya sahada doğrulanmadı (SPEC 9); bunlar "ölçülecek" diye işaretlidir.
- **Doorbell network** menüsündeki ayarlar panelde ve tüm daire modüllerinde **aynı** olmalıdır.
- İmkânsız bir kombinasyon (örneğin en kısa süre en uzundan büyük) derleme sırasında hata verir.

---

## 1. Doorbell network (panel ve modül)

### 1.1. Radyo ve mesaj aktarma

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Maximum TX power | 20 dBm | Yayın gücü. **10 dBm** (laboratuvar) seçeneği gerçekte 8,5 dBm verir. Düşük güç menzili kısaltır, masa üstü testte yan cihazları rahatsız etmez. |
| Forward TTL (hops) | 10 | Bir mesajın en fazla kaç ara modül tarafından aktarılabileceği. Aralık 1–30. |
| Forward RSSI threshold | -55 dBm | Bir modül mesajı bu değerden **güçlü** aldıysa aktarır, daha zayıf aldıysa aktarmaz (kendisi yine okur). Ölçülecek. |
| Retransmit count | 10 | Her mesajın her aktarmada havaya kaç kez gönderildiği. 0 girilirse 1 kabul edilir. |
| Drop packets weaker than the forward RSSI threshold | Kapalı | Açılırsa eşikten zayıf alınan mesaj hiç işlenmez: ne okunur ne aktarılır. |

**TTL örneği:** TTL = 6 ise mesaj panelden çıktıktan sonra en fazla 6 ara modül tarafından aktarılır. Panel ile
hedef daire arasında 6'dan fazla aktarma gerekiyorsa mesaj ulaşmaz. Her modül aynı mesajı yalnızca **bir kez**
aktarır (esp-now tekrar filtresi). Bu yüzden TTL'yi büyütmek trafiği artırmaz, sadece mesajın gidebileceği mesafeyi
uzatır. Küçük bir binada 10 fazlasıyla yeterlidir.

**RSSI eşiği örneği:** RSSI sıfıra yaklaştıkça sinyal güçlenir (-40 dBm, -70 dBm'den güçlüdür).
- Eşik **-70 dBm** yapılırsa zayıf alan modüller de aktarır. Daha çok aktarıcı olur, hava trafiği artar ama uzak
  noktalara ulaşma şansı da artar.
- Eşik **-40 dBm** yapılırsa yalnızca mesajı çok güçlü alan modüller aktarır. Trafik azalır ama bazı daireler
  ulaşılamaz hale gelebilir.

**Retransmit örneği:** 10 ise bir zil mesajı panelden 10 kopya halinde çıkar, her aktaran modül de 10 kopya gönderir.
3 yapılırsa hava trafiği azalır ama kayıp riski artar.

**Zayıf sinyali düşürme:** Açıksa ve eşik -55 dBm ise, mesajı -60 dBm alan bir daire modülü zili hiç duymaz. Bu
yüzden kapalı kalması önerilir.

### 1.2. Uygulama mesajları

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Application ACK timeout | 400 ms | Zil veya kapı açma komutu gönderildikten sonra "aldım" cevabının (ACK) her denemede ne kadar bekleneceği. Ölçülecek. |
| Pairing window | 20 s | Panelin ve modülün eşleştirme modunda kaldığı süre. |
| Replay counter NVS block size | 100 | Mesaj sayacının flash'a kaç mesajda bir yazıldığı. |

**ACK örneği:** 400 ms ve zil tekrarı 2 ise panel, "cevap vermiyor" göstermeden önce en fazla 3 × 400 ms = 1,2
saniye bekler. Süre çok kısa olursa geç gelen cevaplar boşuna tekrar gönderime yol açar; çok uzun olursa ulaşılamayan
dairenin hata göstermesi gecikir. Monitördeki `acknowledged after ... ms` satırları gerçek süreyi gösterir.

**Sayaç bloğu örneği:** 100 ise cihaz her 100 mesajda bir flash'a yazar. Yeniden başladığında sayaç en fazla 100
ileri atlar; bu bir sorun değildir. Küçük değer daha sık flash yazımı demektir.

### 1.3. Wi-Fi channel

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Pairing channel dwell time | 500 ms | Eşleşmemiş modülün her aday kanalda panelin cevabını bekleme süresi. Ölçülecek. |
| Candidate channel 1 / 2 / 3 | 1 / 6 / 11 | Panelin ilk açılışta seçebileceği kanallar ve modülün eşleştirmede aradığı kanallar. |

- **Bekleme örneği:** 500 ms ise modül 3 kanalı 1,5 saniyede dolaşır. Panel ilk cevaptan sonra 1,5 saniye daha
  dinleyip ikinci bir modül olup olmadığına bakar. Değeri büyütmek eşleştirmeyi yavaşlatır; çok küçültmek, panelin
  cevabı gelmeden modülün kanaldan ayrılmasına yol açabilir. 6 × bekleme süresi, eşleştirme penceresinden çok kısa
  olmalıdır.
- **Kanallar:** Panel ilk açılışta bu üç kanalı tarar ve en az Wi-Fi ağı olanı seçer. 1, 6 ve 11, 2,4 GHz bandında
  birbiriyle çakışmayan standart kanallardır. Kurulumdan sonra kanal değiştirmek için bina sıfırlaması ve tüm
  dairelerin yeniden eşleştirilmesi gerekir.

### 1.4. Wi-Fi regulatory and PHY

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Set a Wi-Fi country code | Kapalı | Açılırsa aşağıdaki ülke kodu uygulanır. Kapalıyken ESP-IDF'in varsayılan ülke ayarı kullanılır. |
| Country code | (boş) | İki harfli ülke kodu, örn. `TR`. Kullanılabilecek kanalları ve en yüksek gücü belirler. Aday kanallardan biri o ülkede izinli değilse o kanal çalışmaz. |
| Enable ESP-NOW Long Range mode | Kapalı | Espressif'in Long Range (512/256 kbps) modunu açar; daha uzun menzil hedeflenir. Katlar arası etkisi ölçülmedi. |

---

## 2. Doorbell panel (yalnızca panel)

### 2.1. GPIO
Pin numaraları, kablolama varsayılandan farklıysa değiştirilir. GPIO 6–11 flash'a ayrılmıştır. GPIO 34–39 sadece
giriştir ve dahili pull-up'ı yoktur.

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Keypad row 0–3 | 13, 14, 27, 26 | Tuş takımı satır pinleri |
| Keypad column 0–2 | 25, 33, 32 | Tuş takımı sütun pinleri |
| Door relay | 23 | Kapı kilidi rölesi |
| Door relay is driven LOW to open | Açık | Röle modülü LOW ile çekiyorsa açık kalmalı. **Yanlış ayarlanırsa kilit beklemede açık durur.** |
| Buzzer | 4 | Pasif buzzer pini |
| Status LED | 2 | Durum LED'i |
| Hidden code reset button | 19 | Gizli şifre sıfırlama butonu |
| Code reset button pulls the GPIO LOW | Açık | Buton GND'ye çekiyorsa açık |
| Enable the internal pull resistor | Açık | Dahili direnci kullanır; harici direnç varsa kapatılabilir |

### 2.2. Keypad

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Key map | `123456789*0#` | 12 karakter: satırlar yukarıdan aşağı, sütunlar soldan sağa. Tuş takımı farklı dizilmişse buradan düzeltilir. |
| Input timeout | 10 s | Bu süre tuşa basılmazsa yazılan silinir. |

### 2.3. Door and buzzer

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Door relay pulse | 500 ms | Kapı kilidinin açık tutulduğu süre. Kilit tipine göre ayarlanır. |
| Buzzer duration | 500 ms | Kapı açılırken buzzer'ın ötme süresi. **0** buzzer'ı kapatır. |
| Buzzer tone frequency | 2700 Hz | Ses tonu; buzzer'ın veri sayfasındaki rezonans frekansı en yüksek sesi verir. Ölçülecek. |
| Buzzer PWM duty | %50 | %50 kare dalgadır, genelde en yüksek sesi verir. |

### 2.4. Apartments and codes

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Lowest / Highest apartment number | 1 / 99 | Geçerli daire numarası aralığı (1–254). |
| Admin code length | 6 | Yönetici şifresinin hane sayısı (6–10). |
| User code length | 4 | Kullanıcı şifresinin hane sayısı (4–8). |

Şifre uzunluğu değişirse fabrika şifreleri de aynı uzunlukta yüklenmelidir. Flaşlama programı şifreleri formdaki
uzunluğa göre kontrol eder; eski uzunlukla derlenmiş bir firmware yüklenirse panel şifreleri kabul etmez (ADR 0014).

### 2.5. Keypad lockout

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Wrong codes before lockout | 5 | Kaç yanlış şifreden sonra kilitleneceği |
| First lockout | 60 s | İlk kilit süresi |
| Lockout multiplier | 2 | Her yeni kilitte sürenin kaç katına çıkacağı. **1** sabit süre demektir. |
| Maximum lockout | 900 s | Kilit süresinin üst sınırı (15 dakika) |

**Örnek:** 3 deneme, 30 saniye, çarpan 3 ve en fazla 600 saniye ise kilit süreleri 30, 90, 270 ve 600 saniye olur;
sonrasında hep 600 saniyede kalır. Doğru şifre seviyeyi sıfırlar.

### 2.6. Code reset button

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Minimum hold | 10 s | Bundan kısa basış şifreleri sıfırlamaz |
| Maximum hold | 60 s | Bundan uzun basış (sıkışmış buton) sıfırlamaz |

### 2.7. Diğer

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| RING retries without ACK | 2 | Cevap gelmezse zilin kaç kez daha gönderileceği. 2 ise toplam 3 gönderim yapılır. |

---

## 3. Doorbell apartment module (yalnızca daire modülü)

### 3.1. GPIO

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Bell relay | 4 | Zil rölesi |
| Bell relay is driven HIGH to ring | Açık | Röle HIGH ile çekiyorsa açık |
| Door-open button | 23 | Kapı açma butonu |
| Hidden pairing button | 18 | Gizli eşleştirme butonu |
| Hidden reset button | 19 | Gizli sıfırlama butonu |
| Buttons pull the GPIO LOW when pressed | Açık | Butonlar GND'ye çekiyorsa açık |
| Enable the internal pull resistors | Açık | Dahili dirençleri kullanır |
| Status LED | 2 | Durum LED'i |

### 3.2. Bell

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Bell relay pulse | 500 ms | Zil rölesinin çekili kaldığı süre |
| Blink the status LED while ringing | Açık | Görsel zil: zil çalarken LED hızlı yanıp söner (işitme engelliler için) |

### 3.3. Buttons

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| Door button minimum press | 100 ms | Daha kısa basışlar parazit sayılır |
| Door button maximum press | 30 s | Daha uzun basışlar sıkışmış buton sayılır |
| Pairing/reset button minimum hold | 7 s | Eşleştirme ve sıfırlama için en kısa basılı tutma |
| Pairing/reset button maximum hold | 12 s | En uzun basılı tutma; bundan sonra bırakmak işe yaramaz |
| Pairing/reset button cancel after | 15 s | Bu sürede LED üçlü flaş yapar (iptal). En fazla 65 s; en uzun basılı tutmadan büyük olmalı. |

### 3.4. Diğer

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| OPEN_DOOR retries without ACK | 1 | Cevap gelmezse kapı açma isteğinin kaç kez daha gönderileceği. 1 ise toplam 2 gönderim yapılır. |

---

## 4. Doorbell status LED (panel ve modül)

| Ayar | Varsayılan | Ne işe yarar |
| :--- | :--- | :--- |
| LED is on when the GPIO is HIGH | Açık | LED kutbu. LED ters çalışıyorsa (beklemede yanık) kapatın. Kart şemasına göre doğrulanmadı. |
| Full brightness | %100 | Tam parlaklık |
| Dim brightness | %10 | Modülün "loş, sabit" (normal) durumundaki parlaklık |

**Pattern timing:** LED desenlerinin süreleri. Kısa flaş 50 ms, hızlı yanıp sönme periyodu 200 ms, orta 500 ms,
yavaş 1000 ms (1 Hz), çift/üçlü flaş darbesi 100 ms, arası 100 ms, tekrar periyodu 1000 ms, seyrek flaş aralığı
3000 ms. Başarı 2000 ms, hata 1000 ms, cevap yok 3000 ms gösterilir. 1 Hz ve 1/2/3 saniyelik süreler SPEC'ten gelir;
diğerleri öneridir. Her adım en az bir sistem tick'i (10 ms) olmalıdır; olmazsa derleme hata verir.

---

## 5. Espressif ve ESP-IDF ayarları

Formun altındaki **ESP-NOW Configuration**, **IoT Button**, **LED Indicator** ve seçilmiş ESP-IDF ayarları Espressif'in
kendi bileşenlerine aittir. Bir sebep olmadıkça varsayılanda bırakın.

| Menü / ayar | Ne işe yarar |
| :--- | :--- |
| ESP-NOW Configuration | esp-now bileşeninin iç ayarları (görev önceliği, bellek, hata ayıklama). OTA alt menüsü bu projede kullanılmaz (ADR 0013). |
| IoT Button: period time / debounce ticks | Butonların ve tuş takımının tarama aralığı (5 ms) ve parazit filtresi (2 tarama = 10 ms). Butonlar çift algılıyorsa debounce artırılır. Uzun basma süreleri bu projede firmware'in kendi ayarlarından gelir. |
| LED Indicator | LED'in PWM frekansı ve çözünürlüğü |
| CPU frequency | İşlemci hızı |
| Brownout detector | Besleme gerilimi düşünce cihazı güvenli şekilde yeniden başlatır. **Açık kalmalı.** |
| Task / interrupt watchdog | Kilitlenen yazılımı tespit edip cihazı yeniden başlatır. Açık kalmalı. |
| Console UART / baud rate | Seri monitör portu ve hızı |
| Flash mode / frequency / size | Flash bellek ayarları. Boyut 2 MB'tır; kartla uyumlu olmalı. |
| Default log verbosity | Monitörde ne kadar ayrıntı görüneceği. Info varsayılandır; Debug çok daha fazla satır üretir. |
