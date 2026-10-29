# 8. Tek bina kullanıcı kodu

Tarih: 2026-10-29
Durum: Kabul edildi

## Karar
Panelden kapı açma için tüm binada tek bir 4 haneli kullanıcı kodu kullanılır: `* kod #`.
Kodun fabrika değeri cihaza özeldir (ADR 0003); yönetici değiştirir.

## Değerlendirilen alternatif
Daire başına kod (`* daire * kod #`): sızıntı tek daireyle sınırlı kalır ve iz bırakır. Kullanıcı tercihiyle reddedildi.

## Kabul edilen risk
- Kod sızarsa tüm bina için değiştirilmeli ve tüm sakinlere duyurulmalıdır.
- Kapıyı kimin açtığı bilinmez.
