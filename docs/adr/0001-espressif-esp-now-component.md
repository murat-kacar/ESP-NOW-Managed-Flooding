# 1. Haberleşme çekirdeği: espressif/esp-now bileşeni

Tarih: 2026-10-29
Durum: Kabul edildi

## Bağlam
Şartnamede flooding motoru (`db_mesh_pkt_t`, dedup ring, jitter, TTL, ACK) sıfırdan yazılacaktı.
Espressif'in resmi `espressif/esp-now` bileşeni (v2.5.3, ESP-IDF >= 4.4, Apache-2.0) bunları sağlıyor.

## Karar
Haberleşme `espressif/esp-now` üzerine kurulur. Kullanılacak özellikler:
- Forwarding: `forward_ttl`, `forward_rssi`, `filter_weak_signal`
- Tekrar filtresi: `magic` (rastgele, 32 girişlik önbellek)
- Güvenilirlik: `ack`, `retransmit_count`
- Güvenlik: espnow_sec (ECDH + PoP ile anahtar dağıtımı, AES128-CCM, paket başına rastgele IV)
- OTA: bileşenin OTA modülü

## Sonuçlar
- Özel flooding kodu, CRC-16, jitter ve `seq` alanı kaldırılır.
- Bileşende replay koruması yoktur; uygulama katmanında eklenir (bkz. ADR 0002).
- PoP varsayılanı (`espnow_pop`) kullanılmaz.
- `forward_ttl` en fazla 30 olabilir: `ESPNOW_FORWARD_MAX_COUNT` (31) değerinde TTL azaltılmaz, yani sınırsız iletim olur.
- Açık: başka anahtarla şifrelenmiş paketlerin iletilip iletilmediği testle doğrulanacak.
