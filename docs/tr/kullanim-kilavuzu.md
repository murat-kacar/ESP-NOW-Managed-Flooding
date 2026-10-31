# Kullanım Kılavuzu

Apartman zili ve kapı açma sistemi. Bu kılavuzdaki süreler ve şifre uzunlukları **varsayılan** değerlerdir;
kurulumda farklı ayarlanmış olabilir (bkz. [Yapılandırma Ayarları](yapilandirma-ayarlari.md)).

---

## 1. Sistem

| Cihaz | Yeri | Görevi |
| :--- | :--- | :--- |
| **Panel** | Bina girişi (binada bir tane) | Tuş takımı, kapı kilidi rölesi, buzzer, durum LED'i |
| **Daire modülü** | Her dairede bir tane | Zil rölesi, kapı açma butonu, gizli eşleştirme ve sıfırlama butonları, durum LED'i |

- Cihazlar birbirleriyle kablosuz (ESP-NOW) haberleşir. Mesajlar dairelerin modülleri üzerinden aktarılarak panelden
  uzak katlara ulaşır.
- Sistem kapalıdır: internete, buluta veya bir Wi-Fi ağına hiç bağlanmaz.
- Elektrik kesildiğinde kapı kilitli kalır.

---

## 2. Ziyaretçi: zil çalma

Panelde daire numarasını yazıp `#` tuşuna basın:

```text
12 #
```

| Panel LED'i | Anlamı |
| :--- | :--- |
| 2 saniye sabit yanar | Zil çaldı |
| 3 saniye orta hızda yanıp söner | Daire kayıtlı ama modül cevap vermedi |
| 1 saniye hızlı yanıp söner | Geçersiz numara veya bu numarada kayıtlı daire yok |

- Daire numaraları 1–99 arasıdır.
- Zil, yanlış şifre kilidi sırasında da çalışır.

---

## 3. Sakin: kapıyı açma

### 3.1. Daireden
Daire modülündeki **kapı açma butonuna** basıp bırakın. Kapı kilidi 0,5 saniye açılır, paneldeki buzzer öter.

- Buton bırakıldığında çalışır.
- 0,1 saniyeden kısa veya 30 saniyeden uzun basışlar dikkate alınmaz (parazit veya sıkışmış buton).

### 3.2. Panelden şifreyle
```text
* <kullanıcı şifresi> #        Örnek: * 1234 #
```
- Kullanıcı şifresi binanın tamamı için tektir (4 hane).
- Kapı açıkken (0,5 saniye) gelen ikinci açma komutu yok sayılır.

### 3.3. Yazdığınızı iptal etme
```text
* #
```
Hiç tuşa basılmazsa yazılan 10 saniye sonra kendiliğinden silinir.

---

## 4. Yönetici işlemleri

Yönetici şifresi 6 hanedir. Tüm komutlar `*` ile başlar, `#` ile biter.

| İşlem | Komut |
| :--- | :--- |
| Kullanıcı şifresini değiştirme | `* <yönetici> * 3 * <yeni 4 hane> * <yeni 4 hane> #` |
| Yönetici şifresini değiştirme | `* <yönetici> * 4 * <yeni 6 hane> * <yeni 6 hane> #` |
| Daire eşleştirme | `* <yönetici> * 1 * <daire no> #` (bölüm 5) |
| Daire kaydını silme | `* <yönetici> * 2 * <daire no> * <daire no> #` |
| Bina sıfırlama | `* <yönetici> * 6 * <yönetici> #` (bölüm 6.3) |

- Başarılı işlemde panel LED'i 2 saniye sabit yanar; hatalı komutta 1 saniye hızlı yanıp söner.
- **Fabrika şifreleri** cihaz etiketinde yazar. Kurulumdan sonra ikisini de değiştirin.

### 4.1. Yanlış şifre kilidi
- Üst üste 5 yanlış şifreden sonra `*` ile başlayan girişler 60 saniye kilitlenir. LED seyrek tek flaşlar yapar.
- Hatalar sürerse kilit süresi her seferinde iki katına çıkar (120, 240 saniye …), en fazla 15 dakika.
- Doğru şifre girilince kilit seviyesi sıfırlanır.
- Kilit sırasında zil çalınabilir.
- Yanlış yazılmış giriş (örneğin `**#` ya da `* 1234 * #`) hata gösterir ama yanlış şifre sayılmaz.

---

## 5. Kurulum: daire modülünü eşleştirme

Eşleştirme **panelin yanında** yapılır; modül daireye eşleştirmeden sonra takılır.

1. **Panelde:** `* <yönetici> * 1 * <daire no> #` yazın.
   Panel LED'i tekrarlayan çift flaş yapar; 20 saniyelik eşleştirme penceresi açılır.
2. **Modülde (panelin yanında):** Gizli **eşleştirme butonunu** basılı tutun.
   - 7. saniyede LED orta hızda yanıp sönmeye başlar. **Bu sırada bırakın** (7–12 saniye arası).
   - 12 saniyeyi geçerse bırakmak işe yaramaz; 15 saniyede LED üçlü flaş yapar ve işlem iptal olur.
   - Bıraktığınızda LED yavaş (1 Hz) yanıp söner: modül panelin kanalını arıyor.
3. **Sonuç:**

| Durum | Panel | Modül |
| :--- | :--- | :--- |
| Başarılı | LED 2 saniye sabit yanar | LED 2 saniye tam parlak yanar, zil bir kez çalar, sonra LED loş sabit kalır |
| Birden fazla eşleşmemiş modül bulundu | LED 1 saniye hızlı yanıp söner; eşleştirme iptal | Pencere bitince eşleşmemiş duruma döner |
| Modül bulunamadı (20 saniye doldu) | LED 1 saniye hızlı yanıp söner | Eşleşmemiş duruma döner |
| Daire numarası dolu | LED 1 saniye hızlı yanıp söner | — |

4. Modülü daireye takın.

**Kurallar:**
- Dolu bir daire numarasının üzerine yazılmaz; önce o kaydı silin.
- Eşleşmiş bir modül, sıfırlanmadan tekrar eşleştirilemez (bölüm 6.2).
- Aynı anda yalnızca **bir** modül eşleştirme modunda olmalıdır.

---

## 6. Sıfırlama

### 6.1. Panel şifre sıfırlama
Panel kutusunun içindeki gizli butonu **10–60 saniye** basılı tutup bırakın. Değiştirilmiş şifreler silinir; panel
etiketteki fabrika şifrelerine döner. Daire kayıtları korunur.

### 6.2. Daire modülü sıfırlama
Modüldeki gizli **sıfırlama butonunu** **7–12 saniye** basılı tutup bırakın.
- 7. saniyede LED sabit yanmaya başlar (arada kısa söner). **Bu sırada bırakın** (7–12 saniye arası); 15 saniyede
  LED üçlü flaş yapar ve işlem iptal olur.
- Modülün eşleştirme bilgileri silinir, modül yeniden başlar ve eşleşmemiş duruma döner.
- Paneldeki daire kaydı silinmez. Aynı daire numarasıyla yeniden eşleştirmek için önce panelden kaydı silin
  (bölüm 4). Modülü **başka** bir numarayla eşleştirirseniz eski kaydı otomatik olarak silinir.

### 6.3. Bina sıfırlama
```text
* <yönetici> * 6 * <yönetici> #
```
Tüm daire kayıtları, kanal ve şifreleme anahtarı silinir; şifreler korunur. Panel yeniden başlar ve yeni bir kanal
seçer. **Bütün daire modülleri sıfırlanıp yeniden eşleştirilmelidir.**

### 6.4. Yeniden başlatma (hiçbir şey silinmez)
- Cihaz kilitlenirse kendini en geç 30 saniye içinde yeniden başlatır; ne olduğu panelin monitöründe görünür
  ("last restart: task watchdog" gibi).
- Panel uzun süre hiçbir modülden rapor alamazsa (varsayılan 3 saat) kendini yeniden başlatır. Durum sürerse bekleme
  süresi her seferinde iki katına çıkar (en fazla 48 saat).
- Elle yeniden başlatmak için kart üzerindeki **EN** butonuna basın ya da elektriğini kesip açın. Kutunun içine EN ile
  GND arasına gizli bir buton da bağlanabilir. Kayıtlar, şifreler ve eşleştirme bilgileri korunur.

---

## 7. LED durumları

### 7.1. Panel
| LED | Anlamı |
| :--- | :--- |
| Sönük | Beklemede, hazır |
| Tek kısa flaş | Tuşa basıldı |
| Yavaş yanıp sönme (1 Hz) | Komut yazılıyor |
| 2 saniye sabit | Başarılı / zil çaldı / eşleşti |
| Kapı açılırken sabit (0,5 saniye, buzzer ile) | Kapı açıldı |
| 1 saniye hızlı yanıp sönme | Hata / yanlış şifre / reddedildi / daire dolu / birden fazla modül |
| 3 saniye orta hızda yanıp sönme | Daire kayıtlı ama cevap vermiyor |
| Tekrarlayan çift flaş | 20 saniyelik eşleştirme penceresi açık |
| Seyrek tek flaşlar | Tuş takımı yanlış şifre nedeniyle kilitli |

### 7.2. Daire modülü
| LED | Anlamı |
| :--- | :--- |
| Tekrarlayan çift flaş | Eşleşmemiş |
| Loş, sabit | Normal, eşleşmiş |
| Hızlı yanıp sönme | Zil çalıyor (görsel zil) |
| Orta hızda yanıp sönme | Eşleştirme butonu basılı: şimdi bırakın |
| Yavaş yanıp sönme (1 Hz) | Eşleştirme modu: panel aranıyor |
| 2 saniye tam parlak | Eşleştirme başarılı |
| Sabit, arada kısa sönme | Sıfırlama butonu basılı: şimdi bırakın |
| Tekrarlayan üçlü flaş | Buton 15 saniyeden uzun basıldı; iptal |

---

## 8. Sorun giderme

Panelin seri monitörü tüm daireleri tek yerden gösterir: her modül belirli aralıklarla panele bir durum raporu
gönderir (çalışma süresi, son yeniden başlama nedeni, cevapsız kapı açma istekleri, reddedilen mesajlar, sinyal gücü).
Bir modülden uzun süre rapor gelmezse panel bunu uyarı olarak yazar (bkz. Yapılandırma Ayarları, heartbeat).

| Belirti | Olası neden | Ne yapmalı |
| :--- | :--- | :--- |
| Zil çalınca panel 3 saniye orta hızda yanıp sönüyor | Modülün elektriği yok veya sinyal ulaşmıyor | Modülün enerjisini kontrol edin; aradaki dairelerin modüllerinin açık olduğundan emin olun |
| Zil çalınca panel 1 saniye hızlı yanıp sönüyor | Bu numarada kayıtlı daire yok | Numarayı kontrol edin; gerekiyorsa daireyi eşleştirin |
| Eşleştirme olmuyor | Modül zaten eşleşmiş, 20 saniye doldu ya da başka bir modül de eşleştirme modunda | Modülü sıfırlayın (6.2), diğer modülleri eşleştirme modundan çıkarın, tekrar deneyin |
| Tuş takımı şifre kabul etmiyor, LED seyrek flaş yapıyor | Yanlış şifre kilidi | Kilit süresinin bitmesini bekleyin |
| Şifreler unutuldu | — | Panel şifre sıfırlama (6.1); etiketteki fabrika şifreleri geçerli olur |
| Modül sürekli çift flaş yapıyor | Modül eşleşmemiş | Eşleştirin (bölüm 5) |
