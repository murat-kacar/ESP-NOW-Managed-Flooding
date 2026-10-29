# 9. Proje yapısı

Tarih: 2026-10-29
Durum: Kabul edildi

## Bağlam
İki ayrı rol (dış panel, daire modülü) aynı haberleşme koduna ihtiyaç duyar.
Geliştirme ortamı: ESP-IDF v6.1 (`C:\esp\v6.1\esp-idf`).

## Karar
ESP-IDF'in belgelenmiş yöntemi: her rol ayrı bir ESP-IDF projesi, ortak kod `EXTRA_COMPONENT_DIRS` ile paylaşılan bileşenlerde.

```
apps/panel/        Dış panel projesi
apps/apartment/    Daire modülü projesi
components/        Ortak bileşenler (db_net, ...)
docs/              Şartname ve ADR'ler
```

- CMake şablonu ESP-IDF v6.1 `tools/templates/sample_project` ve `examples/get-started/hello_world` ile aynıdır (`cmake_minimum_required(VERSION 3.22)`, `MINIMAL_BUILD ON`).
- Wi-Fi başlatma sırası ESP-IDF `examples/wifi/espnow` ve esp-now `examples/get-started` örneklerine göre yapılır.
- Bağımlılıklar `idf_component.yml` ile; `dependencies.lock` sürüm kontrolüne eklenir.
- Her projenin kendi `sdkconfig.defaults` dosyası vardır.

## Sonuçlar
- Her rol ayrı derlenir: `idf.py -C apps/panel build`, `idf.py -C apps/apartment build`.
- espressif/esp-now 2.5.3, IDF 6.0 derleme düzeltmesi içerir; IDF 6.1 ile uyumu ilk derlemede doğrulanacak.
