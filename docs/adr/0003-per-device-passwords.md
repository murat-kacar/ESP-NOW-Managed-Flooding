# 3. Cihaza özel varsayılan şifreler

Tarih: 2026-10-29
Durum: Kabul edildi

## Bağlam
Şartnamede tüm panellerde aynı fabrika kodları vardı (`1234`, `123456`).
ETSI EN 303 645 madde 5.1-1 evrensel varsayılan şifreyi yasaklar.

## Karar
- Her panele üretimde rastgele kullanıcı kodu (4 hane) ve yönetici kodu (6 hane) üretilir ve etikete basılır.
- Kodlar ESP-IDF Manufacturing Utility (`mfg_gen.py`) ile ayrı, salt okunur bir fabrika NVS bölümüne yazılır.
- Kullanıcının değiştirdiği kodlar normal NVS bölümünde tutulur.
- Panel kod sıfırlama (GPIO 19) yalnızca normal NVS'deki kodları siler; panel fabrika bölümündeki etiket kodlarına döner.

## Değerlendirilen alternatif
İlk kurulumda şifre belirleme: üretimde ek iş gerektirmez, ancak kurulum yarım kalırsa panel kurulum modunda kalır.

## Sonuçlar
- Üretim hattında kod üretme ve etiket basma adımı gerekir.
- Fabrika bölümündeki kodların flaştan okunamaması için Flash Encryption ve NVS Encryption gerekir (açık madde 12).
