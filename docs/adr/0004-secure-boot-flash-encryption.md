# 4. Secure Boot v2, Flash Encryption ve NVS Encryption

Tarih: 2026-10-29
Durum: Kabul edildi

## Bağlam
Flaşta cihaza özel kodlar (ADR 0003) ve espnow_sec anahtarları tutulur. Koruma olmadan flaş okunarak bunlara ulaşılabilir.
Kullanılan kartların çip revizyonu: ESP32 v3 (kullanıcı tarafından `esptool.py chip_id` ile doğrulandı).
Espressif dokümanı: Secure Boot v2, ESP32'de v3.0 ve sonrasında desteklenir.

Proje geliştirme ve test aşamasındadır; bu aşamada her değişiklik geri alınabilir olmalıdır.
Espressif dokümanına göre Flash Encryption Development modu bile geri alınamaz eFuse işlemleri yapar:
anahtar bir kez yazılır ve korunur, JTAG kapatılır, şifreleme çip başına yalnızca 3 kez kapatılabilir.

## Karar
İki ayrı yapılandırma profili kullanılır (ESP-IDF `SDKCONFIG_DEFAULTS` ile):

| | Geliştirme (`sdkconfig.defaults`) | Üretim (`sdkconfig.defaults.prod`) |
|---|---|---|
| `CONFIG_ESP32_REV_MIN` | v3.0 | v3.0 |
| Secure Boot v2 | Kapalı | Açık (RSA-PSS imzalı imajlar) |
| Flash Encryption | Kapalı | Açık, Release modu |
| NVS Encryption | Kapalı | Açık (flash encryption tabanlı `nvs_keys`; ESP32'de HMAC çevre birimi yok) |
| eFuse yakma | Yok | Üretim hattında |

## Sonuçlar
- Geliştirme kartlarında hiçbir eFuse yakılmaz; tüm değişiklikler geri alınabilir.
- Üretim profili ayrıca, eFuse yakılmadan önce ayrılmış test kartlarında doğrulanır.
- Üretimde yalnızca v3.0 ve üstü ESP32 kullanılabilir.
- OTA imajları üretimde imzalı olmalıdır; imzalama anahtarı güvenli ortamda saklanır.
- Bölüm 5'teki "0x0'dan birleşik imaj flaşlama" yöntemi yalnızca geliştirme profilinde geçerlidir; üretim flaşlama akışı ayrıca tanımlanacak.
