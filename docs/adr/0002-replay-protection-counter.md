# 2. Replay koruması: monoton sayaç

Tarih: 2026-10-29
Durum: Kabul edildi

## Bağlam
espressif/esp-now yalnızca son 32 paketi filtreler. Daha eski şifreli paketler kaydedilip yeniden gönderilebilir (ör. `OPEN_DOOR`).

## Karar
BLE Mesh SEQ / Zigbee / IEEE 802.15.4 frame counter paterni uygulanır:
- Her gönderici her mesajda sayacı 1 artırır; sayaç şifreli payload içindedir.
- Alıcı, gönderici başına son kabul edilen sayacı tutar; küçük veya eşit olanı reddeder.
- Sayaçlar NVS'de saklanır. Flaş yıpranmasına karşı blok rezervasyonu kullanılır (blok boyutu kullanım verisine göre belirlenecek).

## Değerlendirilen alternatif
Challenge-response: kalıcı veri gerektirmez, ancak 3 mesaj ve ~3 kat gecikme getirir.

## Sonuçlar
- Panel her daire için, her daire modülü panel için son sayacı NVS'de tutar.
- Eşleştirme silindiğinde ilgili sayaç kaydı da silinir.
