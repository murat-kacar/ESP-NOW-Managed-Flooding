# 7. Eşleştirme panelin yanında yapılır

Tarih: 2026-10-29
Durum: Kabul edildi

## Bağlam
espressif/esp-now kaynak kodundan:
- Bileşen yalnızca `broadcast = true` paketleri iletir (forward).
- espnow_sec el sıkışmasında Command 0'dan sonraki tüm paketler unicast'tir (`broadcast = false`), dolayısıyla iletilmez.
- `CONFIG_ESPNOW_SEC_SEND_FORWARD_TTL` varsayılanı 0'dır.
- Initiator ve responder kanal değiştirmez; ikisi aynı kanalda olmalıdır.
- Responder, el sıkışmayı başlatan ilk initiator'ı kabul eder; yapılandırıldıktan sonra yeni el sıkışmalarını yok sayar.

Sonuç: anahtar değişimi yalnızca panelin doğrudan radyo menzilinde çalışır. (Tezgâh testiyle doğrulanacak.)

## Karar
- Montajcı modülü panelin yanına getirir, eşleştirir, sonra daireye takar.
- espnow_sec bileşeni değiştirilmeden kullanılır: panel initiator, daire modülü responder.

## Değerlendirilen alternatifler
- Bileşeni çok atlamalı el sıkışma için değiştirmek: resmi koddan sapma.
- Özel anahtar değişimi yazmak: özel kriptografi, reddedildi.

## Sonuçlar
- Eşleştirme fiziksel yakınlık gerektirir; uzaktan sahte cihaz eşleştirilemez.
- Şartnamedeki daire içinden eşleştirme akışı (Bölüm 3.3) buna göre yeniden yazılacak.

- Kanal bulma: eşleşmemiş modül 1, 6, 11 kanallarında sırayla dinler (`esp_wifi_set_channel`, uygulama kodunda).
  Panel kendi kanalında kalır ve eşleştirme penceresi boyunca `espnow_sec_initiator_scan` ile tarar.
  Modül, el sıkışma tamamlandığında bulunduğu kanalı NVS'ye yazar. Kanal başına bekleme süresi tezgâh testiyle belirlenecek.

- PoP kullanılmaz. Bileşen bir PoP değeri gerektiriyorsa, tüm cihazlarda aynı ve gizli olmayan sabit bir değer verilir
  (NULL desteği doğrulanacak). Koruma fiziksel yakınlık ve 20 saniyelik eşleştirme penceresine dayanır.

## Kabul edilen risk
- Eşleştirme penceresi açıkken panelin menzilindeki herhangi bir eşleşmemiş modül eşleşebilir.
- Değerlendirilen alternatif: cihaza özel, etikete basılı PoP (Espressif Wi-Fi provisioning yaklaşımı). Kullanıcı tercihiyle reddedildi.

- Risk azaltma: tarama birden fazla eşleşmemiş modül bulursa eşleştirme iptal edilir ve panel hata gösterir.
