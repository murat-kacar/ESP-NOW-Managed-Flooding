# ESP32 Akıllı Apartman Kapı Zili ve Erişim Kontrol Sistemi — Şartname

Sürüm: 2026-10-29
Kaynaklar: ilk proje şartnamesi + `docs/adr/0001`–`0008`

> Bu belgedeki her karar ilgili ADR'ye bağlıdır. Ölçüm veya testle doğrulanmamış değerler **[DOĞRULANACAK]** olarak işaretlidir ve Bölüm 9'da listelenir.

---

## 1. Mimari

- Haberleşme: ESP-NOW, Espressif'in resmi `espressif/esp-now` bileşeni (v2.5.3, ESP-IDF ≥ 4.4) ile. Özel flooding motoru yazılmaz. ([ADR 0001](adr/0001-espressif-esp-now-component.md))
- Bağlantısızdır: düğümler arasında AP-STA bağlantısı yoktur.
- Çok atlamalı iletim bileşenin forwarding özelliğiyle yapılır (yalnızca broadcast paketler iletilir).
- Hedef gecikme bütçesi: en uzak daireye 2 saniye. Gerçek gecikme **[DOĞRULANACAK]**.

### 1.1. Topoloji
- Binada 1 dış panel, 1–99 daire modülü.
- Tüm düğümler aynı Wi-Fi kanalındadır ([ADR 0006](adr/0006-channel-selection.md)).
- Panel tek merkezi otoritedir; panel çalışmazsa sistem çalışmaz.
- Katlar arası sinyal geçişi ve ara düğüm kaybında yolun devam edip etmediği **[DOĞRULANACAK]**.

---

## 2. Donanım ve GPIO Haritası

Her iki modülde **NodeMCU-32S (ESP32-WROOM-32)**, çip revizyonu **v3** (`esptool.py chip_id` ile doğrulandı).

### 2.1. Dış Modül (Bina Giriş Paneli)
Binada yalnızca 1 adet bulunur.

| Fonksiyon | GPIO | Detay |
| :--- | :---: | :--- |
| Tuş takımı satırları | 13, 14, 27, 26 | R0–R3 sırasıyla (3x4 matris). Pin 13-16 sırası. |
| Tuş takımı sütunları | 25, 33, 32 | C0–C2 sırasıyla. |
| Kapı rölesi | 23 | Active-LOW, boşta HIGH. 3.3V ile çalışan optokuplörlü röle modülü. ([ADR 0005](adr/0005-door-relay-output.md)) |
| Buzzer | 4 | Pasif buzzer, LEDC PWM. Yalnızca kapı açılırken çalar. |
| Durum LED'i | 2 | Kart üzerindeki LED, LEDC ile parlaklık. |
| Gizli kod sıfırlama butonu | 19 | Dahili pull-up, basınca GND. Kapak içinde. |
| Boş | 18, 21, 22 | İleride kullanım için. |
| Besleme | — | 5V / 1A DC adaptör. Röle bobini 3.3V hattından ~120 mA çeker. |

### 2.2. İç Modül (Daire Ünitesi)
Her dairede 1 adet; 1–99 daire.

| Fonksiyon | GPIO | Detay |
| :--- | :---: | :--- |
| Zil rölesi | 4 | Active-HIGH. Harici pull-down önerilir. |
| Kapı açma butonu | 23 | Dahili pull-up, basınca GND. 100 ms altı ve 30 sn üstü yok sayılır. Bırakışta tetiklenir. |
| Gizli eşleştirme butonu | 18 | Dahili pull-up. 7–12 sn basılı tutup bırakınca eşleştirme moduna girer. |
| Gizli sıfırlama butonu | 19 | Dahili pull-up. 7–12 sn basılı tutup bırakınca kimliği siler. |
| Durum LED'i | 2 | Kart üzerindeki LED, LEDC ile parlaklık. |
| Boş | 13, 14, 21, 22, 25, 26, 27, 32, 33 | |

NodeMCU-32S'te GPIO 18, GPIO 19 ve GND yan yanadır; iki gizli buton tek GND hattını paylaşır.

### 2.3. Kapı rölesi kuralları ([ADR 0005](adr/0005-door-relay-output.md))
- Sistemin sorumluluğu yalnızca GPIO 23'ü sürmektir; kilit tipi kurulum tarafının sorumluluğundadır.
- Kapı açma: GPIO 23 **0.5 sn LOW**, sonra HIGH. Süre `esp_timer` one-shot ile yönetilir.
- Buzzer röleyle aynı anda başlar, 0.5 sn sonra durur.
- Darbe sürerken gelen ikinci kapı açma komutu yok sayılır.
- Başlatmada çıkış seviyesi önce HIGH yapılır, sonra pin çıkış moduna alınır.
- Elektrik kesintisinde kapı kapalı kalmalıdır. Güç verme, reset ve flaşlamada röle çekmemelidir **[DOĞRULANACAK]**.

---

## 3. Kullanım

### 3.1. Ziyaretçi zil çalar
```text
<daire_no> #        Örnek: 12 #
```
- Daire numaraları 1–99.
- Daire modülü zil rölesine 0.5 sn darbe verir ve panele ACK döner.
- Panel LED'i 2 sn sabit yanar.

### 3.2. Sakin kapıyı açar
- Daire modülündeki kapı açma butonuna basıp bırakır.
- Panel kapı rölesini ve buzzer'ı 0.5 sn sürer (Bölüm 2.3).

### 3.3. Panelden kodla kapı açma
```text
* <kullanıcı_kodu> #
```
Tüm binada tek kullanıcı kodu vardır ([ADR 0008](adr/0008-single-building-user-code.md)).

### 3.4. Tuşlamayı iptal
```text
* #
```
10 sn işlem yapılmazsa tuşlama kendiliğinden sıfırlanır.

---

## 4. Yönetim ve Kodlar

| Kod | Uzunluk | Fabrika değeri | Yetki |
| :--- | :---: | :--- | :--- |
| Kullanıcı kodu | 4 hane | Cihaza özel, etikette | Kapıyı açar |
| Yönetici kodu | 6 hane | Cihaza özel, etikette | Yönetim ve eşleştirme |

- Fabrika kodları üretimde rastgele üretilir, `mfg_gen.py` ile salt okunur fabrika NVS bölümüne yazılır ve etikete basılır ([ADR 0003](adr/0003-per-device-passwords.md)).
- Kullanıcı kodunu değiştirme: `* <yönetici> * 3 * <yeni:4> * <yeni:4> #`
- Yönetici kodunu değiştirme: `* <yönetici> * 4 * <yeni:6> * <yeni:6> #`

### 4.1. Hatalı giriş koruması
- Üst üste 5 yanlış kodda `*` ile başlayan girişler 60 sn kilitlenir.
- Hata sürerse süre katlanır (120, 240 sn…), en fazla 15 dk.
- Kilit sırasında zil çalma asla engellenmez.

---

## 5. Eşleştirme ([ADR 0007](adr/0007-pairing-near-panel.md))

Eşleştirme **panelin yanında** yapılır. espnow_sec anahtar değişimi yalnızca panelin doğrudan radyo menzilinde çalışır.

### 5.1. Akış
1. **Panelde:** Yönetici komutu tuşlanır:
   ```text
   * <yönetici> * 1 * <daire_no> #
   ```
   Panel LED'i tekrarlayan çift flaş yapar; 20 sn eşleştirme penceresi açılır. Panel kendi kanalında kalır ve pencere boyunca `espnow_sec_initiator_scan` ile tarar.
2. **Modülde (panelin yanında):** Montajcı gizli eşleştirme butonunu (GPIO 18) 7–12 sn basılı tutup bırakır. Modül espnow_sec responder olarak başlar ve 1, 6, 11 kanallarında sırayla dinler.
3. **Panel tarama sonucu:**
   - Tam olarak 1 eşleşmemiş modül bulunursa: espnow_sec el sıkışması yapılır; ardından daire numarası ve kanal şifreli olarak modüle gönderilir. Modül bunları NVS'ye yazar.
   - Birden fazla eşleşmemiş modül bulunursa: eşleştirme iptal edilir, panel hata gösterir.
   - Hiç bulunamazsa: pencere sonunda hata gösterilir.
4. **Onay:**
   - Panel LED'i 2 sn sabit yanar; kayıt NVS'ye yazılır.
   - Modül LED'i 2 sn tam parlaklıkta yanar, zil rölesine 0.5 sn darbe verir (zil bağlıysa duyulur), sonra normal duruma geçer.
   - Modül 20 sn içinde eşleşmezse eşleşmemiş duruma döner.
5. Montajcı modülü daireye takar.

### 5.2. Kurallar
- PoP kullanılmaz (kabul edilen risk, ADR 0007).
- Dolu daire numarasının üzerine yazılmaz; istek reddedilir.
- Eşleşmiş modül sıfırlanmadan yeniden eşleştirilemez (responder yapılandırıldıktan sonra yeni el sıkışmayı yok sayar).
- Daire kaydını silme: `* <yönetici> * 2 * <daire> * <daire> #`. Silmede o dairenin replay sayacı kaydı da silinir.

---

## 6. Sıfırlama

1. **Panel kodlarını sıfırlama:** Panel kapağındaki gizli butona (GPIO 19) 10–60 sn basılı tutup bırakılır. Normal NVS'deki kodlar silinir; panel etiketteki fabrika kodlarına döner. Daire kayıtlarına dokunulmaz.
2. **İç modülü sıfırlama:** Gizli sıfırlama butonu (GPIO 19) 7–12 sn basılı tutulup bırakılır. Daire kimliği, kanal, anahtar ve sayaç NVS'den silinir; modül eşleşmemiş duruma döner.
3. **Tüm binayı sıfırlama:** `* <yönetici> * 6 * <yönetici> #` — tüm daire kayıtları, sayaçlar, kanal ve uygulama anahtarı silinir. Panel yeniden başlar ve kanal taraması yapar (Bölüm 7.5).

---

## 7. Haberleşme

### 7.1. Bileşen yapılandırması ([ADR 0001](adr/0001-espressif-esp-now-component.md))
- `frame_head.broadcast = true` (iletim yalnızca broadcast pakette çalışır)
- `frame_head.security = true`
- `frame_head.magic`: rastgele (bileşen varsayılanı)
- `forward_ttl` ≤ 30 (31 = sınırsız). Değer **[DOĞRULANACAK]**.
- `forward_rssi` / `filter_weak_signal`: değer **[DOĞRULANACAK]**.
- `esp_wifi_set_ps(WIFI_PS_NONE)`
- Alım işleyicileri bloklamaz; uzun işler kuyruğa atılıp ayrı görevde yapılır (ESP-IDF ESP-NOW dokümanı).

### 7.2. Uygulama mesajı
Şifreli payload içinde taşınır:

```c
typedef enum {
    DB_OP_RING          = 1,  // Panel -> Daire
    DB_OP_RING_ACK      = 2,  // Daire -> Panel
    DB_OP_OPEN_DOOR     = 3,  // Daire -> Panel
    DB_OP_OPEN_DOOR_ACK = 4,  // Panel -> Daire
} db_opcode_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;    // Mesaj formatı sürümü
    uint8_t  opcode;     // db_opcode_t
    uint8_t  apartment;  // Hedef veya kaynak daire (1-99)
    uint32_t counter;    // Replay sayacı (ADR 0002)
} db_msg_t;
```

Eşleştirme mesajları espnow_sec el sıkışmasıyla değiştirildi; ayrı opcode'ları yoktur. El sıkışma sonrası gönderilen yapılandırma mesajının (daire no, kanal) formatı kodlama aşamasında tanımlanacak.

### 7.3. Replay koruması ([ADR 0002](adr/0002-replay-protection-counter.md))
- Her gönderici her mesajda sayacı 1 artırır.
- Alıcı, gönderici başına son kabul edilen sayacı tutar; küçük veya eşit olanı reddeder.
- Sayaçlar NVS'de, blok rezervasyonuyla saklanır. Blok boyutu **[DOĞRULANACAK]**.

### 7.4. Uygulama ACK'leri ve tekrar
- Panel `RING` gönderir, `RING_ACK` bekler; gelmezse 2 kez daha gönderir.
- Daire `OPEN_DOOR` gönderir, `OPEN_DOOR_ACK` bekler; gelmezse 1 kez daha gönderir.
- Tekrar gönderimde aynı sayaç kullanılır; alıcı eylemi tekrarlamaz, yalnızca ACK'i yeniden gönderir.
- ACK zaman aşımı **[DOĞRULANACAK]**. İlk şartnamedeki 400 ms, belgede verilen tek yön gecikmesiyle (250–300 ms) çelişiyordu.

### 7.5. Kanal ([ADR 0006](adr/0006-channel-selection.md))
- Adaylar: 1, 6, 11.
- Panel, NVS'de kanal yoksa (ilk kurulum veya bina sıfırlama sonrası) açılışta otomatik saha taraması yapar; en az AP sayısı, eşitlikte en düşük en yüksek RSSI olan kanalı seçer ve NVS'ye yazar.
- Kanal eşleştirmede modüle iletilir.
- Kurulumdan sonra kanal değiştirilemez; değişmesi gerekirse tüm daireler yeniden eşleştirilir.

### 7.6. RF gücü
- Laboratuvar: 10 dBm (`esp_wifi_set_max_tx_power(40)`)
- Saha: 20 dBm (`esp_wifi_set_max_tx_power(80)`)

---

## 8. Yazılım ve Dağıtım ([ADR 0004](adr/0004-secure-boot-flash-encryption.md))

| | Geliştirme (`sdkconfig.defaults`) | Üretim (`sdkconfig.defaults.prod`) |
|---|---|---|
| `CONFIG_ESP32_REV_MIN` | v3.0 | v3.0 |
| Secure Boot v2 | Kapalı | Açık |
| Flash Encryption | Kapalı | Açık, Release modu |
| NVS Encryption | Kapalı | Açık (`nvs_keys`) |
| eFuse yakma | Yok | Üretim hattında |
| Flaşlama | Birleşik imaj, `0x0` adresinden (`builds/<rol>_YYYY-MM-DD_HH-MM-SS.bin`) | Ayrıca tanımlanacak |

- Geliştirme aşamasında hiçbir eFuse yakılmaz; her değişiklik geri alınabilir.
- Birleşik imaj `tools/build.py` ile üretilir: her rol için `idf.py build` ve `idf.py merge-bin` (raw format) çalıştırılır.
  esptool raw formatta dosyalar arasındaki boşlukları `0xFF` ile doldurur (esptool 5.6.0 kaynak kodu); bu yüzden `0x0`'dan flaşlama NVS bölgesini de siler.
- OTA: `espressif/esp-now` OTA modülü. Üretimde imzalı imaj.
- Bölüm tablosu: OTA bölümleri, fabrika NVS bölümü (`mfg_gen.py`), üretimde `nvs_keys`.

---

## 9. Doğrulanacaklar

| # | Konu | Yöntem |
|---|---|---|
| 1 | espnow_sec el sıkışmasının tek atlamalı olması | Tezgâh testi |
| 2 | espnow_sec PoP parametresinin NULL kabul edip etmediği | Kaynak kod / tezgâh |
| 3 | Kanal gezmede kanal başına bekleme süresi | Tezgâh testi |
| 4 | Başka anahtarla şifrelenmiş paketlerin iletilip iletilmediği | Tezgâh testi |
| 5 | espnow_sec uygulama anahtarının nasıl üretildiği | Kaynak kod |
| 6 | Uçtan uca gecikme ve ACK zaman aşımı | Saha ölçümü |
| 7 | `forward_ttl` ve `forward_rssi` değerleri | Saha ölçümü |
| 8 | Katlar arası sinyal geçişi, ara düğüm kaybında iletim | Saha ölçümü |
| 9 | Güç verme / reset / flaşlamada kapı rölesinin çekmemesi | Ölçüm |
| 10 | Röle çekerken 3.3V hattındaki düşüş | Ölçüm |
| 11 | Replay sayacı blok boyutu | Kullanım verisi |

## 10. Tanımlanacaklar

- Eşleştirme sonrası yapılandırma mesajının formatı (kodlama aşamasında).
- Üretim flaşlama akışı (üretim aşamasında).

---

## 11. LED Durumları

### 11.1. Dış panel
| Desen | Anlamı |
| :--- | :--- |
| Sönük | Boşta, hazır |
| Tek kısa flaş | Tuşa basıldı |
| Yavaş yanıp sönme (1 Hz) | Komut giriliyor |
| 2 sn sabit | Başarılı / zil çaldı / eşleşti |
| Röle süresince sabit + buzzer (0.5 sn) | Kapı açıldı |
| 1 sn hızlı yanıp sönme | Hata / geçersiz kod / reddedildi / daire dolu / birden fazla modül bulundu |
| 3 sn orta hızda yanıp sönme | Daire kayıtlı ama yanıt vermiyor |
| Tekrarlayan çift flaş | 20 sn eşleştirme penceresi açık |
| Seyrek tek flaşlar | Tuş takımı hatalı kod nedeniyle kilitli |

### 11.2. İç modül
| Desen | Anlamı |
| :--- | :--- |
| Tekrarlayan çift flaş | Eşleşmemiş |
| Kısık ve sabit | Normal, eşleşmiş |
| Hızlı yanıp sönme | Zil çalıyor (görsel zil) |
| Orta hızda yanıp sönme | Eşleştirme butonu tutuluyor (7–12 sn'de bırakın) |
| Yavaş yanıp sönme (1 Hz) | Eşleştirme modu: panelin kanalı aranıyor |
| 2 sn tam parlaklık | Eşleşme başarılı |
| Sabit, arada göz kırpar | Sıfırlama butonu tutuluyor (7–12 sn'de bırakın) |
| Tekrarlayan üçlü flaş | Buton 15 sn'den uzun tutuldu; iptal |
