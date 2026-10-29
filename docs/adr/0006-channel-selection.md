# 6. Wi-Fi kanalı kurulumda seçilir

Tarih: 2026-10-29
Durum: Kabul edildi

## Bağlam
Şartnamede kanal 1 saha verisi olmadan sabitlenmişti. Binadaki Wi-Fi ağları aynı kanalı kullanabilir.
Tüm ESP-NOW düğümleri aynı kanalda olmalıdır.

espressif/esp-now bileşeninde (kaynak: `espnow.h`, `espnow.c`):
- `frame_head.channel = ESPNOW_CHANNEL_ALL`: paket, ülke ayarındaki tüm kanallarda sırayla gönderilir.
- `CONFIG_ESPNOW_AUTO_RESTORE_CHANNEL`: gönderimden sonra asıl kanala dönülür.
- Kanal tarama veya kanal bulma fonksiyonu yoktur.

## Karar
- Kanal adayları: 1, 6, 11 (örtüşmeyen kanallar).
- Kurulumda panel `esp_wifi_scan_start` ile saha taraması yapar ve adaylardan birini seçer.
  Seçim kriteri: en az AP sayısı; eşitlikte en düşük en yüksek RSSI.
- Seçilen kanal panelde NVS'de saklanır.
- Kanal, eşleştirme sırasında şifreli olarak daire modülüne iletilir; modül NVS'de saklar.
- Kurulumdan sonra kanal değiştirilemez. Değişmesi gerekirse tüm daireler yeniden eşleştirilir.
- Panel, NVS'de kanal yoksa açılışta otomatik tarama yapar (ilk kurulum, bina sıfırlama sonrası).
- Bina sıfırlama kanalı ve uygulama anahtarını da siler.

## Açık
- Eşleşmemiş modülün panelin kanalını nasıl bulacağı (eşleştirme ADR'sinde tanımlanacak).
