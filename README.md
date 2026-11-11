<div align="center">

# AKSIS OS

### Modern x86_64 Mikro Çekirdek ve Grafik Masaüstü İşletim Sistemi

[![Mimari: x86_64](https://img.shields.io/badge/Mimari-x86__64-blue.svg)](#sistem-mimarisi)
[![Önyükleyici: Limine UEFI / BIOS](https://img.shields.io/badge/%C3%96ny%C3%BCkleyici-Limine%20UEFI%20%2F%20BIOS-orange.svg)](#önyükleme-ve-uyumluluk)
[![Arayüz: 1080p DWM Kompozitör](https://img.shields.io/badge/Aray%C3%BCz-1080p%20FHD%20%7C%20DWM%20UI-success.svg)](#grafik-arayüz-ve-pencere-yöneticisi)
[![Ağ: RTL8139 / TCP / TLS](https://img.shields.io/badge/A%C4%9F-RTL8139%20%7C%20TCP%20%7C%20BearSSL-purple.svg)](#ağ-yığını-ve-web-motoru)
[![Lisans: GPL-3.0](https://img.shields.io/badge/Lisans-GPL--3.0-green.svg)](LICENSE)

<br/>

AKSIS OS, modern 64-bit donanım mimarisi üzerinde bağımsız çalışabilen; çekirdek seviyesinden grafik arayüzüne kadar sıfırdan C ve Assembly dilleriyle geliştirilmiş bir işletim sistemidir. UEFI ve Legacy BIOS standartlarını destekleyen Limine önyükleyicisi üzerinde çalışır; kendi çift tamponlu pencere kompozitörüne, kalıcı dosya sistemine ve donanım düzeyinde ağ yığınına sahiptir.

[Genel Bakış](#genel-bakış) • [Sistem Mimarisi](#sistem-mimarisi) • [Grafik Arayüz](#grafik-arayüz-ve-pencere-yöneticisi) • [Ağ ve Web](#ağ-yığını-ve-web-motoru) • [Derleme ve Çalıştırma](#derleme-ve-çalıştırma) • [Dizin Yapısı](#dizin-yapısı) • [Lisans](#lisans)

</div>

---

## Genel Bakış

AKSIS OS, modern donanım olanaklarını sade ve modüler bir mimariyle buluşturmayı hedefler. Geleneksel monolitik Unix benzeri yapıların temel kabiliyetlerini korurken; kullanıcı deneyimini doğrudan çekirdeğe entegre edilmiş bir pencere yöneticisi ve hafif bir yerel web çalışma zamanı ile zenginleştirir.

Sistemin temel tasarım hedefleri şunlardır:
- Dış bağımlılıklardan arındırılmış, doğrudan donanım seviyesinde çalışan kararlı bir çekirdek altyapısı kurmak.
- Yüksek çözünürlüklü (1080p) grafik modlarında bile düşük gecikmeli, akıcı bir masaüstü etkileşimi sunmak.
- Standartlara uyumlu ağ protokolleri ve HTML/JavaScript ayrıştırıcıları ile konuk sistem içerisinde hafif web tabanlı araçların çalıştırılmasını sağlamak.

---

## Sistem Mimarisi

Çekirdek mimarisi, donanım kaynaklarının doğrudan soyutlanması ve süreçler arası veri iletiminin en az ek yükle sağlanması prensibine dayanır.

```
+-------------------------------------------------------------------------+
|                           KULLANICI ALANI                               |
|   +------------------+  +-------------------+  +--------------------+   |
|   |  Terminal Kabuk  |  |   AXIS Web / JS   |  |  Sistem Paneli     |   |
|   +------------------+  +-------------------+  +--------------------+   |
|   |  Dosya Yöneticisi|  |   Medya Oynatıcı  |  |  Sistem Asistanı   |   |
|   +------------------+  +-------------------+  +--------------------+   |
+-------------------------------------------------------------------------+
|               GRAFİK VE PENCERE YÖNETİMİ (DWM / WDDM)                   |
|          Donanım Yüzeyleri, Çift Tamponlama, 2D Çizim Motoru            |
+-------------------------------------------------------------------------+
|                            HİZMET KATMANI                               |
|        Sanal Dosya Sistemi (VFS)       Ağ Yığını (TCP/IP & TLS)         |
|        Kalıcı Disk Sürücüsü (ATA/IDE)  PS/2 ve Giriş Aygıtları          |
+-------------------------------------------------------------------------+
|                         ÇEKİRDEK (AKSIS KERNEL)                         |
|   Kesme Tablosu (IDT)   Bellek Yönetimi (Paging/PAT)   Süreç Yöneticisi |
+-------------------------------------------------------------------------+
|                  ÖNYÜKLEYİCİ (Limine UEFI & BIOS x86_64)                |
+-------------------------------------------------------------------------+
|                                DONANIM                                  |
+-------------------------------------------------------------------------+
```

### Çekirdek ve Bellek Yönetimi
- **x86_64 Long Mode:** 64-bit korumalı kip, 4 düzeyli sayfalama (4-level paging) ve IDT tabanlı kesme dağıtımı.
- **PAT (Page Attribute Table):** Grafik işlem biriminin video çerçeve belleği için Write-Combining yapılandırması etkinleştirilmiştir; bu sayede 1080p çözünürlükte ekran tazeleme gecikmesi en aza indirilir.
- **Zamanlama Mekanizması:** TSC (Time Stamp Counter) tabanlı yüksek hassasiyetli saat takibi ve donanımsal RTC senkronizasyonu.

### Dosya Sistemi ve Kalıcılık
- **VFS (Sanal Dosya Sistemi):** Unix tarzı dosya izin modelini (okuma, yazma, çalıştırma izinleri) destekleyen dizin ağacı yapısı.
- **Kalıcı Depolama:** ATA/IDE kontrolcüsü üzerinden PIO modunda doğrudan disk erişimi sağlanır. `/dev/ide0` aygıtı üzerindeki değişiklikler kalıcı olarak disk bloklarına kaydedilir.

---

## Grafik Arayüz ve Pencere Yöneticisi

AKSIS OS, basit metin kabuklarının ötesinde kullanıcı dostu ve akıcı bir masaüstü arabirimi sunar.

- **DWM Tabanlı Kompozitör:** Pencereler birbirinden bağımsız yüzeyler (surface) olarak ayrılır ve 100 Hz pacing sınırı ile tam ekran kompozisyonunda birleştirilir.
- **Pencere Kontrolleri:** Pencereler taşınabilir, boyutlandırılabilir, simge durumuna küçültülebilir ve kapatılabilir. İçerik tamponları boyut değişimlerinde korunur.
- **Görsel Tasarım:**
  - Saydam pencere başlıkları ve menü katmanları.
  - 4x4 alt piksel örneklemeli yumuşatılmış fare imleci ve kontrol kenarlıkları.
  - Prosedürel vektörel simge çizimi ve Selawik tabanlı Türkçe anti-aliased bitmap font desteği.
  - Noto Sans Mono tabanlı UTF-8 uyumlu terminal görünümü.
- **Giriş Desteği:** Türkçe Q klavye dizilimi, karakter kodlama dönüşümleri ve fare sürücüsü sistem düzeyinde entegre edilmiştir.

---

## Ağ Yığını ve Web Motoru

İşletim sistemi, modern web teknolojilerini doğrudan kendi alt katmanlarında çalıştırabilecek donanım sürücülerine ve kütüphane entegrasyonlarına sahiptir:

```
[ Ağ Paketi ] ---> [ RTL8139 Sürücüsü ]
                          |
                   [ ARP / IPv4 ]
                          |
              +-----------+-----------+
              |                       |
          [ ICMP / DHCP ]         [ TCP Soketi ]
                                      |
                              [ BearSSL (TLS 1.3) ]
                                      |
                              [ HTTP / HTTPS İstemcisi ]
                                      |
                     +----------------+----------------+
                     |                                 |
           [ Lexbor HTML5/DOM ]               [ QuickJS ES2020 ]
                     |                                 |
                     +----------------+----------------+
                                      |
                          [ AXIS Web Uygulama Katmanı ]
```

- **Ağ Arayüzü:** Realtek RTL8139 PCI denetleyicisi ile yerel Ethernet paket iletimi.
- **Protokol Yığını:** ARP adresi çözümleme, IPv4 paketleme, ICMP ping yanıtları, DHCP ile dinamik IP yapılandırması ve tam çift yönlü TCP bağlantıları.
- **Şifreli İletişim (TLS):** BearSSL kütüphanesi ile TLS 1.2/1.3 el sıkışması ve HTTPS bağlantı güvenliği.
- **Yerel Web Çalışma Zamanı:**
  - **Lexbor:** C99 standardında yazılmış, harici bağımlılığı olmayan tam HTML5, DOM ve CSS ayrıştırma motoru.
  - **QuickJS:** ES2020 standardını destekleyen hafif JavaScript motoru; çekirdek içi DOM olaylarıyla çift yönlü köprü oluşturur.
- **Güvenli Kabuk (SSH):** Sistem, uzaktan yönetim için yerleşik bir SSH-2.0 sunucu oturum yöneticisi barındırır.

---

## Derleme ve Çalıştırma

### Gereksinimler

Sistemi kaynak koddan derlemek ve test etmek için aşağıdaki araçlar gereklidir:

- `gcc` veya `clang` (x86_64 hedefli çapraz derleme desteği)
- `nasm` (Assembly derleyicisi)
- `make`
- `xorriso` ve `mtools` (Önyüklenebilir ISO ve FAT imajı üretimi için)
- `qemu-system-x86_64` (Sanal makine ortamında çalıştırma için)

macOS üzerinde gerekli araçları yüklemek için:
```bash
brew install nasm qemu xorriso mtools
```

### Adım Adım Derleme

1. Depoyu yerel ortama kopyalayın:
   ```bash
   git clone --recurse-submodules https://github.com/aliyabuz25/AKSIS-OS.git
   cd AKSIS-OS
   ```

2. Çekirdek ve önyüklenebilir hibrit ISO dosyasını derleyin:
   ```bash
   make iso
   ```
   *Bu işlem sonucunda `build/out/shenlong.iso` dosyası üretilir.*

3. Sanal makine üzerinde çalıştırın:
   ```bash
   # BIOS kipi ile çalıştırma
   make run

   # UEFI kipi ile çalıştırma
   make run-uefi
   ```

4. Doğrulama ve arayüz testlerini yürütün:
   ```bash
   make test
   ```

---

## Dizin Yapısı

```
AKSIS-OS/
├── assets/          # Duvar kağıtları, TrueType yazı tipleri, boot logoları ve HTML arayüz şablonları
├── bootloader/      # MBR, Stage2 ve CD açılış montaj kodları
├── build/           # Limine konfigürasyonları, linker betikleri ve ortam kurulum dosyaları
├── docs/            # Mimari detaylar, web motoru belgeleri ve geliştirici notları
├── kernel/          # Çekirdek kaynak kodları (Bellek, IDT, VFS, GPU, DWM, TCP/IP, UI, Sürücüler)
│   ├── axis_web/    # Lexbor ve QuickJS çalışma zamanı bağlayıcıları
│   └── libc/        # Çekirdek içi bağımsız standart C başlık dosyaları
├── limine/          # Limine UEFI ve BIOS önyükleme ikilileri
├── shell/           # Kullanıcı kabuğu kaynakları
├── tools/           # Yazı tipi oluşturucular, test araçları ve paketleme betikleri
└── vendor/          # Bağımsız harici kütüphaneler (BearSSL, QuickJS, Lexbor, stb, pl_mpeg)
```

---

## Lisans

Bu proje [GNU General Public License v3.0](LICENSE) şartları altında sunulmaktadır. Ayrıştırma ve kullanım hakları için lisans metnini inceleyebilirsiniz.
EOF