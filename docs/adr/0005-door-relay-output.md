# 5. Kapı rölesi çıkışı (Dış panel, GPIO 23)

Tarih: 2026-10-29
Durum: Kabul edildi

## Bağlam
Kapı kilidi tipi kurulum yerine göre değişir ve sistemin sorumluluğu dışındadır.
Sistemin tek sorumluluğu: 3.3V ile çalışan, LOW ile sürülen optokuplörlü röle modülünü sürmek.
Elektrik kesintisinde kapı kapalı kalmalıdır.

## Karar
- GPIO 23, active-LOW. Boşta HIGH.
- Kapı açma komutu geldiğinde GPIO 23, 0.5 saniye LOW tutulur, sonra HIGH'a çekilir.
- Süre `esp_timer` one-shot zamanlayıcısıyla yönetilir; görev bloklanmaz.
- Başlatmada çıkış seviyesi önce HIGH yapılır, ardından pin çıkış moduna alınır; açılışta LOW darbesi oluşmaz.
- Bir çökme veya watchdog reset olursa pin reset durumuna döner ve röle bırakılır.
- Buzzer (GPIO 4) röleyle aynı anda başlar ve aynı zamanlayıcıyla 0.5 saniye sonra durur.

## Doğrulanacaklar (ölçümle)
- Güç verme, reset ve flaşlama sırasında rölenin hiç çekmediği ölçülecek. Çekerse GPIO 23'e harici pull-up eklenecek.
- Röle çekerken 3.3V hattındaki düşüş ölçülecek (brownout riski).

- Darbe sürerken gelen ikinci kapı açma komutu yok sayılır; süre uzatılmaz.
